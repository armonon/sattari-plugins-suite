#include "MainPluginProcessor.h"
#include "MainPluginEditor.h"

namespace
{
// Two-phase load (§A3). The single-phase path decodes AND analyses before the deck
// ever sees audio, so time-to-playable scaled with track length: measured 424ms at
// 2 minutes and 1365ms at 5, against a 200ms bound. Decode is the only work that has
// to precede sound; analysis is published afterwards through the deck's refined-
// analysis channel, which already existed for neural refinement.
class TwoPhaseAudioLoadJob final : public juce::ThreadPoolJob
{
public:
    using Decoded  = std::shared_ptr<DeckPlayer::LoadedAudio>;
    TwoPhaseAudioLoadJob (juce::File f,
                          std::function<Decoded (const juce::File&)> decodeFn,
                          std::function<void (TrackAnalysis&, const juce::AudioBuffer<float>&, double, const juce::File&)> analyseFn,
                          std::function<void (std::shared_ptr<const DeckPlayer::LoadedAudio>)> onAudioReady,
                          std::function<void (std::shared_ptr<const TrackAnalysis>)> onAnalysisReady)
        : ThreadPoolJob ("StemDeck audio load (2-phase)"), file (std::move (f)),
          decode (std::move (decodeFn)), analyse (std::move (analyseFn)),
          onAudio (std::move (onAudioReady)), onAnalysis (std::move (onAnalysisReady)) {}

    JobStatus runJob() override
    {
        auto loaded = decode (file);
        if (loaded == nullptr)
            return jobHasFinished;

        // PHASE 1 — the deck is playable from here.
        std::shared_ptr<const DeckPlayer::LoadedAudio> published = loaded;
        juce::MessageManager::callAsync ([published, cb = onAudio]() mutable
        { if (cb) cb (std::move (published)); });

        if (shouldExit())
            return jobHasFinished;

        // PHASE 2 — analysis into a SEPARATE object. `loaded` is now visible to the
        // audio thread, so filling its analysis field in place would be a race.
        auto analysis = std::make_shared<TrackAnalysis>();
        analyse (*analysis, loaded->audio, loaded->sourceSampleRate, file);

        if (shouldExit())
            return jobHasFinished;

        juce::MessageManager::callAsync (
            [a = std::shared_ptr<const TrackAnalysis> (std::move (analysis)), cb = onAnalysis]() mutable
        { if (cb) cb (std::move (a)); });
        return jobHasFinished;
    }

private:
    juce::File file;
    std::function<Decoded (const juce::File&)> decode;
    std::function<void (TrackAnalysis&, const juce::AudioBuffer<float>&, double, const juce::File&)> analyse;
    std::function<void (std::shared_ptr<const DeckPlayer::LoadedAudio>)> onAudio;
    std::function<void (std::shared_ptr<const TrackAnalysis>)> onAnalysis;
};

class AudioLoadJob final : public juce::ThreadPoolJob
{
public:
    AudioLoadJob (juce::File f,
                  std::function<std::shared_ptr<const DeckPlayer::LoadedAudio>(const juce::File&)> loader,
                  std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> done)
        : ThreadPoolJob ("StemDeck audio load"), file (std::move (f)), load (std::move (loader)), onDone (std::move (done)) {}

    JobStatus runJob() override
    {
        auto loaded = load (file);
        juce::MessageManager::callAsync ([loaded, cb = onDone]() mutable
        {
            if (cb)
                cb (std::move (loaded));
        });
        return jobHasFinished;
    }

private:
    juce::File file;
    std::function<std::shared_ptr<const DeckPlayer::LoadedAudio>(const juce::File&)> load;
    std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> onDone;
};

// This build decodes bare char* literals as Latin-1; wrap non-ASCII text so it
// is interpreted as UTF-8 (otherwise "•" / "→" render as mojibake).
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                       float min, float max, float def, const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, 0.001f);   // keep fine internal precision
    const auto unit = suffix.trim();                            // host-facing label only
    // Display 1 decimal place so readouts stay short ("120.0") instead of the
    // 0.001-interval default "120.000". The Slider appends its own unit suffix, so the
    // value text must NOT include the unit here, or it would double ("0.0 dB dB").
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes()
            .withLabel (unit)
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam (const juce::String& id, const juce::String& name,
                                                         const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (id, 1), name, choices, def);
}

juce::File withSuffix (const juce::File& file, const juce::String& suffix)
{
    return file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension() + suffix).withFileExtension ("wav");
}

juce::String deckNameFor (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, kMaxChannels - 1, deckIndex)));
}

// Crossfader response law. Returns { A/C-bus gain, B/D-bus gain } for a fader
// position in [0,1], shaped by `curve` (0 Smooth, 1 Linear, 2 Sharp, 3 Cut).
// `reverse` (hamster) flips the fader sense so the two busses trade ends.
inline std::pair<float, float> crossfaderBusGains (float x, int curve, bool reverse) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    if (reverse)
        x = 1.0f - x;

    const float halfPi = juce::MathConstants<float>::halfPi;
    switch (juce::jlimit (0, 3, curve))
    {
        case 1:   // Linear — constant-sum; gentle -6 dB dip through the centre.
            return { 1.0f - x, x };

        case 2:   // Sharp — equal-power on a centre-steepened position warp.
        {
            const float d  = x - 0.5f;                                   // -0.5..0.5
            const float w  = std::copysign (std::pow (std::abs (d) * 2.0f, 0.45f), d);
            const float xs = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * w); // steeper near centre
            return { std::cos (xs * halfPi), std::sin (xs * halfPi) };
        }

        case 3:   // Cut / transform — each side full except in a narrow edge band.
        {
            const float band = 0.10f;
            return { juce::jlimit (0.0f, 1.0f, (1.0f - x) / band),
                     juce::jlimit (0.0f, 1.0f, x / band) };
        }

        default:  // Smooth — classic equal-power blend.
            return { std::cos (x * halfPi), std::sin (x * halfPi) };
    }
}
}

// Constructed before anything can dispatch. applyEvent is a member, so the
// dispatcher can be handed a stable callable for the processor's whole lifetime.
MainPluginProcessor::MainPluginProcessor()
    : AudioProcessor (BusesProperties().withInput ("Mic / Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // The dispatcher's applier is a member function, so it stays valid for the
    // processor's whole lifetime and is the same callable live and on replay.
    eventDispatcher = std::make_unique<stemdeck::events::Dispatcher> (
        [this] (const stemdeck::events::Event& e) { applyEvent (e); });

    // Listen to every parameter that maps to a ParamId, so moving a control emits an
    // event without unpicking its SliderAttachment.
    for (auto* p : getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (mapApvtsId (wid->paramID).valid)
                apvts.addParameterListener (wid->paramID, this);

    formatManager.registerBasicFormats();
#if JUCE_USE_MP3AUDIOFORMAT
    formatManager.registerFormat (new juce::MP3AudioFormat(), true);
#endif
    apvts.state.setProperty ("schema", "SattariStemDeck.v0.3.8", nullptr);

    // Bootstrap the channel list: numDecks TrackDeck channels, so every existing
    // deck-indexed call site keeps working while add/remove is layered on top.
    channels.reserve ((size_t) kMaxChannels);
    for (int i = 0; i < kDefaultChannels; ++i)
        channels.push_back (makeChannel (i, ChannelType::trackDeck));
    publishChannels();

    for (int i = 0; i < numDecks; ++i)
    {
        liveKeyPitchClass[(size_t) i].store (-1, std::memory_order_relaxed);
        liveKeyIsMinor[(size_t) i].store (0, std::memory_order_relaxed);
        liveKeyConfidence[(size_t) i].store (0.0f, std::memory_order_relaxed);
        stemRestoreGain[(size_t) i].fill (100.0f);
        stemSoloLane[(size_t) i] = -1;
    }
    for (auto& deck : deckLoadGeneration)
        for (auto& lane : deck)
            lane.store (0, std::memory_order_relaxed);
    for (auto& pad : padLoadGeneration)
        pad.store (0, std::memory_order_relaxed);
    cacheDeckParamIds();
    // Optional trained key model (shared with Auto-Pitch). Absent -> DSP key stands.
    neuralKey.loadDefaultModel();
    // Optional trained beat model (beat_this). Absent -> DSP beat grid stands.
    neuralBeat.loadDefaultModel();
    buildMidiTargets();
    // The Tracktion engine is created LAZILY — see ensureTeWrapperOnMessageThread.
    // Nothing in the current build populates its Edit, so eagerly constructing it
    // cost startup time and per-block CPU for silence, and was the sole reason a
    // second plugin instance could not exist.
}

// ── Channel accessors ────────────────────────────────────────────────────────
// No dynamic_cast: ChannelBody::trackDeckPlayer() is a virtual returning nullptr for
// non-TrackDeck bodies, so this is safe to call from the audio thread.
DeckPlayer& MainPluginProcessor::deckAt (int deckSlot) noexcept
{
    for (auto& ch : channels)
        if (ch != nullptr && ch->slotIndex == deckSlot)
            if (auto* body = ch->getBody())
                if (auto* dp = body->trackDeckPlayer())
                    return *dp;
    return nullDeck;
}

const DeckPlayer& MainPluginProcessor::deckAt (int deckSlot) const noexcept
{
    return const_cast<MainPluginProcessor*> (this)->deckAt (deckSlot);
}

void MainPluginProcessor::publishChannels()
{
    // Does any channel need the live device input? (drives mic capture in processBlock)
    bool anyInput = false;
    for (auto& id : channelIdBySlot)
        id.store (-1, std::memory_order_relaxed);
    for (auto& c : channels)
        if (c != nullptr)
        {
            if (juce::isPositiveAndBelow (c->slotIndex, kMaxChannels))
                channelIdBySlot[(size_t) c->slotIndex].store (
                    c->id, std::memory_order_release);
            if (c->type() == ChannelType::input)
                anyInput = true;
        }
    hasInputChannels.store (anyInput, std::memory_order_relaxed);

    channelGraph.mutate ([this] (std::vector<std::shared_ptr<Channel>>& v) { v = channels; });
}

// Build a fully-wired channel for slot `index` (frame hook included), so the
// constructor bootstrap and runtime addChannel() stay identical.
std::shared_ptr<Channel> MainPluginProcessor::makeChannel (int index, ChannelType type, ChannelID requestedId)
{
    std::unique_ptr<ChannelBody> body;
    juce::String defaultName;
    switch (type)
    {
        case ChannelType::input:
            body = std::make_unique<InputBody>();          defaultName = "Input";      break;
        case ChannelType::midiInstrument:
            body = std::make_unique<MidiInstrumentBody>(); defaultName = "Instrument"; break;
        default:
        {
            auto tdb = std::make_unique<TrackDeckBody>();
            // Per-stem plugin chains. Only the four real decks have them; a TrackDeck
            // beyond deck D has no stem chains and simply gets no hook.
            if (index < HostGraph::kNumDecks)
            {
                const int deckIdx = index;
                tdb->onStemFx = [this, deckIdx] (int stemIndex, juce::AudioBuffer<float>& stem)
                {
                    if (currentBlockMidi == nullptr) return;
                    hostGraph.processChain (HostGraph::stemChain (deckIdx, stemIndex), stem, *currentBlockMidi);
                };
            }
            const int deckSlot = index;
            tdb->onPreFaderCapture = [this, deckSlot] (
                juce::AudioBuffer<float>& source, int)
            {
                if (juce::isPositiveAndBelow (deckSlot, kMaxChannels))
                    deckSourceRecorders[(size_t) deckSlot].captureBlock (source);
            };
            body = std::move (tdb);
            defaultName = deckLabel (index);
            break;
        }
    }

    const ChannelID assignedId = requestedId >= 0 ? requestedId : nextChannelId++;
    nextChannelId = juce::jmax (nextChannelId, assignedId + 1);
    auto ch = std::make_shared<Channel> (assignedId, std::move (body));
    ch->name      = defaultName;
    ch->slotIndex = index;
    ch->cfSide    = type != ChannelType::trackDeck ? CrossfaderSide::center
                  : (index % 2 == 0) ? CrossfaderSide::ac : CrossfaderSide::bd;

    const int i = index;
    ch->onPostBody = [this, i] (juce::AudioBuffer<float>& buf, int numSamples)
    {
        if (currentBlockMidi != nullptr
            && i < HostGraph::kNumChannelChains)
            hostGraph.processChain (
                HostGraph::channelChain (i), buf, *currentBlockMidi);

        if (i < kMaxChannels)
        {
            deckPrintedRecorders[(size_t) i].captureBlock (buf);
            for (int c = 0; c < 2 && c < buf.getNumChannels(); ++c)
            {
                const float blockPeak = buf.getMagnitude (c, 0, numSamples);
                const float previous  = deckPeak[(size_t) i][(size_t) c].load (std::memory_order_relaxed);
                deckPeak[(size_t) i][(size_t) c].store (juce::jmax (blockPeak, previous * 0.86f),
                                                        std::memory_order_relaxed);
            }
        }
    };
    return ch;
}

// Rebuild the channel list from persisted state. State with no <CHANNELS> child
// predates the channel architecture and restores as numDecks TrackDeck channels,
// so old sessions/presets open unchanged.
void MainPluginProcessor::restoreChannelsFromState()
{
    auto chTree = apvts.state.getChildWithName ("CHANNELS");

    std::vector<std::shared_ptr<Channel>> rebuilt;
    nextChannelId = 0;
    if (chTree.isValid() && chTree.getNumChildren() > 0)
    {
        for (int i = 0; i < chTree.getNumChildren() && (int) rebuilt.size() < kMaxChannels; ++i)
        {
            const auto c = chTree.getChild (i);
            const int slot = juce::jlimit (0, kMaxChannels - 1, (int) c.getProperty ("slot", i));
            const ChannelID id = (ChannelID) (juce::int64) c.getProperty ("id", (juce::int64) -1);
            auto ch = makeChannel (slot,
                                   channelTypeFromId (c.getProperty ("type", "trackDeck").toString()),
                                   id);
            const auto name = c.getProperty ("name").toString();
            if (name.isNotEmpty()) ch->name = name;
            ch->cfSide = (CrossfaderSide) (int) c.getProperty ("cfSide", (int) ch->cfSide);
            rebuilt.push_back (std::move (ch));
        }
    }

    if (rebuilt.empty())
        for (int i = 0; i < kDefaultChannels; ++i)
            rebuilt.push_back (makeChannel (i, ChannelType::trackDeck));

    for (auto& ch : rebuilt)
        ch->prepare (currentSampleRate > 0.0 ? currentSampleRate : 44100.0,
                     juce::jmax (1, getBlockSize()),
                     juce::jmax (1, getTotalNumOutputChannels()));

    channels = std::move (rebuilt);
    publishChannels();
}

int MainPluginProcessor::channelIndexForId (ChannelID id) const noexcept
{
    for (int i = 0; i < (int) channels.size(); ++i)
        if (channels[(size_t) i] != nullptr && channels[(size_t) i]->id == id)
            return i;
    return -1;
}

ChannelID MainPluginProcessor::channelIdForIndex (int index) const noexcept
{
    if (juce::isPositiveAndBelow (index, (int) channels.size())
        && channels[(size_t) index] != nullptr)
        return channels[(size_t) index]->id;
    return -1;
}

ChannelID MainPluginProcessor::channelIdForSlot (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, kMaxChannels)
               ? channelIdBySlot[(size_t) slot].load (std::memory_order_acquire)
               : -1;
}

int MainPluginProcessor::channelSlotForId (ChannelID id) const noexcept
{
    for (int slot = 0; slot < kMaxChannels; ++slot)
        if (channelIdBySlot[(size_t) slot].load (
                std::memory_order_acquire) == id)
            return slot;
    return -1;
}

// ── Plugin delay compensation (spec §3.3) ───────────────────────────────────
// A channel whose FX chain reports latency arrives late. Delay every OTHER channel
// by the difference so they all land aligned at the master bus, and tell the host
// our total latency so it can compensate the plugin as a whole.
//
// Live-input channels are deliberately EXCLUDED from being held back: adding delay
// to a mic a performer is monitoring makes it unusable (§3.3 — the monitoring path
// and the compensated mix path are in tension, and monitoring wins for a live tool).
void MainPluginProcessor::refreshLatencyCompensation()
{
    // Cache each channel's FX-chain latency (querying the graph takes a lock, so it
    // can only happen here on the message thread). The per-block alignment in
    // processBlock then combines it with the body's LIVE latency — key-lock toggles
    // RubberBand's latency on and off at runtime, so it cannot be cached here.
    int maxChainLatency = 0;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        if (channels[i] == nullptr) continue;
        const int chainIndex = channels[i]->slotIndex;
        int lat = chainIndex < HostGraph::kNumChannelChains
                ? hostGraph.getChainLatencySamples (
                      HostGraph::channelChain (chainIndex)) : 0;

        // Per-stem chains: publish each stem's latency to the deck body so it can align
        // the four stems before summing, and fold the SLOWEST stem into this channel's
        // latency — that is what the deck's output is actually delayed by.
        if (chainIndex < HostGraph::kNumDecks)
            if (auto* tdb = dynamic_cast<TrackDeckBody*> (channels[i]->getBody()))
            {
                for (int st = 0; st < HostGraph::kNumStemsPerDeck; ++st)
                    tdb->setStemChainLatency (st, hostGraph.getChainLatencySamples (
                        HostGraph::stemChain (chainIndex, st)));
                lat += tdb->getMaxStemChainLatency();
            }

        channels[i]->setChainLatencySamples (lat);
        maxChainLatency = juce::jmax (maxChainLatency, lat);
    }

    // Report to the host. Body (time-stretch) latency varies with key-lock, so the
    // reported figure is the stable chain+master part.
    const int masterLat = hostGraph.getChainLatencySamples (HostGraph::Chain::master);
    setLatencySamples (maxChainLatency + masterLat);
}

int MainPluginProcessor::addChannel (ChannelType type)
{
    // These enum values are reserved in the session schema but do not yet have
    // concrete ChannelBody implementations. Refuse them instead of silently
    // constructing a TrackDeckBody and persisting the wrong type.
    if (type == ChannelType::sampler
        || type == ChannelType::pluginHost)
        return -1;

    if ((int) channels.size() >= kMaxChannels)
        return -1;   // parameter pool exhausted

    std::array<bool, kMaxChannels> slotUsed {};
    for (const auto& existing : channels)
        if (existing != nullptr
            && juce::isPositiveAndBelow (
                existing->slotIndex, kMaxChannels))
            slotUsed[(size_t) existing->slotIndex] = true;
    int slot = -1;
    for (int candidate = 0; candidate < kMaxChannels; ++candidate)
        if (! slotUsed[(size_t) candidate])
        {
            slot = candidate;
            break;
        }
    if (slot < 0)
        return -1;
    // TrackDeck has a fixed APVTS bank and full performance surface for A..D.
    // Do not create a fifth silent/invisible deck until the parameter model itself
    // is dynamically extensible.
    if (type == ChannelType::trackDeck && slot >= numDecks)
        return -1;

    stemdeck::events::Event event;
    event.type = stemdeck::events::EventType::channelAdd;
    event.channelAdd.id = nextChannelId;
    switch (type)
    {
        case ChannelType::input:
            event.channelAdd.kind =
                stemdeck::events::ChannelKind::input; break;
        case ChannelType::midiInstrument:
            event.channelAdd.kind =
                stemdeck::events::ChannelKind::midiInstrument; break;
        case ChannelType::sampler:
            event.channelAdd.kind =
                stemdeck::events::ChannelKind::sampler; break;
        case ChannelType::pluginHost:
            event.channelAdd.kind =
                stemdeck::events::ChannelKind::pluginHost; break;
        case ChannelType::trackDeck:
        default:
            event.channelAdd.kind =
                stemdeck::events::ChannelKind::trackDeck; break;
    }
    event.channelAdd.slotIndex = (std::uint8_t) slot;
    eventDispatcher->dispatch (event);
    return channelIndexForId (event.channelAdd.id);
}

bool MainPluginProcessor::removeChannel (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) channels.size()) || channels.size() <= 1)
        return false;   // always keep at least one channel

    stemdeck::events::Event event;
    event.type = stemdeck::events::EventType::channelRemove;
    event.channelRemove.id = channels[(size_t) index]->id;
    eventDispatcher->dispatch (event);
    return channelIndexForId (event.channelRemove.id) < 0;
}

ChannelType MainPluginProcessor::getChannelType (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) channels.size()))
        if (auto& ch = channels[(size_t) index])
            return ch->type();
    return ChannelType::trackDeck;
}

ChannelBody* MainPluginProcessor::getChannelBody (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) channels.size()))
        if (auto& ch = channels[(size_t) index])
            return ch->getBody();
    return nullptr;
}

float MainPluginProcessor::getChannelActivity (int index) const noexcept
{
    if (! juce::isPositiveAndBelow (index, (int) channels.size())
        || channels[(size_t) index] == nullptr)
        return 0.0f;
    if (channels[(size_t) index]->type() == ChannelType::trackDeck)
    {
        const int slot = channels[(size_t) index]->slotIndex;
        if (juce::isPositiveAndBelow (slot, numDecks))
            return juce::jmax (getDeckPeak (slot, 0), getDeckPeak (slot, 1));
    }
    if (auto* input = dynamic_cast<InputBody*> (channels[(size_t) index]->getBody()))
        return input->getInputPeak();
    if (auto* instrument = dynamic_cast<MidiInstrumentBody*> (
            channels[(size_t) index]->getBody()))
        return instrument->isActive() ? 0.62f : 0.0f;
    return 0.0f;
}

// ── A1 phase 3: converted controls ───────────────────────────────────────────
// These no longer mutate state directly. They emit an event; applyEvent() performs
// the change. Replay calls applyEvent() with the same event and therefore takes the
// identical path — that equivalence is the whole basis of the gate.
void MainPluginProcessor::toggleChannelMute (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) channels.size())) return;
    auto& ch = channels[(size_t) index];
    if (ch == nullptr) return;

    stemdeck::events::Scope sc;
    sc.kind    = stemdeck::events::ScopeKind::channel;
    sc.channel = ch->id;
    eventDispatcher->dispatchParam (sc, stemdeck::events::ParamId::channelMute,
                                    ch->isMuted() ? 0.0 : 1.0);
}

void MainPluginProcessor::toggleChannelSolo (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) channels.size())) return;
    auto& ch = channels[(size_t) index];
    if (ch == nullptr) return;

    stemdeck::events::Scope sc;
    sc.kind    = stemdeck::events::ScopeKind::channel;
    sc.channel = ch->id;
    eventDispatcher->dispatchParam (sc, stemdeck::events::ParamId::channelSolo,
                                    ch->isSoloed() ? 0.0 : 1.0);
}

// The ONE place converted state changes. Live dispatch and replay both land here.
void MainPluginProcessor::applyEvent (const stemdeck::events::Event& e)
{
    using namespace stemdeck::events;
    if (e.type == EventType::channelAdd)
    {
        if (channelIndexForId ((ChannelID) e.channelAdd.id) >= 0
            || (int) channels.size() >= kMaxChannels)
            return;
        ChannelType type = ChannelType::trackDeck;
        switch (e.channelAdd.kind)
        {
            case ChannelKind::input:          type = ChannelType::input; break;
            case ChannelKind::midiInstrument:type = ChannelType::midiInstrument; break;
            case ChannelKind::sampler:        type = ChannelType::sampler; break;
            case ChannelKind::pluginHost:     type = ChannelType::pluginHost; break;
            case ChannelKind::trackDeck:
            case ChannelKind::count:
            default:                          break;
        }
        const int slot = juce::jlimit (
            0, kMaxChannels - 1, (int) e.channelAdd.slotIndex);
        for (const auto& existing : channels)
            if (existing != nullptr && existing->slotIndex == slot)
                return;   // malformed replay: two channels cannot own one bank
        auto channel = makeChannel (
            slot, type, (ChannelID) e.channelAdd.id);
        channel->prepare (
            currentSampleRate > 0.0 ? currentSampleRate : 44100.0,
            juce::jmax (1, getBlockSize()),
            juce::jmax (1, getTotalNumOutputChannels()));
        channels.push_back (std::move (channel));
        publishChannels();
        refreshLatencyCompensation();
        return;
    }
    if (e.type == EventType::channelRemove)
    {
        const int index = channelIndexForId (
            (ChannelID) e.channelRemove.id);
        if (! juce::isPositiveAndBelow (
                index, (int) channels.size())
            || channels.size() <= 1)
            return;
        const int retiredSlot = channels[(size_t) index]->slotIndex;
        if (juce::isPositiveAndBelow (retiredSlot, kMaxChannels))
            for (auto& generation : deckLoadGeneration[(size_t) retiredSlot])
                generation.fetch_add (1, std::memory_order_relaxed);
        channels.erase (channels.begin() + index);
        publishChannels();
        refreshLatencyCompensation();
        return;
    }
    if (e.type == EventType::channelReorder)
    {
        const int from = channelIndexForId (
            (ChannelID) e.channelReorder.id);
        const int to = juce::jlimit (
            0, (int) channels.size() - 1,
            (int) e.channelReorder.toPos);
        if (! juce::isPositiveAndBelow (
                from, (int) channels.size()) || from == to)
            return;
        auto channel = channels[(size_t) from];
        channels.erase (channels.begin() + from);
        channels.insert (channels.begin() + to, std::move (channel));
        publishChannels();
        return;
    }
    if (e.type != EventType::param) return;

    const bool positionalInsertScope = e.scope.kind == ScopeKind::insert;
    std::shared_ptr<Channel> targetChannel;
    if (e.scope.kind != ScopeKind::master && ! positionalInsertScope)
    {
        const auto snapshot = channelGraph.current();
        for (const auto& channel : snapshot->channels)
            if (channel != nullptr
                && channel->id == (ChannelID) e.scope.channel)
            {
                targetChannel = channel;
                break;
            }
    }
    const int slot = positionalInsertScope
                         ? (int) e.scope.channel
                         : targetChannel != nullptr
                               ? targetChannel->slotIndex : -1;
    switch (e.param.id)
    {
        case ParamId::channelMute:
            if (targetChannel != nullptr)
                targetChannel->setMuted (e.param.value > 0.5);
            break;

        case ParamId::channelSolo:
            if (targetChannel != nullptr)
                targetChannel->setSoloed (e.param.value > 0.5);
            break;

        case ParamId::stemMute:
        case ParamId::stemSolo:
        {
            const int stem = (int) e.scope.stemLane - (int) StemLane::drums;
            if (targetChannel == nullptr
                || ! juce::isPositiveAndBelow (slot, numDecks)
                || ! juce::isPositiveAndBelow (stem, HostGraph::kNumStemsPerDeck))
                break;

            auto parameterFor = [this, slot] (int s) -> juce::RangedAudioParameter*
            {
                const int lane = DeckPlayer::drumsStem + s;
                return apvts.getParameter (deckPrefix (slot) + "Stem"
                                           + DeckPlayer::stemLaneId (lane) + "Gain");
            };
            auto writeGain = [this, &parameterFor] (int s, float percent)
            {
                if (auto* p = parameterFor (s))
                {
                    applyingEvent.store (true, std::memory_order_release);
                    p->setValueNotifyingHost (p->convertTo0to1 (percent));
                    applyingEvent.store (false, std::memory_order_release);
                }
            };

            constexpr float muteEpsilon = 0.5f;
            const bool enabled = e.param.value > 0.5;
            if (e.param.id == ParamId::stemMute)
            {
                if (auto* p = parameterFor (stem))
                {
                    const float current = p->convertFrom0to1 (p->getValue());
                    if (enabled && current > muteEpsilon)
                        stemRestoreGain[(size_t) slot][(size_t) stem] = current;
                }
                stemMuted[(size_t) slot][(size_t) stem] = enabled;
                writeGain (stem, enabled ? stemdeck::stemgain::kMin
                                         : stemRestoreGain[(size_t) slot][(size_t) stem]);
            }
            else
            {
                stemSoloLane[(size_t) slot] = enabled ? stem : -1;
                for (int s = 0; s < HostGraph::kNumStemsPerDeck; ++s)
                {
                    if (auto* p = parameterFor (s))
                    {
                        const float current = p->convertFrom0to1 (p->getValue());
                        if (current > muteEpsilon)
                            stemRestoreGain[(size_t) slot][(size_t) s] = current;
                    }

                    const bool shouldMute = enabled ? s != stem
                                                    : stemMuted[(size_t) slot][(size_t) s];
                    writeGain (s, shouldMute ? stemdeck::stemgain::kMin
                                             : stemRestoreGain[(size_t) slot][(size_t) s]);
                }
            }
            break;
        }

        case ParamId::cfSide:
            if (slot >= 0) applyDeckCfSide (slot, (int) e.param.value);
            break;

        // ── Input / instrument channels ──────────────────────────────────────
        // No APVTS parameter behind these: they live on the channel BODY, so they
        // are applied directly here rather than through the bridge.
        case ParamId::inputGain:
        case ParamId::inputMonitor:
        case ParamId::inputSource:
            if (auto* in = targetChannel != nullptr
                               ? dynamic_cast<InputBody*> (
                                     targetChannel->getBody())
                               : nullptr)
            {
                if (e.param.id == ParamId::inputGain)         in->setInputGain ((float) e.param.value);
                else if (e.param.id == ParamId::inputMonitor) in->setMonitorEnabled (e.param.value > 0.5);
                else                                          in->setSourceChannel ((int) e.param.value);
            }
            break;

        case ParamId::instGain:
        case ParamId::instMidiChannel:
            if (auto* mi = targetChannel != nullptr
                               ? dynamic_cast<MidiInstrumentBody*> (
                                     targetChannel->getBody())
                               : nullptr)
            {
                if (e.param.id == ParamId::instGain) mi->setGain ((float) e.param.value);
                else                                 mi->setMidiChannel ((int) e.param.value);
            }
            break;

        // ── FX insert bypass ─────────────────────────────────────────────────
        // Scope carries the chain in `channel` and the insert POSITION in `index`,
        // because HostGraph addresses inserts positionally.
        case ParamId::insertBypass:
            if (slot >= 0) setChainPluginBypassed (slot, (int) e.param.index, e.param.value > 0.5);
            break;

        case ParamId::insertParam:
        {
            const int insertIndex = (int) ((e.param.index >> 16) & 0xffffu);
            const int parameterIndex = (int) (e.param.index & 0xffffu);
            hostGraph.setPluginParameterByStableId (
                (HostGraph::Chain) slot, insertIndex, e.param.stableId,
                parameterIndex, (float) e.param.value);
            hostedParameterValues[
                std::make_tuple (slot, insertIndex, e.param.stableId)]
                    = (float) e.param.value;
            break;
        }

        default:
        {
            // APVTS-backed params: write the parameter with the guard set so the
            // resulting notification is not dispatched straight back.
            const auto apvtsId = apvtsIdFor (e);
            if (apvtsId.isNotEmpty())
                if (auto* p = apvts.getParameter (apvtsId))
                {
                    applyingEvent.store (true, std::memory_order_release);
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) e.param.value));
                    applyingEvent.store (false, std::memory_order_release);
                }
            break;
        }
    }
}

bool MainPluginProcessor::isChannelMuted (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) channels.size()))
        if (auto& ch = channels[(size_t) index]) return ch->isMuted();
    return false;
}

bool MainPluginProcessor::isChannelSoloed (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) channels.size()))
        if (auto& ch = channels[(size_t) index]) return ch->isSoloed();
    return false;
}

int MainPluginProcessor::getNumTrackDecks() const
{
    int n = 0;
    for (auto& ch : channels)
        if (ch != nullptr && ch->type() == ChannelType::trackDeck) ++n;
    return n;
}

std::vector<MidiCapture::CapturedNote> MainPluginProcessor::getCapturedMidiNotes (double bpm) const
{
    const double useBpm = bpm > 1.0 ? bpm : 120.0;
    return midiCapture.takeNotes (currentSampleRate, useBpm,
                                  recordingSampleCounter.load (std::memory_order_relaxed));
}

juce::String MainPluginProcessor::getChannelName (int index) const
{
    if (juce::isPositiveAndBelow (index, (int) channels.size()))
        if (auto& ch = channels[(size_t) index])
            return ch->name;
    return {};
}

void MainPluginProcessor::cacheDeckParamIds()
{
    for (int i = 0; i < kMaxChannels; ++i)
    {
        const auto d = deckPrefix (i);
        auto& p = deckIds[(size_t) i];
        p.bpm = d + "Bpm";  p.beatOffset = d + "BeatOffset";  p.downbeat = d + "Downbeat";
        p.tempoInterp = d + "TempoInterpretation";  p.syncMode = d + "SyncMode";
        p.keyTarget = d + "KeyTarget";  p.pitch = d + "Pitch";
        p.play = d + "Play";  p.liveKey = d + "LiveKey";  p.keyLock = d + "KeyLock";
        p.volume = d + "Volume";  p.fader = d + "Fader";
        p.low = d + "Low";  p.mid = d + "Mid";  p.high = d + "High";  p.filter = d + "Filter";
        p.revMix = d + "RevMix";  p.echoMix = d + "EchoMix";
        p.displayName = deckNameFor (i);
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto s = d + "Stem" + DeckPlayer::stemLaneId (lane);
            p.stemGain [(size_t) lane] = s + "Gain";
            p.stemPitch[(size_t) lane] = s + "Pitch";
            p.stemFilt [(size_t) lane] = s + "Filt";
            p.stemSend [(size_t) lane] = s + "Send";
        }
    }

    // Pad gains: resolve the atomics once so processBlock never builds a String.
    for (int i = 0; i < PadBank::numPads; ++i)
        padGainParams[(size_t) i] = apvts.getRawParameterValue ("pad" + juce::String (i + 1) + "Gain");
}

void MainPluginProcessor::buildMidiTargets()
{
    std::vector<MidiMappingManager::CcTarget> cc;
    auto addCc = [&] (const juce::String& name, const juce::String& paramId)
    {
        cc.push_back ({ name, apvts.getParameter (paramId) });
    };

    addCc ("Crossfader",    "crossfader");
    addCc ("Master BPM",    "masterBpm");
    addCc ("Master Volume", "master");
    for (int i = 0; i < numDecks; ++i)
    {
        const auto label = deckLabel (i);
        addCc (label + " Volume", deckPrefix (i) + "Volume");
        addCc (label + " Filter", deckPrefix (i) + "Filter");
        addCc (label + " Low",    deckPrefix (i) + "Low");
        addCc (label + " High",   deckPrefix (i) + "High");
        // Per-stem faders (drums/bass/music/vocals) so an MPK-style controller can
        // ride individual stems.
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
            addCc (label + " " + DeckPlayer::stemLaneLabel (lane) + " Stem",
                   deckPrefix (i) + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain");
    }

    std::vector<MidiMappingManager::NoteTarget> notes;
    for (int i = 0; i < numDecks; ++i)
    {
        notes.push_back ({ deckLabel (i) + " Play", i, MidiMappingManager::ActionPlay });
        notes.push_back ({ deckLabel (i) + " Cue",  i, MidiMappingManager::ActionCue  });
    }
    for (int pad = 0; pad < PadBank::numPads; ++pad)   // pad triggers -> hardware pads
        notes.push_back ({ "Pad " + juce::String (pad + 1), pad, MidiMappingManager::ActionPad });

    midiMapping.setTargets (std::move (cc), std::move (notes));
}

MainPluginProcessor::~MainPluginProcessor()
{
    // Clear the alive flag BEFORE draining pools: any job that already posted its
    // completion closure will see false and skip touching this (soon-freed) processor.
    aliveFlag->store (false, std::memory_order_release);
    neuralBeat.cancelPending();
    // The provider can enqueue decoded-stem jobs on loadPool. Stop that producer
    // first so the analysis pool has a closed, finite set of work to drain.
    stemProvider.shutdown();
    // Neural inference cannot be destroyed underneath Ort::Session::Run. The old
    // 1.5 s timeout allowed member teardown to continue while a full-track job was
    // still inside ONNX, producing a repeatable RunSince use-after-free. Jobs now
    // poll aliveFlag between chunks, and teardown waits for the current chunk to
    // return. Destroying the pool here (rather than at automatic member teardown)
    // guarantees its worker threads are gone while both neural engines still exist.
    if (loadPool != nullptr)
    {
        loadPool->removeAllJobs (true, -1);
        loadPool.reset();
    }

    // Destroy the engine BEFORE releasing the claim, so a next instance constructing
    // immediately after can never overlap with this one's teardown.
    teWrapper.reset();
    if (ownsTeEngine)
    {
        ownsTeEngine = false;
        releaseTeEngine();
    }
}

// Tracktion Engine is one-per-process: constructing a second while the first is alive
// DEADLOCKS, so a DAW opening two StemDecks froze on the second. This claim makes the
// engine exclusive — the first instance gets it, later ones run without it.
//
// That degradation is safe by construction: every teWrapper use in this file is
// null-guarded, so a second instance keeps decks, mixer, stems and FX and only loses
// the Tracktion-backed arrangement transport. Losing a feature beats hanging the host.
//
// The claim is released on destruction, so closing the first instance hands the engine
// to the next one that asks (the sequential case, which already worked).
static std::atomic<bool> gTeEngineClaimed { false };

bool MainPluginProcessor::claimTeEngine() noexcept
{
    bool expected = false;
    return gTeEngineClaimed.compare_exchange_strong (expected, true, std::memory_order_acq_rel);
}

void MainPluginProcessor::releaseTeEngine() noexcept
{
    gTeEngineClaimed.store (false, std::memory_order_release);
}

void MainPluginProcessor::prepareTeWrapper()
{
    if (teWrapper == nullptr || currentSampleRate <= 0.0 || preparedBlockSize <= 0)
        return;
    tracktion::HostedAudioDeviceInterface::Parameters teParams;
    teParams.sampleRate     = currentSampleRate;
    teParams.blockSize      = preparedBlockSize;
    teParams.inputChannels  = getTotalNumInputChannels();
    teParams.outputChannels = juce::jmax (2, getTotalNumOutputChannels());
    teWrapper->audioInterface.initialise (teParams);
    teWrapper->audioInterface.prepareToPlay (currentSampleRate, preparedBlockSize);
}

void MainPluginProcessor::ensureTeWrapperOnMessageThread()
{
    if (teWrapper != nullptr || teEngineUnavailable)
        return;

    if (! ownsTeEngine)
    {
        if (! claimTeEngine())
        {
            // Another instance already owns the engine. Do NOT construct a second.
            teEngineUnavailable = true;
            return;
        }
        ownsTeEngine = true;
    }

    auto create = [this]
    {
        if (teWrapper == nullptr)
        {
            teWrapper = std::make_unique<TeWrapper>();
            prepareTeWrapper();   // created after prepareToPlay — prepare it now
        }
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        create();
    }
    else
    {
        jassert (! juce::MessageManager::getInstance()->currentThreadHasLockedMessageManager());
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&] { create(); done.signal(); });
        done.wait (-1);
    }
}


// ── APVTS <-> event mapping (A1 phase 3) ─────────────────────────────────────
// One table, used in BOTH directions, so a parameter and its event can never drift
// apart: mapApvtsId turns a parameter change into an event, apvtsIdFor turns an
// event back into the parameter to write. A mapping present in one direction only
// would make a control capture but not replay, or replay but not capture.
namespace
{
struct DeckSuffixMap { const char* suffix; stemdeck::events::ParamId id; };
const DeckSuffixMap kDeckSuffixes[] = {
    { "Volume",              stemdeck::events::ParamId::channelGain },
    { "Low",                 stemdeck::events::ParamId::eqLow },
    { "Mid",                 stemdeck::events::ParamId::eqMid },
    { "High",                stemdeck::events::ParamId::eqHigh },
    { "Filter",              stemdeck::events::ParamId::filter },
    { "Pitch",               stemdeck::events::ParamId::pitchSemitones },
    { "KeyLock",             stemdeck::events::ParamId::keyLockOn },
    { "KeyTarget",           stemdeck::events::ParamId::keyTarget },
    { "Bpm",                 stemdeck::events::ParamId::bpmSource },
    { "TempoInterpretation", stemdeck::events::ParamId::tempoInterp },
    { "SyncMode",            stemdeck::events::ParamId::syncMode },
};

struct GlobalMap { const char* id; stemdeck::events::ParamId pid; };
const GlobalMap kGlobals[] = {
    { "master",         stemdeck::events::ParamId::masterGain },
    { "limiterCeiling", stemdeck::events::ParamId::limiterCeiling },
    { "masterKey",      stemdeck::events::ParamId::masterKey },
    { "crossfader",      stemdeck::events::ParamId::crossfader },
    { "crossfaderCurve", stemdeck::events::ParamId::crossfaderCurve },
};
} // namespace

MainPluginProcessor::ParamMapping MainPluginProcessor::mapApvtsId (const juce::String& id) const
{
    using namespace stemdeck::events;
    ParamMapping m;

    for (const auto& g : kGlobals)
        if (id == g.id)
        {
            m.valid = true; m.id = g.pid; m.scope.kind = ScopeKind::master;
            return m;
        }

    if (! id.startsWith ("deck") || id.length() < 5)
        return m;

    const int deck = id[4] - 'A';
    if (! juce::isPositiveAndBelow (deck, numDecks))
        return m;

    const auto rest = id.substring (5);

    // Per-stem gain: "Stem<lane>Gain" -> stem scope.
    if (rest.startsWith ("Stem") && rest.endsWith ("Gain"))
    {
        const auto laneId = rest.substring (4, rest.length() - 4);
        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
            if (laneId.equalsIgnoreCase (DeckPlayer::stemLaneId (lane)))
            {
                m.valid = true;
                m.id = ParamId::gainStem;
                m.scope.kind     = ScopeKind::stem;
                m.scope.channel =
                    channelIdBySlot[(size_t) deck].load (
                        std::memory_order_acquire);
                m.scope.stemLane = (StemLane) lane;
                return m;
            }
        return m;   // full-mix trim is not a stem lane
    }

    for (const auto& d : kDeckSuffixes)
        if (rest == d.suffix)
        {
            m.valid = true; m.id = d.id;
            m.scope.kind = ScopeKind::channel;
            m.scope.channel =
                channelIdBySlot[(size_t) deck].load (
                    std::memory_order_acquire);
            return m;
        }
    return m;
}

juce::String MainPluginProcessor::apvtsIdFor (const stemdeck::events::Event& e) const
{
    using namespace stemdeck::events;
    if (e.type != EventType::param) return {};

    for (const auto& g : kGlobals)
        if (e.param.id == g.pid) return g.id;

    const int deck = channelSlotForId ((ChannelID) e.scope.channel);
    if (! juce::isPositiveAndBelow (deck, numDecks)) return {};
    const auto pfx = deckPrefix (deck);

    if (e.param.id == ParamId::gainStem)
    {
        const int lane = (int) e.scope.stemLane;
        if (lane >= 1 && lane < DeckPlayer::numStemLanes)
            return pfx + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain";
        return {};
    }

    for (const auto& d : kDeckSuffixes)
        if (e.param.id == d.id) return pfx + d.suffix;
    return {};
}

void MainPluginProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Re-entrancy guard: applyEvent writes the parameter, which notifies us straight
    // back. Without this, replaying one event would dispatch a new one forever.
    if (applyingEvent.load (std::memory_order_acquire))
        return;

    const auto m = mapApvtsId (parameterID);
    if (! m.valid || eventDispatcher == nullptr)
        return;

    eventDispatcher->dispatchParam (m.scope, m.id, (double) newValue);
}


// ── Dispatch helpers for body-backed controls (A1 phase 3) ───────────────────
// The editor calls these instead of reaching into a body's setter. Every one of them
// goes through the dispatcher, so the gesture is captured and replays identically.
namespace
{
stemdeck::events::Scope channelScope (ChannelID id)
{
    stemdeck::events::Scope sc;
    sc.kind    = stemdeck::events::ScopeKind::channel;
    sc.channel = id;
    return sc;
}
} // namespace

void MainPluginProcessor::setInputGainDispatched (int c, float linear)
{ eventDispatcher->dispatchParam (channelScope (channelIdForIndex (c)), stemdeck::events::ParamId::inputGain, (double) linear); }

void MainPluginProcessor::setInputMonitorDispatched (int c, bool on)
{ eventDispatcher->dispatchParam (channelScope (channelIdForIndex (c)), stemdeck::events::ParamId::inputMonitor, on ? 1.0 : 0.0); }

void MainPluginProcessor::setInputSourceDispatched (int c, int deviceChannel)
{ eventDispatcher->dispatchParam (channelScope (channelIdForIndex (c)), stemdeck::events::ParamId::inputSource, (double) deviceChannel); }

void MainPluginProcessor::setInstGainDispatched (int c, float linear)
{ eventDispatcher->dispatchParam (channelScope (channelIdForIndex (c)), stemdeck::events::ParamId::instGain, (double) linear); }

void MainPluginProcessor::setInstMidiChannelDispatched (int c, int midiChannel)
{ eventDispatcher->dispatchParam (channelScope (channelIdForIndex (c)), stemdeck::events::ParamId::instMidiChannel, (double) midiChannel); }

namespace
{
stemdeck::events::Scope stemScope (ChannelID id, int stemIndex)
{
    stemdeck::events::Scope sc;
    sc.kind     = stemdeck::events::ScopeKind::stem;
    sc.channel  = id;
    sc.stemLane = (stemdeck::events::StemLane) (DeckPlayer::drumsStem + stemIndex);
    return sc;
}
} // namespace

void MainPluginProcessor::setStemMutedDispatched (int deckIndex, int stemIndex, bool muted)
{
    eventDispatcher->dispatchParam (stemScope (channelIdForSlot (deckIndex), stemIndex),
                                    stemdeck::events::ParamId::stemMute,
                                    muted ? 1.0 : 0.0);
}

void MainPluginProcessor::setStemSoloedDispatched (int deckIndex, int stemIndex, bool soloed)
{
    eventDispatcher->dispatchParam (stemScope (channelIdForSlot (deckIndex), stemIndex),
                                    stemdeck::events::ParamId::stemSolo,
                                    soloed ? 1.0 : 0.0);
}

bool MainPluginProcessor::isStemMuted (int deckIndex, int stemIndex) const
{
    const int slot = juce::isPositiveAndBelow (
                         deckIndex, (int) channels.size())
                         && channels[(size_t) deckIndex] != nullptr
                         ? channels[(size_t) deckIndex]->slotIndex : -1;
    return juce::isPositiveAndBelow (slot, numDecks)
        && juce::isPositiveAndBelow (stemIndex, HostGraph::kNumStemsPerDeck)
        && stemMuted[(size_t) slot][(size_t) stemIndex];
}

bool MainPluginProcessor::isStemSoloed (int deckIndex, int stemIndex) const
{
    const int slot = juce::isPositiveAndBelow (
                         deckIndex, (int) channels.size())
                         && channels[(size_t) deckIndex] != nullptr
                         ? channels[(size_t) deckIndex]->slotIndex : -1;
    return juce::isPositiveAndBelow (slot, numDecks)
        && juce::isPositiveAndBelow (stemIndex, HostGraph::kNumStemsPerDeck)
        && stemSoloLane[(size_t) slot] == stemIndex;
}

void MainPluginProcessor::setInsertBypassDispatched (int chainIndex, int insertPos, bool bypassed)
{
    stemdeck::events::Scope sc;
    sc.kind    = stemdeck::events::ScopeKind::insert;
    sc.channel = chainIndex;
    eventDispatcher->dispatchParam (sc, stemdeck::events::ParamId::insertBypass,
                                    bypassed ? 1.0 : 0.0, stemdeck::events::GestureFlag::none,
                                    (std::uint32_t) insertPos);
}

void MainPluginProcessor::setInsertParameterDispatched (
    int chainIndex, int insertPos, int fallbackParameterIndex,
    std::uint64_t stableParameterId, float normalizedValue)
{
    stemdeck::events::Scope scope;
    scope.kind = stemdeck::events::ScopeKind::insert;
    scope.channel = chainIndex;
    const auto packed = ((std::uint32_t) juce::jlimit (0, 0xffff, insertPos) << 16)
                      | (std::uint32_t) juce::jlimit (
                            0, 0xffff, fallbackParameterIndex);
    eventDispatcher->dispatchParam (
        scope, stemdeck::events::ParamId::insertParam,
        (double) juce::jlimit (0.0f, 1.0f, normalizedValue),
        stemdeck::events::GestureFlag::none, packed, stableParameterId);
}

void MainPluginProcessor::pollHostedPluginParameterChanges()
{
    for (const auto& snapshot : hostGraph.getParameterSnapshots())
    {
        const auto key = std::make_tuple (
            snapshot.chainIndex, snapshot.insertIndex,
            snapshot.stableParameterId);
        auto [it, inserted] = hostedParameterValues.emplace (
            key, snapshot.normalizedValue);
        if (inserted
            || std::abs (it->second - snapshot.normalizedValue) < 1.0e-6f)
            continue;
        it->second = snapshot.normalizedValue;
        setInsertParameterDispatched (
            snapshot.chainIndex, snapshot.insertIndex,
            snapshot.fallbackParameterIndex, snapshot.stableParameterId,
            snapshot.normalizedValue);
    }
}

juce::String MainPluginProcessor::deckPrefix (int deckIndex)
{
    return "deck" + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, kMaxChannels - 1, deckIndex)));
}

juce::String MainPluginProcessor::deckLabel (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, kMaxChannels - 1, deckIndex)));
}

juce::StringArray MainPluginProcessor::keyTargetChoices()
{
    juce::StringArray choices { "Off" };
    for (int pc = 0; pc < 12; ++pc)
        choices.add (TrackAnalyzer::pitchClassName (pc));
    return choices;
}

int MainPluginProcessor::keyTargetChoiceToPitchClass (int choiceIndex)
{
    return choiceIndex > 0 ? juce::jlimit (0, 11, choiceIndex - 1) : -1;
}

juce::AudioProcessorValueTreeState::ParameterLayout MainPluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // One parameter bank per channel slot. Banks 0-3 keep the historical deckA-D
    // ids, so existing host state and saved sessions load unchanged.
    for (int i = 0; i < kMaxChannels; ++i)
    {
        const auto deck = deckPrefix (i);
        const auto label = deckLabel (i);
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (deck + "Play", 1), label + " Play", false));
        params.push_back (floatParam (deck + "Bpm", label + " Source BPM", 40.0f, 240.0f, 120.0f, " BPM"));
        params.push_back (choiceParam (deck + "TempoInterpretation", label + " Tempo Interpretation", BeatSyncEngine::tempoInterpretationChoices(), 0));
        params.push_back (choiceParam (deck + "SyncMode", label + " Sync Mode", BeatSyncEngine::syncModeChoices(), 0));
        params.push_back (floatParam (deck + "Volume", label + " Gain", -60.0f, 6.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "Fader", label + " Channel Fader", 0.0f, 1.0f, 1.0f));
        params.push_back (floatParam (deck + "Pitch", label + " Pitch Shift", -12.0f, 12.0f, 0.0f, " st"));
        params.push_back (floatParam (deck + "BeatOffset", label + " Beat Grid Offset", -4.0f, 4.0f, 0.0f, " beats"));
        params.push_back (floatParam (deck + "Downbeat", label + " Downbeat Position", -16.0f, 16.0f, 0.0f, " beats"));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (deck + "KeyLock", 1), label + " Pitch Preserve Request", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (deck + "LiveKey", 1), label + " Live Key Analysis", false));
        params.push_back (choiceParam (deck + "KeyTarget", label + " Match Target Key", keyTargetChoices(), 0));
        params.push_back (floatParam (deck + "Low", label + " Low", -24.0f, 12.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "Mid", label + " Mid", -24.0f, 12.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "High", label + " High", -24.0f, 12.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "Filter", label + " Filter", -1.0f, 1.0f, 0.0f));

        // Stem gain is a LINEAR PERCENT (0 = silence, 100 = unity, 200 = x2.0/+6 dB).
        // See StemGain.h — this replaced a dB range in schema v2 and old state is
        // migrated in setStateInformation (0 dB meant unity; 0% means silence).
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            params.push_back (floatParam (deck + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain",
                                          label + " " + DeckPlayer::stemLaneLabel (lane) + " Stem Gain",
                                          stemdeck::stemgain::kMin, stemdeck::stemgain::kMax,
                                          stemdeck::stemgain::kUnity, "%"));

        // Independent per-stem pitch (semitones) — pitch e.g. the vocal acapella while
        // the instrumental stays in key. Only the 4 separated stems, not the full mix.
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
            params.push_back (floatParam (deck + "Stem" + DeckPlayer::stemLaneId (lane) + "Pitch",
                                          label + " " + DeckPlayer::stemLaneLabel (lane) + " Stem Pitch",
                                          -12.0f, 12.0f, 0.0f, " st"));

        // Per-stem FX (real-time separation path): independent filter + space/echo
        // send for each of the four stems. Filt 0.5 = neutral, Send 0 = dry.
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
        {
            const auto id = DeckPlayer::stemLaneId (lane);
            const auto lbl = DeckPlayer::stemLaneLabel (lane);
            params.push_back (floatParam (deck + "Stem" + id + "Filt", label + " " + lbl + " Stem Filter", 0.0f, 1.0f, 0.5f));
            params.push_back (floatParam (deck + "Stem" + id + "Send", label + " " + lbl + " Stem Space/Echo", 0.0f, 1.0f, 0.0f));
        }

        params.push_back (floatParam (deck + "RevMix",  label + " Reverb Mix",   0.0f, 1.0f, 0.0f));
        params.push_back (floatParam (deck + "EchoMix", label + " Echo Mix",     0.0f, 1.0f, 0.0f));
    }

    params.push_back (floatParam ("crossfader", "Crossfader", 0.0f, 1.0f, 0.5f));
    // Crossfader response: 0 Smooth (equal-power, long blends) · 1 Linear (constant-sum) ·
    // 2 Sharp (fast crossover) · 3 Cut (scratch/transform — near-instant near either edge).
    params.push_back (choiceParam ("crossfaderCurve", "Crossfader Curve",
                                   { "Smooth", "Linear", "Sharp", "Cut" }, 0));
    // Hamster / reverse: swaps the fader sense so A/C and B/D trade ends.
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("crossfaderReverse", 1), "Crossfader Reverse", false));
    params.push_back (floatParam ("masterBpm", "Global Master BPM", 40.0f, 240.0f, 120.0f, " BPM"));
    params.push_back (floatParam ("master", "Master", -60.0f, 6.0f, 0.0f, "dB"));
    params.push_back (floatParam ("limiterCeiling", "Limiter Ceiling", -12.0f, -0.1f, -1.0f, "dB"));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("hostSync", 1), "Host Tempo Sync", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("beatSync", 1), "Host Beat/Grid Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("micMonitor", 1), "Mic/Input Monitor", false));
    params.push_back (floatParam ("micGain", "Mic/Input Gain", -60.0f, 6.0f, -12.0f, "dB"));
    // Built-in live-MIDI instrument: play a connected controller straight into the mix.
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("liveInstrument", 1), "Live MIDI Instrument", false));
    params.push_back (floatParam ("liveInstrumentGain", "Live Instrument Gain", -60.0f, 6.0f, -6.0f, "dB"));
    // Global Master Effects XY pad: X = bipolar filter, Y = space/echo send.
    params.push_back (floatParam ("masterFxX", "Master FX Filter", 0.0f, 1.0f, 0.5f));
    params.push_back (floatParam ("masterFxY", "Master FX Space/Echo", 0.0f, 1.0f, 0.0f));
    // Smart mastering assistant: auto EQ tilt + bus dynamics + LUFS targeting.
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("smartMasterOn", 1), "AI Master", false));
    params.push_back (choiceParam ("smartMasterMode", "AI Master Mode",
                                   { "Streaming -14", "Club -9", "Warm -16" }, 0));
    params.push_back (floatParam ("smartMasterAmount", "AI Master Amount", 0.0f, 1.0f, 0.8f));
    params.push_back (choiceParam ("masterKey", "Master Key Target", keyTargetChoices(), 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("stemLink", 1), "Link Stem Gains Across Decks", false));

    for (int i = 0; i < PadBank::numPads; ++i)
        params.push_back (floatParam ("pad" + juce::String (i + 1) + "Gain", "Pad " + juce::String (i + 1) + " Gain", -60.0f, 6.0f, 0.0f, "dB"));

    return { params.begin(), params.end() };
}

void MainPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    limiterEnvelope = 1.0f;
    const int outputChannels = juce::jmax (1, getTotalNumOutputChannels());
    // Prepare every channel (frame scratch + body: DeckPlayer, RTSS, stem FX, DeckFX,
    // warp), then republish so the audio thread picks up the prepared snapshot.
    for (auto& ch : channels)
        if (ch != nullptr)
            ch->prepare (sampleRate, samplesPerBlock, outputChannels);
    publishChannels();
    refreshLatencyCompensation();
    padBank.prepare (sampleRate);
    masterRecorder.prepare (sampleRate, outputChannels, samplesPerBlock);
    micRecorder.prepare (sampleRate, getTotalNumInputChannels() > 0 ? getTotalNumInputChannels() : outputChannels, samplesPerBlock);
    for (auto& recorder : deckSourceRecorders)
        recorder.prepare (sampleRate, outputChannels, samplesPerBlock);
    for (auto& recorder : deckPrintedRecorders)
        recorder.prepare (sampleRate, outputChannels, samplesPerBlock);
    micScratch.setSize (juce::jmax (outputChannels, getTotalNumInputChannels()), samplesPerBlock);
    teScratch.setSize (juce::jmax (2, outputChannels), samplesPerBlock);
    // 4096, not 256: a MidiBuffer that runs out of reserved capacity reallocates
    // on whatever thread calls addEvent() next, which for this buffer is the audio
    // thread. 256 bytes is a comfortable margin for a quiet block but not for a
    // dense one (a chord dump, a CC automation burst, an all-notes-off flurry),
    // and a silent reallocation is exactly the kind of RT violation nobody notices
    // until it glitches during a real performance. Paid once here, message thread.
    teMidiScratch.ensureSize (4096);
    warpEngine.prepare (sampleRate, samplesPerBlock, outputChannels);
    hostGraph.prepare (sampleRate, samplesPerBlock, outputChannels);
    masterFx.prepare (sampleRate, samplesPerBlock, outputChannels);
    smartMaster.prepare (sampleRate);
    liveLoudness.prepare (sampleRate, 2);
    arrangementPlayer.prepare (sampleRate, samplesPerBlock);
    midiClipPlayer.prepare (sampleRate, samplesPerBlock);
    metronome.prepare (sampleRate);
    // Record the format so a lazily-created engine can be prepared correctly later;
    // prepare it now only if it already exists.
    preparedBlockSize = samplesPerBlock;
    if (teWrapper != nullptr)
        prepareTeWrapper();
    // (Per-deck scratch / RTSS / stem-FX / DeckFX now live inside each Channel's
    //  TrackDeckBody and are prepared by Channel::prepare above.)
}

bool MainPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::disabled()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* MainPluginProcessor::createEditor()
{
    return new MainPluginEditor (*this);
}

float MainPluginProcessor::getFloatParam (juce::StringRef id) const
{
    if (auto* value = apvts.getRawParameterValue (id))
        return value->load();
    return 0.0f;
}

bool MainPluginProcessor::getBoolParam (juce::StringRef id) const
{
    if (auto* value = apvts.getRawParameterValue (id))
        return value->load() > 0.5f;
    return false;
}

void MainPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto cpuStartTicks = juce::Time::getHighResolutionTicks();
    juce::ScopedNoDenormals noDenormals;

    // ── Hardware MIDI controller mapping ──────────────────────────────────────
    // CC messages are applied to mapped parameters inside processBlock();
    // note triggers are returned for us to dispatch as deck actions.
    {
        const int nTriggers = midiMapping.processBlock (midi);
        for (int t = 0; t < nTriggers; ++t)
        {
            const auto tr = midiMapping.triggered (t);

            if (tr.action == MidiMappingManager::ActionPad)   // tr.deck holds the pad index
            {
                if (juce::isPositiveAndBelow (tr.deck, PadBank::numPads))
                    padBank.triggerPad (tr.deck);             // RT-safe primitive (no logging)
                continue;
            }

            if (! juce::isPositiveAndBelow (tr.deck, numDecks)) continue;

            if (tr.action == MidiMappingManager::ActionPlay)
            {
                // Deferred to the message thread (see deckPlayToggleRequested):
                // deckPrefix(tr.deck) + "Play" heap-allocates a juce::String, and
                // setValueNotifyingHost synchronously runs every listener under a
                // blocking CriticalSection — both illegal on the audio thread, and
                // this fires at exactly a track-start transition.
                deckPlayToggleRequested[(size_t) tr.deck].store (true, std::memory_order_relaxed);
            }
            else if (tr.action == MidiMappingManager::ActionCue)
            {
                deckAt (tr.deck).seekToSamples (0.0);   // RT-safe queued seek
            }
        }
    }

    // Mic capture reuses the pre-allocated micScratch (no RT alloc). micCaptured
    // marks whether it holds this block's input (replaces the old getNumSamples check).
    juce::AudioBuffer<float>& micInput = micScratch;
    bool micCaptured = false;
    const bool isCapturingPerformance = masterRecorder.isRecording();
    // Capture the device input when monitoring, recording, OR when an Input channel
    // exists — an input channel needs live audio regardless of the legacy mic toggle.
    if ((getBoolParam ("micMonitor") || isCapturingPerformance
         || hasInputChannels.load (std::memory_order_relaxed)) && getTotalNumInputChannels() > 0)
    {
        micInput.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            micInput.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());
        micCaptured = true;
    }

    buffer.clear();

    auto syncSnapshot = sync.update (getPlayHead());
    const float masterBpm = juce::jlimit (40.0f, 240.0f, getFloatParam ("masterBpm"));
    bool anyDeckPlaying = false;
    for (int i = 0; i < numDecks; ++i)
        anyDeckPlaying = anyDeckPlaying || getBoolParam (deckIds[(size_t) i].play);   // cached id — no RT alloc
    if (! syncSnapshot.hostPlaying)
    {
        syncSnapshot.bpm = masterBpm;
        syncSnapshot.hostPlaying = anyDeckPlaying;
        syncSnapshot.ppqPosition = currentSampleRate > 0.0 ? ((double) transportSampleCounter / currentSampleRate) * (double) masterBpm / 60.0 : 0.0;
    }
    const int64_t blockRecordingStart = recordingSampleCounter.load (std::memory_order_relaxed);
    updatePerformanceCaptureState (blockRecordingStart, syncSnapshot);

    const float crossfader = getFloatParam ("crossfader");
    const int   xfCurve    = (int) std::round (getFloatParam ("crossfaderCurve"));
    const bool  xfReverse  = getBoolParam ("crossfaderReverse");
    const auto  xfGains    = crossfaderBusGains (crossfader, xfCurve, xfReverse);
    const float leftBusGain  = xfGains.first;    // A/C bus
    const float rightBusGain = xfGains.second;   // B/D bus

    // ── Channels: render the published snapshot ──────────────────────────────
    // Per channel the frame does: fill params → body->processInto → onPostBody
    // (per-channel FX chain / capture / meter) → crossfader-bus sum. The snapshot is
    // an immutable atomic load, so add/remove on the message thread can never tear
    // this walk (see GraphSnapshot.h + the StemDeckChannelDropout gate).
    currentBlockMidi = &midi;

    BlockContext blockCtx;
    blockCtx.numSamples       = buffer.getNumSamples();
    blockCtx.leftBusGain      = leftBusGain;
    blockCtx.rightBusGain     = rightBusGain;
    blockCtx.blockStartSample = blockRecordingStart;
    blockCtx.transport        = syncSnapshot;
    blockCtx.midi             = &midi;
    // Live device input for Input channels (null when the device has no inputs).
    blockCtx.inputAudio       = micCaptured ? &micInput : nullptr;

    auto channelSnapshot = channelGraph.current();

    // ── Live delay alignment (RT-safe: atomic reads only) ────────────────────
    // Each channel's latency = cached FX-chain latency + its body's CURRENT latency
    // (RubberBand key-lock switches on/off at runtime). Hold every channel back to
    // the slowest so key-locked and un-key-locked decks stay beat-aligned. Live
    // inputs are never held back — delaying a monitored mic makes it unusable.
    {
        int maxLat = 0;
        for (auto& ch : channelSnapshot->channels)
            if (ch != nullptr) maxLat = juce::jmax (maxLat, ch->getTotalLatencySamples());

        for (auto& ch : channelSnapshot->channels)
            if (ch != nullptr)
                ch->setCompensationSamples (ch->type() == ChannelType::input
                                            ? 0 : maxLat - ch->getTotalLatencySamples());
    }

    // Solo is mix-wide: resolve it once here, then every channel just reads the flag.
    for (auto& ch : channelSnapshot->channels)
        if (ch != nullptr && ch->isSoloed()) { blockCtx.anySoloed = true; break; }

    for (int i = 0; i < (int) channelSnapshot->channels.size(); ++i)
    {
        auto& ch = channelSnapshot->channels[(size_t) i];
        if (ch == nullptr)
            continue;

        // Crossfader assignment (0 = A/left bus, 1 = Center, 2 = B/right bus).
        const int slot = ch->slotIndex;
        const int cfSide = juce::isPositiveAndBelow (slot, numDecks)
                               ? getDeckCfSide (slot) : 1;
        ch->cfSide = cfSide == 0 ? CrossfaderSide::ac
                   : cfSide == 2 ? CrossfaderSide::bd
                                 : CrossfaderSide::center;

        auto* body = ch->getBody();
        if (body == nullptr)
            continue;

        if (auto* p = body->trackDeckParams())
        {
            if (! juce::isPositiveAndBelow (slot, kMaxChannels))
                continue;   // beyond the parameter pool

            const auto& ids = deckIds[(size_t) slot];   // pre-concatenated ids — no RT alloc
            p->play        = getBoolParam  (ids.play);
            p->volume      = getFloatParam (ids.volume);
            p->fader       = getFloatParam (ids.fader);
            p->low         = getFloatParam (ids.low);
            p->mid         = getFloatParam (ids.mid);
            p->high        = getFloatParam (ids.high);
            p->filter      = getFloatParam (ids.filter);
            p->pitch       = getFloatParam (ids.pitch);
            p->bpm         = getFloatParam (ids.bpm);
            p->beatOffset  = getFloatParam (ids.beatOffset);
            p->downbeat    = getFloatParam (ids.downbeat);
            p->tempoInterp = (int) std::round (getFloatParam (ids.tempoInterp));
            p->syncMode    = (int) std::round (getFloatParam (ids.syncMode));
            p->keyTarget   = (int) std::round (getFloatParam (ids.keyTarget));
            p->keyLock     = getBoolParam (ids.keyLock);
            p->liveKey     = getBoolParam (ids.liveKey);
            p->revMix      = getFloatParam (ids.revMix);
            p->echoMix     = getFloatParam (ids.echoMix);

            for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            {
                p->stemGainPercent[(size_t) lane] = getFloatParam (ids.stemGain[(size_t) lane]);
                // Pitch / filter / send params exist only for drums..vocals.
                const bool coreLane = lane >= DeckPlayer::drumsStem && lane <= DeckPlayer::vocalsStem;
                p->stemPitch[(size_t) lane] = coreLane ? getFloatParam (ids.stemPitch[(size_t) lane]) : 0.0f;
                p->stemFilt [(size_t) lane] = coreLane ? getFloatParam (ids.stemFilt [(size_t) lane]) : 0.5f;
                p->stemSend [(size_t) lane] = coreLane ? getFloatParam (ids.stemSend [(size_t) lane]) : 0.0f;
            }

            p->hostSync  = getBoolParam ("hostSync");
            p->beatSync  = getBoolParam ("beatSync");
            p->masterKey = (int) std::round (getFloatParam ("masterKey"));
            p->liveKeyPitchClass = liveKeyPitchClass[(size_t) slot].load (std::memory_order_relaxed);
            p->liveKeyConfidence = liveKeyConfidence[(size_t) slot].load (std::memory_order_relaxed);
        }

        ch->processBlock (buffer, blockCtx);
    }

    // Preserve a raw, sample-aligned safety recording before any legacy mic-chain
    // processing. Input channels separately capture their processed signal through
    // their normal per-channel recorder.
    if (micCaptured)
        micRecorder.captureBlock (micInput);

    if (micCaptured)
    {
        const bool routedByInputChannel =
            hasInputChannels.load (std::memory_order_relaxed);
        const float micGain = routedByInputChannel
                                  ? 1.0f
                                  : juce::Decibels::decibelsToGain (
                                        getFloatParam ("micGain"));

        // The global mic chain is retained only for legacy projects with no typed
        // Input channel. Once an Input channel exists it is the sole audible path.
        if (! routedByInputChannel)
            hostGraph.processChain (HostGraph::Chain::mic, micInput, midi);

        // Talkover ducking: measure mic peak, smooth it, duck the deck output
        if (talkoverEnabled.load (std::memory_order_relaxed))
        {
            float micPeak = 0.0f;
            for (int ch = 0; ch < micInput.getNumChannels(); ++ch)
                for (int s = 0; s < micInput.getNumSamples(); ++s)
                    micPeak = juce::jmax (micPeak, std::abs (micInput.getSample (ch, s)));

            micPeak *= micGain;
            const float thresh    = juce::Decibels::decibelsToGain (talkoverThresholdDb.load (std::memory_order_relaxed));
            const float duckGain  = juce::Decibels::decibelsToGain (talkoverDuckDb.load (std::memory_order_relaxed));
            const float target    = micPeak > thresh ? duckGain : 1.0f;
            // Attack 5 ms, release 150 ms at 44.1k (block-rate smoothing)
            const float n = (float) buffer.getNumSamples();
            const float alpha = target < talkoverDuckGain ? (1.0f - std::exp (-n / (0.005f * (float) currentSampleRate)))
                                                           : (1.0f - std::exp (-n / (0.150f * (float) currentSampleRate)));
            talkoverDuckGain += alpha * (target - talkoverDuckGain);
            buffer.applyGain (talkoverDuckGain);
        }

        if (! routedByInputChannel)
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom (ch, 0, micInput,
                                juce::jmin (ch, micInput.getNumChannels() - 1),
                                0, buffer.getNumSamples(), micGain);
    }
    else if (isCapturingPerformance && getTotalNumInputChannels() == 0)
    {
        micInput.setSize (juce::jmax (1, getTotalNumOutputChannels()), buffer.getNumSamples(), false, false, true);
        micInput.clear();
        micCaptured = true;   // capture silence so the mic recorder stays in sync
        micRecorder.captureBlock (micInput);
    }

    std::array<float, PadBank::numPads> padGains {};
    for (int i = 0; i < PadBank::numPads; ++i)
        if (auto* p = padGainParams[(size_t) i])          // cached — no RT allocation
            padGains[(size_t) i] = p->load (std::memory_order_relaxed);
    padBank.process (buffer, 0, buffer.getNumSamples(), padGains);

    // Arrangement + piano-roll MIDI share one transport (the arrangement playhead),
    // mixing into the master bus (subject to master gain + limiter).
    const bool arrPlaying = arrangementPlayer.isPlaying();
    if (arrPlaying)
    {
        const juce::int64 arrPos = arrangementPlayer.getPositionSamples();
        arrangementPlayer.process (buffer);                          // audio clips + advance playhead
        if (! usingTeMidi.load (std::memory_order_relaxed))
            midiClipPlayer.process (buffer, arrPos, buffer.getNumSamples());
        metronome.process (buffer, arrPos, buffer.getNumSamples());  // beat click
        if (arrangementPlayer.getPositionSamples() < arrPos && ! usingTeMidi.load (std::memory_order_relaxed))
            midiClipPlayer.allNotesOff();
    }
    else
    {
        arrangementPlayer.process (buffer);                          // no-op while stopped
        if (arrWasPlaying)
            midiClipPlayer.allNotesOff();
    }
    // When arrangementPlayer auto-stops (reached end), mirror that to TE transport
    if (arrWasPlaying && ! arrPlaying)
    {
        usingTeMidi.store (false, std::memory_order_relaxed);
        if (teWrapper != nullptr)
            teStopRequested.store (true, std::memory_order_relaxed);  // serviced on the message thread
    }
    arrWasPlaying = arrPlaying;

    // Tracktion Engine tracks (arrangement layer — additive to the main mix).
    // Reuses pre-allocated scratch buffers — no per-block heap allocation.
    if (teWrapper != nullptr)
    {
        teWrapper->playheadSync.synchronise (*this);
        teMidiScratch.clear();
        teScratch.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        teScratch.clear();
        teWrapper->audioInterface.processBlock (teScratch, teMidiScratch);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, teScratch, ch, 0, buffer.getNumSamples());
    }

    // Built-in live-MIDI instrument: render a connected controller straight into the
    // mix (before master FX/limiter, so it's mastered with everything else). Works
    // with no plugin loaded. A hosted instrument in a chain also plays live MIDI.
    if (getBoolParam ("liveInstrument"))
        midiClipPlayer.processLive (buffer, midi, buffer.getNumSamples(),
                                    juce::Decibels::decibelsToGain (getFloatParam ("liveInstrumentGain")));
    else
        midiClipPlayer.liveAllNotesOff();

    hostGraph.processChain (HostGraph::Chain::master, buffer, midi);
    masterFx.process (buffer, getFloatParam ("masterFxX"), getFloatParam ("masterFxY"));
    buffer.applyGain (juce::Decibels::decibelsToGain (getFloatParam ("master")));
    // AI mastering assistant sits after user trim, before the safety limiter.
    smartMaster.process (buffer,
                         (int) std::round (getFloatParam ("smartMasterMode")),
                         getBoolParam ("smartMasterOn") ? getFloatParam ("smartMasterAmount") : 0.0f);
    applyLimiter (buffer, getFloatParam ("limiterCeiling"));

    // Master peak meters (block peak with fall-back decay).
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        const float blockPeak = buffer.getMagnitude (ch, 0, buffer.getNumSamples());
        const float previous  = masterPeak[(size_t) ch].load (std::memory_order_relaxed);
        masterPeak[(size_t) ch].store (juce::jmax (blockPeak, previous * 0.86f), std::memory_order_relaxed);
    }

    // Feed the running loudness meter (post-limiter output). RT-safe push; all the
    // BS.1770 analysis happens on the meter's own worker thread.
    liveLoudness.push (buffer);

    masterRecorder.captureBlock (buffer);
    // Live MIDI played during the take (RT-safe ring; converted to beats on read).
    midiCapture.captureBlock (midi, blockRecordingStart);
    if (isCapturingPerformance)
        recordingSampleCounter.fetch_add (buffer.getNumSamples(), std::memory_order_relaxed);
    transportSampleCounter += buffer.getNumSamples();

    // Motorized-fader feedback: emit CC out for mapped faders whose value changed
    // (so hardware/LED controllers track the UI). Change-detection avoids loops.
    midiMapping.writeFeedback (midi, 0);

    const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks() - cpuStartTicks);
    const double budgetSeconds = currentSampleRate > 0.0
                               ? (double) buffer.getNumSamples() / currentSampleRate : 0.0;
    const float instantLoad = budgetSeconds > 0.0
                            ? (float) juce::jlimit (0.0, 4.0, elapsedSeconds / budgetSeconds) : 0.0f;
    const float previousLoad = audioCpuLoad.load (std::memory_order_relaxed);
    audioCpuLoad.store (previousLoad * 0.92f + instantLoad * 0.08f, std::memory_order_relaxed);
    if (budgetSeconds > 0.0 && elapsedSeconds > budgetSeconds)
        audioDeadlineMisses.fetch_add (1, std::memory_order_relaxed);
}

void MainPluginProcessor::applyLimiter (juce::AudioBuffer<float>& buffer, float ceilingDb)
{
    const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    float minEnv = 1.0f;   // deepest gain applied this block → peak gain reduction
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

        const float targetGain = peak > ceiling ? ceiling / (peak + 1.0e-9f) : 1.0f;
        limiterEnvelope = targetGain < limiterEnvelope ? targetGain : limiterEnvelope + 0.002f * (1.0f - limiterEnvelope);
        minEnv = juce::jmin (minEnv, limiterEnvelope);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, juce::jlimit (-0.995f, 0.995f, buffer.getSample (ch, i) * limiterEnvelope));
    }

    // Peak gain reduction in dB (0 = none), smoothed toward 0 so the meter falls back.
    const float grDb = minEnv < 1.0f ? -20.0f * std::log10 (juce::jmax (1.0e-4f, minEnv)) : 0.0f;
    const float prev = limiterGrDb.load (std::memory_order_relaxed);
    limiterGrDb.store (juce::jmax (grDb, prev * 0.80f), std::memory_order_relaxed);
}

juce::String MainPluginProcessor::deckPathProperty (int deckIndex)
{
    return deckPrefix (deckIndex) + "File";
}

juce::String MainPluginProcessor::deckStemPathProperty (int deckIndex, int stemLaneIndex)
{
    if (stemLaneIndex == DeckPlayer::fullMixStem)
        return deckPathProperty (deckIndex);

    return deckPrefix (deckIndex) + "Stem" + DeckPlayer::stemLaneId (stemLaneIndex) + "File";
}

juce::String MainPluginProcessor::padPathProperty (int padIndex)
{
    return "pad" + juce::String (padIndex + 1) + "File";
}

void MainPluginProcessor::setStatePathProperty (const juce::Identifier& property, const juce::String& path)
{
    apvts.state.setProperty (property, path, nullptr);
}

void MainPluginProcessor::applyPremiumStemMix (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return;

    const auto deck = deckPrefix (deckIndex);
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
    {
        const auto paramId = deck + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain";
        if (auto* gain = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (paramId)))
        {
            // Stem gain is LINEAR PERCENT since schema v2 (StemGain.h): solo the four
            // separated stems at unity (100) and silence the full mix (0). Using the
            // old dB target of 0.0 here would set the separated stems to 0% = SILENCE,
            // muting the deck after neural separation.
            const float target = lane == DeckPlayer::fullMixStem ? stemdeck::stemgain::kMin
                                                                 : stemdeck::stemgain::kUnity;
            gain->setValueNotifyingHost (gain->convertTo0to1 (target));
        }
    }
}

// Decode only — no hashing, no analysis. This is the work that has to finish before
// a deck can make sound, and it is the ONLY work that should stand between dropping a
// track and hearing it (§A3).
std::shared_ptr<DeckPlayer::LoadedAudio> MainPluginProcessor::decodeAudioFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (! reader || reader->lengthInSamples <= 0)
        return {};

    auto loaded = std::make_shared<DeckPlayer::LoadedAudio>();
    loaded->logicalLengthSamples = reader->lengthInSamples;
    constexpr double longFileSeconds = 10.0 * 60.0;
    constexpr double previewSeconds  = 2.0 * 60.0;
    const bool useStreaming = reader->lengthInSamples > (juce::int64) (reader->sampleRate * longFileSeconds)
                           || reader->lengthInSamples > std::numeric_limits<int>::max();
    const auto retainedSamples = useStreaming
        ? juce::jmin<juce::int64> (reader->lengthInSamples,
                                   (juce::int64) std::llround (reader->sampleRate * previewSeconds))
        : reader->lengthInSamples;

    loaded->audio.setSize ((int) juce::jmin<int64> (reader->numChannels, 2),
                           (int) retainedSamples);
    reader->read (&loaded->audio, 0, (int) retainedSamples, 0, true, true);
    if (useStreaming)
    {
        loaded->streamed = ChunkedAudioFile::create (file);
        if (loaded->streamed == nullptr)
            return {}; // never publish a truncated preview as if it were the full file
    }
    loaded->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    loaded->path = file.getFullPathName();
    loaded->displayName = file.getFileName();
    return loaded;
}

std::shared_ptr<const DeckPlayer::LoadedAudio> MainPluginProcessor::loadAudioFile (const juce::File& file,
                                                                                     bool runNeuralInline)
{
    auto loaded = decodeAudioFile (file);
    if (loaded == nullptr)
        return {};
    analyseInto (loaded->analysis, loaded->audio, loaded->sourceSampleRate, file, runNeuralInline);
    return loaded;
}

// Fills a standalone TrackAnalysis. Deliberately NOT a method on LoadedAudio: once
// audio is published to a deck the audio thread reads it, so analysing "into" the
// published object would be a data race. The result is handed over separately via
// DeckPlayer::setRefinedAnalysis, which is an atomic shared_ptr swap.
void MainPluginProcessor::analyseInto (TrackAnalysis& out, const juce::AudioBuffer<float>& audio,
                                       double sampleRate, const juce::File& file, bool runNeuralInline)
{
    // ── Persistent analysis cache (build-order #1) ─────────────────────────────
    // Loading runs three full-track analyses (~10 s for 5 min). Most loads in a
    // studio workflow are RE-loads, so a content-hash hit skips all of it. On a
    // miss we run everything, then store the result for next time.
    const juce::String contentHash = AnalysisCache::hashFile (file);
    if (analysisCache.lookup (contentHash, out))
        return;   // cache hit — DSP + neural analysis already done previously

    out = TrackAnalyzer::analyze (audio, sampleRate);

    // ── §A3 LOAD LATENCY ─────────────────────────────────────────────────────
    // The deck is PLAYABLE now, on the DSP analysis. The neural models cost seconds
    // on a full track, so running them here would block the load. Callers that want
    // a playable deck immediately pass runNeuralInline=false and call
    // refineAnalysisAsync() to hot-swap the refined grid/key in later.
    if (! runNeuralInline)
        return;

    // Trained beat/downbeat model (beat_this) on top of the DSP grid. When the
    // model is installed its grid wins; without it the Ellis DP tracker stands.
    // Background thread — same additive, fallback-safe pattern as the key model.
    if (neuralBeat.isAvailable())
    {
        const auto b = neuralBeat.detect (audio, sampleRate,
                                          [alive = aliveFlag]
                                          { return ! alive->load (std::memory_order_acquire); });
        if (b.valid && b.confidence >= kNeuralBeatConfidenceFloor
            && b.beatGridSamples.size() >= 4)
        {
            out.beatSource = TrackAnalysis::Source::neural;
            out.beatGridSamples = b.beatGridSamples;
            out.downbeatSample  = b.downbeatSample;
            out.beatsPerBar     = b.beatsPerBar;
            if (b.detectedBpm > 1.0f)
                out.detectedBpm = b.detectedBpm;
        }
    }

    // Trained key model on top of the DSP result: when it's installed and clears
    // the confidence floor it wins, otherwise TrackAnalyzer's key stands. The
    // "58% exact vs 45%" figure sometimes cited for this model is a shared-core
    // offline eval (core/tools/key_dataset_eval.cpp on GiantSteps+GTZAN) — see
    // NeuralKeyEngine.h — not a measurement of this app. Background thread.
    if (neuralKey.isAvailable())
    {
        const auto k = neuralKey.detect (audio, sampleRate,
                                         [alive = aliveFlag]
                                         { return ! alive->load (std::memory_order_acquire); });

        // CONFIDENCE GATE (§A3). "valid" only means the model ran — it says nothing
        // about whether the answer is any good. Below the floor we KEEP the DSP
        // result rather than let a low-confidence guess overwrite a measured one.
        // The floor is deliberately conservative: the shared-core eval found the
        // model beats profile correlation (see NeuralKeyEngine.h for the caveat on
        // that number), so the model only earns the override when it is actually
        // sure — not merely "sure enough to have beaten profile correlation on a
        // different dataset through a different code path."
        if (k.valid && k.confidence >= kNeuralKeyConfidenceFloor)
        {
            out.keyPitchClass = k.root;
            out.keyIsMinor    = k.minor;
            out.keyConfidence = k.confidence;
            out.keySource     = TrackAnalysis::Source::neural;
        }
        else if (k.valid)
        {
            // Model ran but wasn't confident: DSP stands, and the UI says so.
            out.keySource        = TrackAnalysis::Source::dsp;
            out.keyLowConfidence = true;
        }
    }

    // Whatever won, flag a weak result so the readout can show it honestly rather
    // than presenting a guess as a fact (§B3 confidence flag).
    if (out.keyConfidence < kNeuralKeyConfidenceFloor)
        out.keyLowConfidence = true;

    // The first-pass DSP analysis the deck becomes playable on is ALWAYS version 1.
    // Refinements are 2+, so a trackLoad event recording version 1 pins this exact
    // result no matter what lands later.
    out.analysisVersion = 1;
    analysisCache.store (contentHash, out);
}

void MainPluginProcessor::loadAudioAsync (const juce::File& file, std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> onLoaded)
{
    if (! file.existsAsFile())
        return;

    // Guard the completion callback so a job finishing during teardown can't run the
    // (this-capturing) closure against a freed processor. See aliveFlag in the header.
    auto guarded = [alive = aliveFlag, cb = std::move (onLoaded)] (std::shared_ptr<const DeckPlayer::LoadedAudio> audio) mutable
    {
        if (alive->load (std::memory_order_acquire) && cb)
            cb (std::move (audio));
    };

    loadPool->addJob (new AudioLoadJob (file,
        [this] (const juce::File& f) { return loadAudioFile (f, /*runNeuralInline*/ false); },
        std::move (guarded)), true);
}

void MainPluginProcessor::loadDeckFileAsync (int deckIndex, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks) || ! file.existsAsFile())
        return;

    // A new full-mix source starts a new stem set. Cancel in-flight stem-lane
    // loads and detach already-loaded stems now; otherwise an old vocal/drum lane
    // can remain audible on, or finish loading into, the replacement track.
    for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
    {
        deckLoadGeneration[(size_t) deckIndex][(size_t) lane]
            .fetch_add (1, std::memory_order_relaxed);
        deckAt (deckIndex).setStemAudio (lane, {});
        setStatePathProperty (deckStemPathProperty (deckIndex, lane), {});
    }
    loadDeckStemFileAsync (deckIndex, DeckPlayer::fullMixStem, file);
}

void MainPluginProcessor::clearDeck (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return;
    for (auto& lane : deckLoadGeneration[(size_t) deckIndex])
        lane.fetch_add (1, std::memory_order_relaxed);
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        deckAt (deckIndex).setStemAudio (lane, {});
    deckAt (deckIndex).reset();
    setStatePathProperty (deckPathProperty (deckIndex), {});
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        setStatePathProperty (deckStemPathProperty (deckIndex, lane), {});
    logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed),
                         "track_cleared", deckNameFor (deckIndex), "full_mix", "cleared");
}

void MainPluginProcessor::requestDeckAiStemsAsync (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return;

    const auto path = getDeckPath (deckIndex);
    if (path.isEmpty())
        return;

    const juce::File source (path);
    const auto sourceGeneration =
        deckLoadGeneration[(size_t) deckIndex][(size_t) DeckPlayer::fullMixStem]
            .load (std::memory_order_relaxed);
    // Start separating from where the deck is playing so isolated stems near the
    // playhead become audible within a segment or two (progressive, "instant-feel"),
    // instead of waiting for the whole track. setStemAudio is a lock-free atomic
    // swap, so publishing partial in-memory stems from the worker thread is RT-safe.
    const juce::int64 playhead = (juce::int64) deckAt (deckIndex).getPlayheadReferenceSamples();

    stemProvider.requestStemsStreamingAsync (source, playhead,
        [this, deckIndex, sourceGeneration, alive = aliveFlag]
        (int lane, std::shared_ptr<const DeckPlayer::LoadedAudio> partial)
        {
            if (! alive->load (std::memory_order_acquire)) return;   // processor may be gone (worker thread)
            if (deckLoadGeneration[(size_t) deckIndex][(size_t) DeckPlayer::fullMixStem]
                    .load (std::memory_order_relaxed) != sourceGeneration)
                return;                                             // deck was replaced while splitting
            if (partial && juce::isPositiveAndBelow (deckIndex, numDecks))
                deckAt (deckIndex).setStemAudio (lane, std::move (partial));
        },
        [this, deckIndex, sourceGeneration, alive = aliveFlag]
        (StemProvider::Result result)
        {
            if (! alive->load (std::memory_order_acquire)) return;   // late completion after teardown
            if (deckLoadGeneration[(size_t) deckIndex][(size_t) DeckPlayer::fullMixStem]
                    .load (std::memory_order_relaxed) != sourceGeneration)
                return;                                             // stale separation result
            logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed),
                                 "ai_stem_split", deckNameFor (deckIndex), "premium_streaming_cache",
                                 result.allCoreStemsReady ? "ready" : "failed_or_pending",
                                 result.cacheDirectory.getFullPathName(), result.message);

            // Reload from the freshly written WAV cache for the final, full-quality
            // buffers (and to pick up any CLI-fallback result if neural was unavailable).
            for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
                if (result.stems[(size_t) lane].ready)
                    loadDeckStemFileAsync (deckIndex, lane, result.stems[(size_t) lane].file);

            if (result.allCoreStemsReady)
                applyPremiumStemMix (deckIndex);
        });
}

// §A3: the neural refinement pass. Runs on the load pool AFTER the deck is already
// playable, then publishes the refined analysis atomically. Confidence gating is the
// same as the inline path — a low-confidence model result must not overwrite the
// measured DSP answer just because it exists.
void MainPluginProcessor::refineAnalysisAsync (int deckIndex, const juce::File& file)
{
    if (! file.existsAsFile() || (! neuralBeat.isAvailable() && ! neuralKey.isAvailable()))
        return;

    loadPool->addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
        [this, deckIndex, file, alive = aliveFlag]() -> juce::ThreadPoolJob::JobStatus
    {
        if (! alive->load (std::memory_order_acquire))
            return juce::ThreadPoolJob::jobHasFinished;

        auto audio = deckAt (deckIndex).getLoadedAudio();
        if (audio == nullptr || audio->path != file.getFullPathName())
            return juce::ThreadPoolJob::jobHasFinished;   // deck moved on; drop the result

        auto refined = std::make_shared<TrackAnalysis> (audio->analysis);

        if (neuralBeat.isAvailable())
        {
            const auto b = neuralBeat.detect (audio->audio, audio->sourceSampleRate,
                                              [alive]
                                              { return ! alive->load (std::memory_order_acquire); });
            if (b.valid && b.confidence >= kNeuralBeatConfidenceFloor
                && b.beatGridSamples.size() >= 4)
            {
                refined->beatGridSamples = b.beatGridSamples;
                refined->downbeatSample  = b.downbeatSample;
                refined->beatsPerBar     = b.beatsPerBar;
                if (b.detectedBpm > 1.0f) refined->detectedBpm = b.detectedBpm;
                refined->beatSource = TrackAnalysis::Source::neural;
            }
        }

        if (neuralKey.isAvailable())
        {
            const auto k = neuralKey.detect (audio->audio, audio->sourceSampleRate,
                                             [alive]
                                             { return ! alive->load (std::memory_order_acquire); });
            if (k.valid && k.confidence >= kNeuralKeyConfidenceFloor)
            {
                refined->keyPitchClass    = k.root;
                refined->keyIsMinor       = k.minor;
                refined->keyConfidence    = k.confidence;
                refined->keySource        = TrackAnalysis::Source::neural;
                refined->keyLowConfidence = false;
            }
            else if (k.valid)
            {
                refined->keySource        = TrackAnalysis::Source::dsp;
                refined->keyLowConfidence = true;
            }
        }

        if (! alive->load (std::memory_order_acquire))
            return juce::ThreadPoolJob::jobHasFinished;

        // The user may have replaced/cleared this deck while neural inference was
        // running. Never publish an old grid/key onto the new source.
        auto currentAudio = deckAt (deckIndex).getLoadedAudio();
        if (currentAudio == nullptr || currentAudio->path != file.getFullPathName())
            return juce::ThreadPoolJob::jobHasFinished;

        // §6.2: publish this as a NEW analysis version rather than overwriting the
        // first-pass result. A session captured before refinement landed replays
        // against the grid and key it actually performed to; overwriting would hand
        // that replay a different grid and make it diverge.
        {
            const auto hash = AnalysisCache::hashFile (file);
            refined->analysisVersion = juce::jmax (2u, analysisCache.latestVersion (hash) + 1u);
            analysisCache.store (hash, *refined);
        }

        // Build the beat grid for this analysis HERE, on a worker thread, and publish
        // it. Otherwise the audio thread discovers the analysis changed and rebuilds
        // the grid mid-render — makeGrid sorts and copies vectors, so that allocates.
        // StemDeckRtSafety catches exactly this.
        if (auto* tdb = dynamic_cast<TrackDeckBody*> (
                juce::isPositiveAndBelow (deckIndex, (int) channels.size()) && channels[(size_t) deckIndex]
                    ? channels[(size_t) deckIndex]->getBody() : nullptr))
        {
            const auto deckAudio = deckAt (deckIndex).getStemAudio (DeckPlayer::fullMixStem);
            const auto pfx = deckPrefix (deckIndex);
            tdb->publishGridForAnalysis (*refined, deckAudio.get(), refined.get(),
                                         getFloatParam (pfx + "Bpm"),
                                         getFloatParam (pfx + "BeatOffset"),
                                         getFloatParam (pfx + "Downbeat"),
                                         (int) std::round (getFloatParam (pfx + "TempoInterpretation")),
                                         (int) std::round (getFloatParam (pfx + "SyncMode")));
        }

        // Hot-swap: atomic publish, picked up on the next block. No audio interruption.
        deckAt (deckIndex).setRefinedAnalysis (refined);
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

void MainPluginProcessor::loadDeckStemFileAsync (int deckIndex, int stemLaneIndex, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)
        || ! juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes)
        || ! file.existsAsFile())
        return;

    const auto generation = deckLoadGeneration[(size_t) deckIndex][(size_t) stemLaneIndex]
                                .fetch_add (1, std::memory_order_relaxed) + 1;

    // §A3: two-phase. Phase 1 publishes decoded audio (deck playable); phase 2
    // publishes analysis without blocking it.
    auto onAudioReady = [this, deckIndex, stemLaneIndex, generation,
                         path = file.getFullPathName()]
                        (std::shared_ptr<const DeckPlayer::LoadedAudio> audio)
    {
        if (! audio
            || deckLoadGeneration[(size_t) deckIndex][(size_t) stemLaneIndex]
                   .load (std::memory_order_relaxed) != generation)
            return;
        deckAt (deckIndex).setStemAudio (stemLaneIndex, audio);
        if (stemLaneIndex == DeckPlayer::fullMixStem)
            deckAt (deckIndex).seekStart();
        setStatePathProperty (deckStemPathProperty (deckIndex, stemLaneIndex), path);
        logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed), "track_loaded",
                             deckNameFor (deckIndex), DeckPlayer::stemLaneId (stemLaneIndex), audio->displayName,
                             path, "Audio loaded into " + DeckPlayer::stemLaneLabel (stemLaneIndex));
    };

    auto onAnalysisReady = [this, deckIndex, stemLaneIndex, generation,
                            path = file.getFullPathName(), alive = aliveFlag]
                           (std::shared_ptr<const TrackAnalysis> analysis)
    {
        if (! alive->load (std::memory_order_acquire) || analysis == nullptr)
            return;
        if (deckLoadGeneration[(size_t) deckIndex][(size_t) stemLaneIndex]
                .load (std::memory_order_relaxed) != generation)
            return;
        if (stemLaneIndex != DeckPlayer::fullMixStem)
            return;

        deckAt (deckIndex).setRefinedAnalysis (analysis);
        if (analysis->detectedBpm > 1.0f)
            if (auto* bpm = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (deckPrefix (deckIndex) + "Bpm")))
                bpm->setValueNotifyingHost (bpm->convertTo0to1 (juce::jlimit (40.0f, 240.0f, analysis->detectedBpm)));

        // Neural refinement on top of the DSP grid/key, as before.
        refineAnalysisAsync (deckIndex, juce::File (path));

        {
            stemProvider.requestStemsAsync (
                juce::File (path),
                [this, deckIndex, path, generation, alive = aliveFlag]
                (StemProvider::Result result)
            {
                if (! alive->load (std::memory_order_acquire)) return;   // late completion after teardown
                if (deckLoadGeneration[(size_t) deckIndex][(size_t) DeckPlayer::fullMixStem]
                        .load (std::memory_order_relaxed) != generation)
                    return;
                logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed),
                                     "stem_cache", deckNameFor (deckIndex), "offline_cache",
                                     result.allCoreStemsReady ? "ready" : "pending",
                                     result.cacheDirectory.getFullPathName(), result.message);

                for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
                    if (result.stems[(size_t) lane].ready)
                        loadDeckStemFileAsync (deckIndex, lane, result.stems[(size_t) lane].file);

                if (result.allCoreStemsReady)
                    applyPremiumStemMix (deckIndex);
            });
        }
    };

    // Guard both callbacks: a job finishing during teardown must not run a
    // this-capturing closure against a freed processor (same contract as loadAudioAsync).
    loadPool->addJob (new TwoPhaseAudioLoadJob (file,
        [this] (const juce::File& f) { return decodeAudioFile (f); },
        [this] (TrackAnalysis& out, const juce::AudioBuffer<float>& audio, double sr, const juce::File& f)
        { analyseInto (out, audio, sr, f, /*runNeuralInline*/ false); },
        [alive = aliveFlag, cb = std::move (onAudioReady)] (std::shared_ptr<const DeckPlayer::LoadedAudio> a) mutable
        { if (alive->load (std::memory_order_acquire)) cb (std::move (a)); },
        std::move (onAnalysisReady)), true);
}

void MainPluginProcessor::loadPadFileAsync (int padIndex, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (padIndex, PadBank::numPads) || ! file.existsAsFile())
        return;
    const auto generation = padLoadGeneration[(size_t) padIndex]
                                .fetch_add (1, std::memory_order_relaxed) + 1;
    loadAudioAsync (file, [this, padIndex, generation, path = file.getFullPathName()]
                    (std::shared_ptr<const DeckPlayer::LoadedAudio> audio)
    {
        if (! audio
            || padLoadGeneration[(size_t) padIndex].load (std::memory_order_relaxed) != generation)
            return;
        padBank.setPadAudio (padIndex, audio);
        setStatePathProperty (padPathProperty (padIndex), path);
    });
}

void MainPluginProcessor::triggerPad (int padIndex)
{
    padBank.triggerPad (padIndex);
    logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed), "pad_trigger", {}, "pad", juce::String (padIndex + 1), getPadPath (padIndex));
}

void MainPluginProcessor::rewindDeck (int deckIndex)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
    {
        deckAt (deckIndex).seekStart();
        logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed), "cue_jump",
                             deckNameFor (deckIndex), "cue", "start", getDeckPath (deckIndex));
    }
}

// ── RTSS ─────────────────────────────────────────────────────────────────────
bool MainPluginProcessor::isDeckRtssActive (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return false;
    for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
        if (deckAt (deckIndex).getStemAudio (lane) != nullptr)
            return false;
    return deckAt (deckIndex).getLoadedAudio() != nullptr;
}

bool MainPluginProcessor::deckHasNeuralStems (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return false;
    for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
        if (deckAt (deckIndex).getStemAudio (lane) != nullptr)
            return true;
    return false;
}

void MainPluginProcessor::setDeckRtssSensitivity (int deckIndex, float sensitivity)
{
    // Route to the channel's own separator — that's the one on the render path.
    if (juce::isPositiveAndBelow (deckIndex, (int) channels.size()))
        if (auto& ch = channels[(size_t) deckIndex])
            if (auto* body = ch->getBody())
                if (auto* p = body->trackDeckParams())   // TrackDeck bodies only
                {
                    juce::ignoreUnused (p);
                    static_cast<TrackDeckBody*> (body)->setDrumsSensitivity (sensitivity);
                }
}

// ── Arrangement playback ──────────────────────────────────────────────────────
void MainPluginProcessor::buildAndStartArrangement (std::vector<ArrangementClipSpec> clips, double minLengthSeconds, double startSeconds)
{
    const auto buildGeneration =
        arrangementBuildGeneration.fetch_add (1, std::memory_order_relaxed) + 1;

    // Stop both transports before reconfiguring
    arrangementPlayer.stop();
    if (teWrapper != nullptr)
        teWrapper->edit->getTransport().stop (false, false);

    // Compute total arrangement length
    double totalLen = minLengthSeconds;
    for (const auto& spec : clips)
        totalLen = juce::jmax (totalLen, spec.timelineStartSeconds + spec.lengthSeconds);

    const bool hasContent = ! clips.empty() || totalLen > 0.0;
    if (! hasContent)
        return;

    const double engineSr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;

    // Render the arrangement through the in-plugin ArrangementPlayer, which mixes the
    // clips straight into the master bus. The old path pushed clips into Tracktion
    // Engine and relied on TE's transport for audio, but TE's output does not reliably
    // route back into the plugin buffer — so a captured arrangement's playhead moved
    // while producing SILENCE. Loading the clip audio + building the schedule happens
    // on a worker thread (no file I/O on the audio or message thread); MIDI plays via
    // the non-TE MidiClipPlayer (usingTeMidi = false).
    struct ArrPayload { std::vector<ArrangementClipSpec> clips; double totalLen = 0, startSecs = 0, engineSr = 44100; };
    auto payload = std::make_shared<ArrPayload>();
    payload->clips    = std::move (clips);
    payload->totalLen = totalLen;
    payload->startSecs = startSeconds;
    payload->engineSr = engineSr;

    if (loadPool == nullptr)
        return;

    // Use the processor-owned pool, not a detached Thread::launch. The pool is
    // drained before processor members are destroyed, so schedule decoding cannot
    // outlive formatManager/this. The generation also prevents a slow older build
    // from replacing a newer arrangement after it finishes.
    loadPool->addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
        [this, payload, buildGeneration, alive = aliveFlag]()
        -> juce::ThreadPoolJob::JobStatus
    {
        if (! alive->load (std::memory_order_acquire)
            || arrangementBuildGeneration.load (std::memory_order_relaxed) != buildGeneration)
            return juce::ThreadPoolJob::jobHasFinished;

        auto sched = std::make_shared<ArrangementPlayer::Schedule>();
        std::map<juce::String, std::shared_ptr<ChunkedAudioFile>> streamedFiles;
        for (const auto& spec : payload->clips)
        {
            if (! alive->load (std::memory_order_acquire)
                || arrangementBuildGeneration.load (std::memory_order_relaxed) != buildGeneration)
                return juce::ThreadPoolJob::jobHasFinished;
            if (! spec.file.existsAsFile() || spec.lengthSeconds <= 0.0) continue;
            const auto key = spec.file.getFullPathName();
            auto& streamed = streamedFiles[key];
            if (streamed == nullptr)
                streamed = ChunkedAudioFile::create (spec.file);
            if (streamed == nullptr || streamed->lengthInSamples() < 2)
                continue;

            ArrangementPlayer::Clip c;
            c.streamed              = streamed;
            c.sourceSampleRate      = streamed->sampleRate() > 0.0
                                          ? streamed->sampleRate()
                                          : payload->engineSr;
            c.timelineStartSamples  = (juce::int64) std::llround (spec.timelineStartSeconds * payload->engineSr);
            c.sourceStartSamples    = (juce::int64) std::llround (spec.sourceStartSeconds   * c.sourceSampleRate);
            c.timelineLengthSamples = (juce::int64) std::llround (spec.lengthSeconds         * payload->engineSr);
            c.gain                  = spec.gain;
            c.fadeInSamples         = (juce::int64) std::llround (spec.fadeInSeconds  * payload->engineSr);
            c.fadeOutSamples        = (juce::int64) std::llround (spec.fadeOutSeconds * payload->engineSr);
            c.gainAutomation.reserve (spec.gainAutomation.size());
            for (const auto& [seconds, value] : spec.gainAutomation)
                c.gainAutomation.emplace_back (
                    (juce::int64) std::llround (
                        seconds * payload->engineSr),
                    juce::jlimit (0.0f, 2.0f, value));
            auto copyAutomation = [engineSr = payload->engineSr] (
                const std::vector<std::pair<double, float>>& source,
                std::vector<std::pair<juce::int64, float>>& destination)
            {
                destination.reserve (source.size());
                for (const auto& [seconds, value] : source)
                    destination.emplace_back (
                        (juce::int64) std::llround (seconds * engineSr),
                        value);
            };
            copyAutomation (spec.lowAutomation, c.lowAutomation);
            copyAutomation (spec.midAutomation, c.midAutomation);
            copyAutomation (spec.highAutomation, c.highAutomation);
            copyAutomation (spec.filterAutomation, c.filterAutomation);
            c.dspEnabled = ! c.lowAutomation.empty()
                        || ! c.midAutomation.empty()
                        || ! c.highAutomation.empty()
                        || ! c.filterAutomation.empty();
            sched->push_back (std::move (c));
        }

        juce::MessageManager::callAsync ([this, sched, payload, buildGeneration, alive]
        {
            if (! alive->load (std::memory_order_acquire)
                || arrangementBuildGeneration.load (std::memory_order_relaxed) != buildGeneration)
                return;
            usingTeMidi.store (false, std::memory_order_relaxed);   // MIDI via MidiClipPlayer
            arrangementPlayer.setSchedule (sched);
            arrangementPlayer.setLengthSeconds (payload->totalLen);
            arrangementPlayer.setPositionSeconds (payload->startSecs);
            arrangementPlayer.start();
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

void MainPluginProcessor::setMidiNotes (const std::vector<MidiClipPlayer::SourceNote>& notes, double bpm)
{
    pendingMidiNotes = notes;
    pendingMidiBpm   = bpm;
    midiClipPlayer.setNotes (notes, bpm);   // keep non-TE path in sync
}

void MainPluginProcessor::setTeArrangementLaneGain (int lane, float gainLinear)
{
    const float dB = juce::Decibels::gainToDecibels (gainLinear);
    juce::MessageManager::callAsync ([this, lane, dB, alive = aliveFlag] {
        if (! alive->load (std::memory_order_acquire)) return;
        if (teWrapper == nullptr) return;
        auto tracks = tracktion::getAudioTracks (*teWrapper->edit);
        if (juce::isPositiveAndBelow (lane, tracks.size()))
            if (auto* vol = tracks[lane]->getVolumePlugin())
                vol->setVolumeDb (dB);
    });
}

void MainPluginProcessor::stopArrangement()
{
    arrangementBuildGeneration.fetch_add (1, std::memory_order_relaxed);
    usingTeMidi.store (false, std::memory_order_relaxed);
    arrangementPlayer.stop();
    if (teWrapper != nullptr)
        teWrapper->edit->getTransport().stop (false, false);
}

void MainPluginProcessor::serviceDeferredActions()
{
    // Message thread only. The audio thread requests a TE transport stop via an
    // atomic (instead of allocating a callAsync closure in processBlock).
    if (teStopRequested.exchange (false, std::memory_order_relaxed))
        if (teWrapper != nullptr)
            teWrapper->edit->getTransport().stop (false, false);

    // Message thread only. A MIDI-mapped Play trigger sets one of these on the
    // audio thread (see processBlock); the actual parameter string lookup and
    // setValueNotifyingHost happen here instead, off the audio thread.
    for (int i = 0; i < numDecks; ++i)
        if (deckPlayToggleRequested[(size_t) i].exchange (false, std::memory_order_relaxed))
            if (auto* p = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (deckPrefix (i) + "Play")))
                p->setValueNotifyingHost (p->get() ? 0.0f : 1.0f);
}

void MainPluginProcessor::setArrangementPositionSeconds (double s)
{
    arrangementPlayer.setPositionSeconds (s);
    if (teWrapper != nullptr)
        teWrapper->edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (s));
}

void MainPluginProcessor::setArrangementLoop (double startSec, double endSec, bool enabled)
{
    arrangementPlayer.setLoop (startSec, endSec, enabled);
    if (teWrapper != nullptr)
    {
        auto& transport = teWrapper->edit->getTransport();
        transport.loopPoint1 = tracktion::TimePosition::fromSeconds (startSec);
        transport.loopPoint2 = tracktion::TimePosition::fromSeconds (endSec);
        transport.looping    = enabled;
    }
}

// ── Slip mode + loop roll ────────────────────────────────────────────────────
void MainPluginProcessor::setDeckSlipEnabled (int deckIndex, bool on) noexcept
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        deckAt (deckIndex).setSlipEnabled (on);
}

bool MainPluginProcessor::isDeckSlipEnabled (int deckIndex) const noexcept
{
    return juce::isPositiveAndBelow (deckIndex, numDecks) && deckAt (deckIndex).isSlipEnabled();
}

void MainPluginProcessor::beginDeckLoopRoll (int deckIndex, double lengthSeconds) noexcept
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        deckAt (deckIndex).beginLoopRoll (lengthSeconds);
}

void MainPluginProcessor::endDeckLoopRoll (int deckIndex) noexcept
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        deckAt (deckIndex).endLoopRoll();
}

// ── Seek / beat jump ────────────────────────────────────────────────────────
void MainPluginProcessor::seekDeckSeconds (int deckIndex, double seconds)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return;
    deckAt (deckIndex).seekToSamples (seconds * audio->sourceSampleRate);
}

stemdeck::TransitionPlan MainPluginProcessor::planAutomixTransition (int outDeck, int inDeck) const
{
    stemdeck::TransitionPlan plan;
    if (! juce::isPositiveAndBelow (outDeck, numDecks) || ! juce::isPositiveAndBelow (inDeck, numDecks) || outDeck == inDeck)
        return plan;

    const auto aOut = getDeckActiveAnalysis (outDeck);
    const auto aIn  = getDeckActiveAnalysis (inDeck);
    const auto audioOut = deckAt (outDeck).getLoadedAudio();
    const auto audioIn  = deckAt (inDeck).getLoadedAudio();

    std::vector<float> eOut, eIn;
    if (audioOut != nullptr)
        eOut = stemdeck::AutomixEngine::phraseEnergies (audioOut->audio, aOut.sampleRate,
                    stemdeck::AutomixEngine::phraseBoundaries (aOut, 16));
    if (audioIn != nullptr)
        eIn = stemdeck::AutomixEngine::phraseEnergies (audioIn->audio, aIn.sampleRate,
                    stemdeck::AutomixEngine::phraseBoundaries (aIn, 16));

    return stemdeck::AutomixEngine::planTransition (aOut, aIn, eOut, eIn);
}

stemdeck::TransitionPlan MainPluginProcessor::beginAutomix (int outDeck, int inDeck)
{
    stemdeck::TransitionPlan plan = planAutomixTransition (outDeck, inDeck);
    if (! plan.valid)
        return plan;

    const auto audioIn = deckAt (inDeck).getLoadedAudio();
    if (audioIn != nullptr && audioIn->sourceSampleRate > 0.0)
        deckAt (inDeck).seekToSamples (plan.inPointSampleB);   // in-point is in source samples

    // Crossfader routing: outgoing on A/left, incoming on B/right.
    setDeckCfSide (outDeck, 0);
    setDeckCfSide (inDeck, 2);

    // Beat-sync so the incoming deck locks to the master grid.
    if (auto* p = apvts.getParameter ("beatSync")) p->setValueNotifyingHost (1.0f);

    // Start the incoming deck; the outgoing one keeps playing until the blend ends.
    if (auto* p = apvts.getParameter (deckPrefix (inDeck) + "Play")) p->setValueNotifyingHost (1.0f);

    return plan;
}

void MainPluginProcessor::beatJumpDeck (int deckIndex, double bars)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return;

    const float bpm = getFloatParam (deckPrefix (deckIndex) + "Bpm");
    const double barSeconds = (60.0 / juce::jmax (1.0f, bpm)) * 4.0;
    const double currentPos = getDeckPlayheadSeconds (deckIndex);
    const double newPos = juce::jmax (0.0, currentPos + bars * barSeconds);
    deckAt (deckIndex).seekToSamples (newPos * audio->sourceSampleRate);
}

// ── Hot cues ────────────────────────────────────────────────────────────────
juce::String MainPluginProcessor::hotCueProperty (int deckIndex, int cueIndex)
{
    return deckPrefix (deckIndex) + "HotCue" + juce::String (cueIndex);
}

void MainPluginProcessor::setHotCue (int deckIndex, int cueIndex, double seconds)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    if (! juce::isPositiveAndBelow (cueIndex, numHotCues)) return;
    apvts.state.setProperty (hotCueProperty (deckIndex, cueIndex), seconds, nullptr);
}

void MainPluginProcessor::clearHotCue (int deckIndex, int cueIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    if (! juce::isPositiveAndBelow (cueIndex, numHotCues)) return;
    apvts.state.setProperty (hotCueProperty (deckIndex, cueIndex), -1.0, nullptr);
}

double MainPluginProcessor::getHotCue (int deckIndex, int cueIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return -1.0;
    if (! juce::isPositiveAndBelow (cueIndex, numHotCues)) return -1.0;
    const auto val = apvts.state.getProperty (hotCueProperty (deckIndex, cueIndex), -1.0);
    return (double) val;
}

void MainPluginProcessor::jumpToHotCue (int deckIndex, int cueIndex)
{
    const double seconds = getHotCue (deckIndex, cueIndex);
    if (seconds >= 0.0)
        seekDeckSeconds (deckIndex, seconds);
}

// ── Intro / Outro markers ────────────────────────────────────────────────────
void MainPluginProcessor::setIntroEnd (int d, double s)
{
    if (juce::isPositiveAndBelow (d, numDecks))
        apvts.state.setProperty (deckPrefix (d) + "IntroEnd", s, nullptr);
}
double MainPluginProcessor::getIntroEnd (int d) const
{
    if (! juce::isPositiveAndBelow (d, numDecks)) return -1.0;
    return (double) apvts.state.getProperty (deckPrefix (d) + "IntroEnd", -1.0);
}
void MainPluginProcessor::clearIntroEnd (int d)   { setIntroEnd (d, -1.0); }

void MainPluginProcessor::setOutroStart (int d, double s)
{
    if (juce::isPositiveAndBelow (d, numDecks))
        apvts.state.setProperty (deckPrefix (d) + "OutroStart", s, nullptr);
}
double MainPluginProcessor::getOutroStart (int d) const
{
    if (! juce::isPositiveAndBelow (d, numDecks)) return -1.0;
    return (double) apvts.state.getProperty (deckPrefix (d) + "OutroStart", -1.0);
}
void MainPluginProcessor::clearOutroStart (int d) { setOutroStart (d, -1.0); }

// ── Crossfader side assignment ────────────────────────────────────────────────
// Crossfader side is read PER CHANNEL PER BLOCK on the audio thread. It used to be
// stored only as an APVTS ValueTree property, so the read did two forbidden things:
// it built the property-id String (an allocation — measured at ~16 allocs/block, the
// dominant RT-safety violation), and it touched a ValueTree that the message thread
// can mutate concurrently. The tree stays the persisted home; this atomic mirror is
// what the audio thread reads.
void MainPluginProcessor::setDeckCfSide (int d, int side)
{
    if (! juce::isPositiveAndBelow (d, numDecks)) return;
    stemdeck::events::Scope sc;
    sc.kind    = stemdeck::events::ScopeKind::channel;
    sc.channel = channelIdForSlot (d);
    eventDispatcher->dispatchParam (sc, stemdeck::events::ParamId::cfSide,
                                    (double) juce::jlimit (0, 2, side));
}

void MainPluginProcessor::applyDeckCfSide (int d, int side)
{
    if (! juce::isPositiveAndBelow (d, numDecks))
        return;
    const int clamped = juce::jlimit (0, 2, side);
    cfSideCache[(size_t) d].store (clamped, std::memory_order_relaxed);
    apvts.state.setProperty (deckPrefix (d) + "CfSide", clamped, nullptr);
}

int MainPluginProcessor::getDeckCfSide (int d) const
{
    if (! juce::isPositiveAndBelow (d, numDecks))
        return d % 2 == 0 ? 0 : 2;
    return cfSideCache[(size_t) d].load (std::memory_order_relaxed);
}

// Message thread: re-seed the atomics from the tree after a state load.
void MainPluginProcessor::refreshCfSideCache()
{
    // Direct, NOT dispatched: loading a session is not a gesture the user performed,
    // and recording it into a capture would inject phantom events.
    for (int d = 0; d < numDecks; ++d)
        cfSideCache[(size_t) d].store (
            juce::jlimit (0, 2, (int) apvts.state.getProperty (deckPrefix (d) + "CfSide",
                                                              d % 2 == 0 ? 0 : 2)),
            std::memory_order_relaxed);
}

// ── Loop region ──────────────────────────────────────────────────────────────
void MainPluginProcessor::setDeckLoopSeconds (int deckIndex, double inSeconds, double outSeconds)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return;
    const double sr = audio->sourceSampleRate;
    deckAt (deckIndex).setLoopRegion (inSeconds * sr, outSeconds * sr);
}

void MainPluginProcessor::setDeckLoopActive (int deckIndex, bool active)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        deckAt (deckIndex).setLoopActive (active);
}

void MainPluginProcessor::clearDeckLoop (int deckIndex)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        deckAt (deckIndex).clearLoopRegion();
}

bool MainPluginProcessor::isDeckLoopActive (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return false;
    return deckAt (deckIndex).isLoopActive();
}

double MainPluginProcessor::getDeckLoopInSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return -1.0;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return -1.0;
    const double s = deckAt (deckIndex).getLoopInSamples();
    return s < 0.0 ? -1.0 : s / audio->sourceSampleRate;
}

double MainPluginProcessor::getDeckLoopOutSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return -1.0;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return -1.0;
    const double s = deckAt (deckIndex).getLoopOutSamples();
    return s < 0.0 ? -1.0 : s / audio->sourceSampleRate;
}

// ── Tap tempo ────────────────────────────────────────────────────────────────
void MainPluginProcessor::tapTempo()
{
    const juce::int64 now = juce::Time::currentTimeMillis();
    // Drop taps older than 3 seconds.
    tapTimes.erase (std::remove_if (tapTimes.begin(), tapTimes.end(),
                                    [now] (juce::int64 t) { return (now - t) > 3000; }),
                    tapTimes.end());
    tapTimes.push_back (now);

    if (tapTimes.size() < 2) return;

    double avgInterval = 0.0;
    for (size_t i = 1; i < tapTimes.size(); ++i)
        avgInterval += (double) (tapTimes[i] - tapTimes[i - 1]);
    avgInterval /= (double) (tapTimes.size() - 1);

    const float bpm = (float) juce::jlimit (40.0, 240.0, 60000.0 / avgInterval);
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("masterBpm")))
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (bpm));
}

bool MainPluginProcessor::beginMasterRecording (const juce::File& destinationFile)
{
    if (destinationFile.getFullPathName().isEmpty())
        return false;

    const auto micFile = withSuffix (destinationFile, "-mic");
    if (! masterRecorder.beginRecording (destinationFile))
        return false;

    if (! micRecorder.beginRecording (micFile))
    {
        masterRecorder.stopRecording();
        return false;
    }

    for (auto& recorder : deckSourceRecorders) recorder.reset();
    for (auto& recorder : deckPrintedRecorders) recorder.reset();
    lastDeckSourceTargets.fill ({});
    lastDeckPrintedTargets.fill ({});

    for (int i = 0; i < numDecks; ++i)
    {
        std::shared_ptr<Channel> channel;
        for (const auto& candidate : channels)
            if (candidate != nullptr && candidate->slotIndex == i)
            {
                channel = candidate;
                break;
            }
        if (channel == nullptr)
            continue;   // do not write hours of silence for unused pool slots

        const auto suffix = "-deck-" + juce::String::charToString (
            (juce_wchar) ('A' + i));
        const auto sourceFile = withSuffix (destinationFile, suffix + "-source");
        const auto printedFile = withSuffix (destinationFile, suffix);
        const bool needsCleanSource =
            channel->type() == ChannelType::trackDeck;
        const bool sourceStarted = ! needsCleanSource
            || deckSourceRecorders[(size_t) i].beginRecording (sourceFile);
        const bool printStarted =
            deckPrintedRecorders[(size_t) i].beginRecording (printedFile);
        if (! sourceStarted || ! printStarted)
        {
            masterRecorder.stopRecording();
            micRecorder.stopRecording();
            for (int j = 0; j <= i; ++j)
            {
                deckSourceRecorders[(size_t) j].stopRecording();
                deckPrintedRecorders[(size_t) j].stopRecording();
            }
            return false;
        }
        if (needsCleanSource)
            lastDeckSourceTargets[(size_t) i] = sourceFile;
        lastDeckPrintedTargets[(size_t) i] = printedFile;
    }

    recordingSampleCounter.store (0, std::memory_order_relaxed);
    currentTakeNumber.store ((int) apvts.state.getProperty ("takeNumber", 0) + 1,
                             std::memory_order_relaxed);
    apvts.state.setProperty ("takeNumber", currentTakeNumber.load (std::memory_order_relaxed), nullptr);
    eventDispatcher->beginCapture();
    performanceRecorder.begin (destinationFile, currentSampleRate);
    midiCapture.beginTake();   // record what gets played, for later piano-roll editing
    initialisePerformanceCaptureState();
    lastPerformanceEventLogFile = performanceRecorder.getEventLogFile();
    lastArrangementFile = performanceRecorder.getArrangementFile();
    lastIntentStreamFile = destinationFile.getSiblingFile (
        destinationFile.getFileNameWithoutExtension() + "-intent.sdev");
    return true;
}

juce::File MainPluginProcessor::stopMasterRecording()
{
    const int64_t endSample = recordingSampleCounter.load (std::memory_order_relaxed);
    midiCapture.endTake();
    const auto masterFile = masterRecorder.stopRecording();
    const auto micFile = micRecorder.stopRecording();
    std::array<juce::File, numDecks> sourceDeckFiles;
    std::array<juce::File, numDecks> printedDeckFiles;
    for (int i = 0; i < numDecks; ++i)
    {
        sourceDeckFiles[(size_t) i] =
            deckSourceRecorders[(size_t) i].stopRecording();
        printedDeckFiles[(size_t) i] =
            deckPrintedRecorders[(size_t) i].stopRecording();
        // Typed input/instrument channels do not have the deck's pre-fader source
        // tap. Their per-channel print is still the editable aligned lane.
        if (sourceDeckFiles[(size_t) i].getFullPathName().isEmpty())
            sourceDeckFiles[(size_t) i] = printedDeckFiles[(size_t) i];
    }
    for (int i = 0; i < numDecks; ++i)
    {
        lastDeckSourceTargets[(size_t) i] = sourceDeckFiles[(size_t) i];
        lastDeckPrintedTargets[(size_t) i] = printedDeckFiles[(size_t) i];
    }
    std::array<juce::String, numDecks> channelNames;
    std::array<juce::String, numDecks> channelTypes;
    std::array<juce::int64, numDecks> channelIds;
    channelIds.fill (-1);
    for (const auto& channel : channels)
        if (channel != nullptr
            && juce::isPositiveAndBelow (channel->slotIndex, numDecks))
        {
            const auto slot = (size_t) channel->slotIndex;
            channelNames[slot] = channel->name;
            channelTypes[slot] = channelTypeId (channel->type());
            channelIds[slot] = channel->id;
        }
    eventDispatcher->endCapture();
    {
        stemdeck::events::Header header;
        header.sampleRate = currentSampleRate;
        header.createdUtc = juce::Time::currentTimeMillis();
        header.blockSize = (std::uint32_t) juce::jmax (1, preparedBlockSize);
        stemdeck::events::Writer::write (lastIntentStreamFile, header, eventDispatcher->takeRecorded());
    }
    lastArrangementFile = performanceRecorder.finishAndWrite (
        endSample, masterFile, micFile, sourceDeckFiles,
        printedDeckFiles, channelNames, channelTypes, channelIds);
    lastPerformanceEventLogFile = performanceRecorder.getEventLogFile();
    return masterFile;
}

int MainPluginProcessor::getDroppedBufferCount() const noexcept
{
    int total = audioDeadlineMisses.load (std::memory_order_relaxed)
              + masterRecorder.getDroppedBlockCount()
              + micRecorder.getDroppedBlockCount();
    for (const auto& recorder : deckSourceRecorders)
        total += recorder.getDroppedBlockCount();
    for (const auto& recorder : deckPrintedRecorders)
        total += recorder.getDroppedBlockCount();
    return total;
}

juce::File MainPluginProcessor::getLastDeckRecordingFile (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};
    return lastDeckSourceTargets[(size_t) deckIndex];
}

juce::File MainPluginProcessor::getLastDeckPrintedRecordingFile (
    int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};
    return lastDeckPrintedTargets[(size_t) deckIndex];
}

juce::String MainPluginProcessor::getDeckPath (int deckIndex) const
{
    return juce::isPositiveAndBelow (deckIndex, numDecks) ? deckAt (deckIndex).getPath() : juce::String{};
}

juce::String MainPluginProcessor::getDeckStemPath (int deckIndex, int stemLaneIndex) const
{
    return juce::isPositiveAndBelow (deckIndex, numDecks) && juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes)
        ? deckAt (deckIndex).getStemPath (stemLaneIndex)
        : juce::String{};
}

double MainPluginProcessor::getDeckPlayheadSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return 0.0;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0)
        return 0.0;
    return deckAt (deckIndex).getPlayheadReferenceSamples() / audio->sourceSampleRate;
}

double MainPluginProcessor::getDeckTotalSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return 0.0;
    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0)
        return 0.0;
    return (double) audio->numSourceSamples() / audio->sourceSampleRate;
}

juce::StringArray MainPluginProcessor::getStemLaneLabels() const
{
    juce::StringArray labels;
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        labels.add (DeckPlayer::stemLaneLabel (lane));
    return labels;
}

juce::StringArray MainPluginProcessor::getKeyTargetChoices() const
{
    return keyTargetChoices();
}

juce::StringArray MainPluginProcessor::getTempoInterpretationChoices() const
{
    return BeatSyncEngine::tempoInterpretationChoices();
}

juce::StringArray MainPluginProcessor::getSyncModeChoices() const
{
    return BeatSyncEngine::syncModeChoices();
}

TrackAnalysis MainPluginProcessor::getDeckAnalysis (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    if (auto audio = deckAt (deckIndex).getLoadedAudio())
        return audio->analysis;

    return {};
}

TrackAnalysis MainPluginProcessor::getDeckLiveAnalysisFromAtomics (int deckIndex) const
{
    TrackAnalysis analysis;
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return analysis;

    analysis.keyPitchClass = liveKeyPitchClass[(size_t) deckIndex].load (std::memory_order_relaxed);
    analysis.keyIsMinor = liveKeyIsMinor[(size_t) deckIndex].load (std::memory_order_relaxed) != 0;
    analysis.keyConfidence = liveKeyConfidence[(size_t) deckIndex].load (std::memory_order_relaxed);
    return analysis;
}

TrackAnalysis MainPluginProcessor::getDeckLiveAnalysis (int deckIndex) const
{
    return getDeckLiveAnalysisFromAtomics (deckIndex);
}

TrackAnalysis MainPluginProcessor::getDeckActiveAnalysis (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto deck = deckPrefix (deckIndex);
    auto live = getDeckLiveAnalysisFromAtomics (deckIndex);
    if (getBoolParam (deck + "LiveKey") && live.hasKey())
        return live;

    return getDeckAnalysis (deckIndex);
}

void MainPluginProcessor::refreshLiveKeyAnalysis()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < numDecks; ++i)
    {
        const auto deck = deckPrefix (i);
        if (! getBoolParam (deck + "LiveKey"))
            continue;

        if (now - (double) lastLiveKeyAnalysisMs[(size_t) i] < 950.0)
            continue;

        // Skip if a previous analysis for this deck is still running.
        bool expected = false;
        if (! liveKeyAnalysisInFlight[(size_t) i].compare_exchange_strong (expected, true))
            continue;

        lastLiveKeyAnalysisMs[(size_t) i] = (int64_t) now;

        // Run the heavy chroma FFT off the UI thread. loadPool is drained in the dtor,
        // so deckAt (i) stays valid for the job's lifetime; aliveFlag is belt-and-braces.
        loadPool->addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
            [this, i, alive = aliveFlag]() -> juce::ThreadPoolJob::JobStatus
        {
            if (alive->load (std::memory_order_acquire))
            {
                const auto analysis = deckAt (i).analyzeFullMixAroundPlayhead (8.0);
                liveKeyPitchClass[(size_t) i].store (analysis.keyPitchClass, std::memory_order_relaxed);
                liveKeyIsMinor[(size_t) i].store (analysis.keyIsMinor ? 1 : 0, std::memory_order_relaxed);
                liveKeyConfidence[(size_t) i].store (analysis.keyConfidence, std::memory_order_relaxed);
            }
            liveKeyAnalysisInFlight[(size_t) i].store (false, std::memory_order_release);
            return juce::ThreadPoolJob::jobHasFinished;
        }));
    }
}

juce::String MainPluginProcessor::getDeckKeyInfo (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto deck = deckPrefix (deckIndex);
    const bool liveKeyEnabled = getBoolParam (deck + "LiveKey");
    const auto liveAnalysis = getDeckLiveAnalysisFromAtomics (deckIndex);
    const auto analysis = getDeckActiveAnalysis (deckIndex);
    const int masterKeyChoice = (int) std::round (getFloatParam ("masterKey"));
    const int deckKeyChoice = (int) std::round (getFloatParam (deck + "KeyTarget"));
    const int targetKey = keyTargetChoiceToPitchClass (masterKeyChoice > 0 ? masterKeyChoice : deckKeyChoice);
    const float manualShift = getFloatParam (deck + "Pitch");

    juce::String text = analysis.hasKey()
        ? juce::String (liveKeyEnabled && liveAnalysis.hasKey() ? "Live key: " : "Key: ") + analysis.keyName() + " (" + juce::String ((int) std::round (analysis.keyConfidence * 100.0f)) + "%)"
        : "Key: unknown";

    if (analysis.hasKey() && targetKey >= 0)
    {
        const int matchShift = TrackAnalyzer::nearestSemitoneDelta (analysis.keyPitchClass, targetKey);
        text += u8 (" \xe2\x86\x92 ") + TrackAnalyzer::pitchClassName (targetKey) + " (" + (matchShift >= 0 ? "+" : "") + juce::String (matchShift) + " st)";
    }

    if (std::abs (manualShift) > 0.01f)
        text += " + manual " + juce::String (manualShift, 1) + " st";

    if (getBoolParam (deck + "KeyLock"))
        text += u8 (" \xe2\x80\xa2 preserve requested");

    if (liveKeyEnabled && ! liveAnalysis.hasKey())
        text += u8 (" \xe2\x80\xa2 live listening\xe2\x80\xa6");

    return text;
}

juce::String MainPluginProcessor::getDeckTimePitchStatus (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto deck = deckPrefix (deckIndex);
    const auto analysis = getDeckActiveAnalysis (deckIndex);
    const int masterKeyChoice = (int) std::round (getFloatParam ("masterKey"));
    const int deckKeyChoice = (int) std::round (getFloatParam (deck + "KeyTarget"));
    const int targetKey = keyTargetChoiceToPitchClass (masterKeyChoice > 0 ? masterKeyChoice : deckKeyChoice);
    const float matchShift = analysis.hasKey() && targetKey >= 0
                           ? (float) TrackAnalyzer::nearestSemitoneDelta (analysis.keyPitchClass, targetKey)
                           : 0.0f;

    const auto audio = deckAt (deckIndex).getLoadedAudio();
    const auto grid = BeatSyncEngine::makeGrid (getDeckAnalysis (deckIndex),
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));
    const auto plan = warpEngine.makePlan (sync.getSnapshot(), grid,
                                           audio ? audio->sourceSampleRate : currentSampleRate,
                                           audio ? (double) audio->numSourceSamples() : 0.0,
                                           getFloatParam (deck + "Pitch") + matchShift,
                                           getBoolParam (deck + "KeyLock"),
                                           getBoolParam ("hostSync") && getBoolParam ("beatSync"));
    return plan.statusLabel;
}

juce::String MainPluginProcessor::getDeckBeatSyncStatus (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto audio = deckAt (deckIndex).getLoadedAudio();
    const auto analysis = audio ? audio->analysis : TrackAnalysis{};
    const auto deck = deckPrefix (deckIndex);
    const auto grid = BeatSyncEngine::makeGrid (analysis,
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));
    const auto transport = sync.getSnapshot();
    const auto plan = BeatSyncEngine::makePlaybackPlan (transport, grid,
                                                        audio ? audio->sourceSampleRate : currentSampleRate,
                                                        audio ? (double) audio->numSourceSamples() : 0.0,
                                                        getBoolParam ("hostSync") && getBoolParam ("beatSync"));

    juce::String text = plan.statusLabel;
    if (analysis.detectedBpm > 1.0f)
        text += u8 (" \xe2\x80\xa2 detected ") + juce::String (analysis.detectedBpm, 1);
    if (! analysis.beatGridSamples.empty())
        text += u8 (" \xe2\x80\xa2 ") + juce::String ((int) analysis.beatGridSamples.size()) + " beats";
    if (! analysis.transientAnchorSamples.empty())
        text += u8 (" \xe2\x80\xa2 ") + juce::String ((int) analysis.transientAnchorSamples.size()) + " anchors";
    return text;
}

MainPluginProcessor::DeckSyncPhase MainPluginProcessor::getDeckSyncPhase (int deckIndex) const
{
    DeckSyncPhase r;
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return r;

    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0 || audio->numSourceSamples() <= 1)
        return r;

    // Rebuild the SAME grid + plan the audio path uses so the readout matches reality.
    const auto deck = deckPrefix (deckIndex);
    const auto grid = BeatSyncEngine::makeGrid (audio->analysis,
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));
    const auto transport = sync.getSnapshot();
    const auto plan = BeatSyncEngine::makePlaybackPlan (transport, grid,
                                                        audio->sourceSampleRate,
                                                        (double) audio->numSourceSamples(),
                                                        getBoolParam ("hostSync") && getBoolParam ("beatSync"));

    r.active = plan.syncEnabled;
    if (! plan.syncEnabled)
        return r;   // armed but host not playing → not phase-locked, no measured offset

    const double sourceBeatLen = audio->sourceSampleRate * 60.0 / juce::jmax (1.0f, plan.sourceBpm);
    if (sourceBeatLen <= 1.0)
        return r;

    // Where the grid says this beat should be (strict interpretation), vs where the
    // deck's playhead actually is. Difference in beats, wrapped to the nearest beat.
    const double sourceLen = (double) audio->numSourceSamples();
    const double correctedBeat = transport.ppqPosition
                               + (double) grid.manualGridCorrectionBeats
                               - grid.downbeatPositionBeats;
    const double idealSamples  = std::fmod (correctedBeat * sourceBeatLen, sourceLen);
    const double actualSamples = deckAt (deckIndex).getPlayheadReferenceSamples();

    double errBeats = (actualSamples - idealSamples) / sourceBeatLen;
    errBeats -= std::round (errBeats);                        // wrap to [-0.5, +0.5] beat
    r.offset = juce::jlimit (-1.0f, 1.0f, (float) (errBeats * 2.0));   // ±1.0 == ±half beat

    // Grid-locking modes (strict/anchor/groove) hard-seek to the grid every block, so
    // they genuinely hold phase; free-drift is allowed to float and is never "locked".
    const bool lockingMode = grid.syncMode != BeatSyncEngine::SyncMode::freeDrift;
    r.locked = lockingMode && std::abs (r.offset) < 0.12f;
    return r;
}

std::vector<double> MainPluginProcessor::getDeckBeatGridSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto audio = deckAt (deckIndex).getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0)
        return {};

    const auto deck = deckPrefix (deckIndex);
    const auto grid = BeatSyncEngine::makeGrid (audio->analysis,
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));

    std::vector<double> seconds;
    seconds.reserve (grid.beatGridSamples.size());
    for (const auto s : grid.beatGridSamples)
        seconds.push_back (s / audio->sourceSampleRate);
    return seconds;
}

juce::String MainPluginProcessor::getWarpEngineStatus() const
{
    return warpEngine.getStatusSummary();
}

juce::StringArray MainPluginProcessor::getSupportedPluginHostFormats() const
{
    return hostGraph.getSupportedPluginFormats();
}

juce::String MainPluginProcessor::getPluginHostStatus() const
{
    return hostGraph.getStatusSummary();
}

juce::String MainPluginProcessor::getStemProviderStatus() const
{
    return stemProvider.getName() + u8 (" \xe2\x80\xa2 cache: ") + stemProvider.getCacheRoot().getFullPathName();
}

// Is a neural stem model actually available (compiled in + a model file present)?
// This is what decides whether an AI split runs on the neural net vs the DSP/CLI path.
bool MainPluginProcessor::isNeuralStemAvailable() const
{
    return NeuralStemEngine::isCompiledIn() && NeuralStemEngine::defaultModelFile().existsAsFile();
}

juce::String MainPluginProcessor::getDeckStemProviderStatus (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto path = getDeckPath (deckIndex);
    const auto status = stemProvider.getStatusForSource (juce::File (path));
    juce::String text = status.headline;
    if (status.sourceFile.existsAsFile())
        text += u8 (" \xe2\x80\xa2 ") + juce::String (status.readyCoreStems) + "/" + juce::String (status.totalCoreStems) + " stems";
    if (status.jobQueued && ! status.jobRunning)
        text += u8 (" \xe2\x80\xa2 queued");
    if (status.jobRunning)
        text += u8 (" \xe2\x80\xa2 processing");
    if (status.detail.isNotEmpty())
        text += u8 (" \xe2\x80\xa2 ") + status.detail;
    // Surface which engine an AI split will use — neural net (model present) vs the
    // always-available DSP/CLI fallback.
    text += isNeuralStemAvailable() ? u8 (" \xe2\x80\xa2 Neural") : u8 (" \xe2\x80\xa2 DSP");
    return text;
}

float MainPluginProcessor::getDeckStemSeparationProgress (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return 1.0f;

    const auto status = stemProvider.getStatusForSource (juce::File (getDeckPath (deckIndex)));

    // No active job -> ring idle (full). While a split is queued/running, report the
    // fraction of core stems ready, clamped into (0,1) so the strip renders the
    // "separating" partial-fill instead of snapping to full or empty.
    if (! (status.jobQueued || status.jobRunning))
        return 1.0f;

    const float total = juce::jmax (1, status.totalCoreStems);
    return juce::jlimit (0.05f, 0.95f, (float) status.readyCoreStems / total);
}

juce::Array<juce::PluginDescription> MainPluginProcessor::getKnownHostPlugins() const
{
    return hostGraph.getKnownPlugins();
}

juce::File MainPluginProcessor::getPluginHostCacheFile() const
{
    return hostGraph.getCacheFile();
}

bool MainPluginProcessor::loadPluginHostCache()
{
    return hostGraph.loadCache();
}

bool MainPluginProcessor::savePluginHostCache() const
{
    return hostGraph.saveCache();
}

HostGraph::ScanResult MainPluginProcessor::scanPluginHostDefaultLocations (int maxPluginsPerFormat, bool recursive)
{
    return hostGraph.scanDefaultLocations (maxPluginsPerFormat, recursive);
}

juce::StringArray MainPluginProcessor::getPluginHostChainNames() const
{
    juce::StringArray names;
    for (int i = 0; i < (int) HostGraph::Chain::count; ++i)
        names.add (HostGraph::chainName ((HostGraph::Chain) i));
    return names;
}

void MainPluginProcessor::loadHostPluginToChainAsync (int chainIndex, int pluginIndex, HostGraph::LoadCallback callback)
{
    if (! juce::isPositiveAndBelow (chainIndex, (int) HostGraph::Chain::count))
    {
        if (callback)
            callback (false, "Invalid plugin chain");
        return;
    }

    const auto plugins = hostGraph.getKnownPlugins();
    if (! juce::isPositiveAndBelow (pluginIndex, plugins.size()))
    {
        if (callback)
            callback (false, "Invalid plugin selection");
        return;
    }

    // Wrap the caller's callback so compensation is recomputed once the plugin is in
    // (its latency is only known after instantiation).
    auto wrapped = [this, cb = std::move (callback), alive = aliveFlag] (bool ok, juce::String msg)
    {
        if (alive->load (std::memory_order_acquire))
            refreshLatencyCompensation();
        if (cb) cb (ok, msg);
    };
    hostGraph.loadPluginToChainAsync ((HostGraph::Chain) chainIndex, plugins.getReference (pluginIndex), std::move (wrapped));
}

void MainPluginProcessor::clearHostPluginChain (int chainIndex)
{
    if (juce::isPositiveAndBelow (chainIndex, (int) HostGraph::Chain::count))
        hostGraph.clearChain ((HostGraph::Chain) chainIndex);
    refreshLatencyCompensation();
}

juce::String MainPluginProcessor::getPadPath (int padIndex) const
{
    return padBank.getPadPath (padIndex);
}

juce::StringArray MainPluginProcessor::getSupportedAudioExtensions() const
{
    return juce::StringArray::fromTokens (formatManager.getWildcardForAllFormats(), ";", "");
}

void MainPluginProcessor::restoreFileAssignmentsFromState()
{
    for (int i = 0; i < numDecks; ++i)
    {
        const auto path = apvts.state.getProperty (deckPathProperty (i)).toString();
        std::array<juce::String, DeckPlayer::numStemLanes> stemPaths;
        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
            stemPaths[(size_t) lane] =
                apvts.state.getProperty (deckStemPathProperty (i, lane)).toString();

        // Snapshot lane paths before starting the full-mix request: a deliberate
        // source replacement clears old stem assignments immediately.
        if (path.isNotEmpty())
            loadDeckFileAsync (i, juce::File (path));

        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto& stemPath = stemPaths[(size_t) lane];
            if (stemPath.isNotEmpty())
                loadDeckStemFileAsync (i, lane, juce::File (stemPath));
        }
    }

    for (int i = 0; i < PadBank::numPads; ++i)
    {
        const auto path = apvts.state.getProperty (padPathProperty (i)).toString();
        if (path.isNotEmpty())
            loadPadFileAsync (i, juce::File (path));
    }
}

void MainPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("midiMap", midiMapping.serialize(), nullptr);
    state.setProperty ("stateVersion", kStateVersion, nullptr);

    // ── Channel list ────────────────────────────────────────────────────────
    // Persist which channels exist (type + slot + name + routing). Versioned
    // separately: state written before the channel architecture simply has no
    // <CHANNELS> child and is restored as numDecks TrackDecks (see setState).
    state.removeChild (state.getChildWithName ("CHANNELS"), nullptr);
    state.removeChild (state.getChildWithName ("HOST_GRAPH"), nullptr);
    juce::ValueTree chTree ("CHANNELS");
    chTree.setProperty ("channelsVersion", 1, nullptr);
    for (auto& ch : channels)
    {
        if (ch == nullptr) continue;
        juce::ValueTree c ("CHANNEL");
        c.setProperty ("id",     (juce::int64) ch->id,     nullptr);
        c.setProperty ("type",   channelTypeId (ch->type()), nullptr);
        c.setProperty ("slot",   ch->slotIndex,            nullptr);
        c.setProperty ("name",   ch->name,                 nullptr);
        c.setProperty ("cfSide", (int) ch->cfSide,         nullptr);
        chTree.appendChild (c, nullptr);
    }
    state.appendChild (chTree, nullptr);
    state.appendChild (hostGraph.createState(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

bool MainPluginProcessor::remapStateMediaPaths (
    juce::MemoryBlock& stateData,
    const juce::StringPairArray& replacements) const
{
    std::unique_ptr<juce::XmlElement> xml (
        getXmlFromBinary (stateData.getData(), (int) stateData.getSize()));
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);
    auto rewrite = [&replacements] (auto&& self, juce::ValueTree node) -> void
    {
        for (int property = 0; property < node.getNumProperties(); ++property)
        {
            const auto name = node.getPropertyName (property);
            const auto current = node.getProperty (name).toString();
            if (replacements.containsKey (current))
                node.setProperty (
                    name, replacements.getValue (current, current), nullptr);
        }
        for (int child = 0; child < node.getNumChildren(); ++child)
            self (self, node.getChild (child));
    };
    rewrite (rewrite, tree);
    auto remappedXml = tree.createXml();
    if (remappedXml == nullptr)
        return false;
    copyXmlToBinary (*remappedXml, stateData);
    return true;
}

void MainPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    // ── Schema migration (ONE-WAY DOOR, see StemGain.h) ─────────────────────────
    // v1 stored stem gain in dB over [-60,+12] where 0.0 meant UNITY. v2 stores a
    // linear percent where 0.0 means SILENCE. Reinterpreting v1 values as v2 would
    // silently mute every stem, so migrate before the state is applied. State with
    // no "stateVersion" property predates versioning and is therefore v1.
    const int fileVersion = (int) tree.getProperty ("stateVersion", 1);
    if (fileVersion < 2)
    {
        migrateStemGainsDbToPercent (tree);
        tree.setProperty ("stateVersion", kStateVersion, nullptr);
    }

    apvts.replaceState (tree);
    restoreChannelsFromState();
    hostGraph.restoreState (tree.getChildWithName ("HOST_GRAPH"));
    restoreFileAssignmentsFromState();
    midiMapping.deserialize (apvts.state.getProperty ("midiMap").toString());

    // Re-seed the audio-thread cfSide mirror from the freshly loaded tree.
    refreshCfSideCache();
}

// Rewrite every "<deck>Stem<lane>Gain" PARAM node from legacy dB to v2 percent.
void MainPluginProcessor::migrateStemGainsDbToPercent (juce::ValueTree& tree)
{
    for (int i = 0; i < numDecks; ++i)
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto id = deckPrefix (i) + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain";
            // APVTS stores each parameter as a child <PARAM id="..." value="..."/>.
            auto node = tree.getChildWithProperty ("id", id);
            if (! node.isValid() || ! node.hasProperty ("value")) continue;
            const float db = (float) node.getProperty ("value");
            node.setProperty ("value", stemdeck::stemgain::legacyDbToPercent (db), nullptr);
        }
}

void MainPluginProcessor::logPerformanceEvent (int64_t sample, const juce::String& type, const juce::String& deck,
                                               const juce::String& parameter, const juce::String& value,
                                               const juce::String& path, const juce::String& details)
{
    performanceRecorder.logEvent (sample, type, deck, parameter, value, path, details);
}

void MainPluginProcessor::initialisePerformanceCaptureState()
{
    const int64_t sample = 0;
    lastCrossfader = getFloatParam ("crossfader");
    lastMasterBpm = getFloatParam ("masterBpm");
    lastMicGain = getFloatParam ("micGain");
    lastMasterGain = getFloatParam ("master");
    lastLimiterCeiling = getFloatParam ("limiterCeiling");
    lastMasterFxX = getFloatParam ("masterFxX");
    lastMasterFxY = getFloatParam ("masterFxY");
    lastSmartMasterAmount = getFloatParam ("smartMasterAmount");
    lastSmartMasterMode =
        (int) std::round (getFloatParam ("smartMasterMode"));
    lastSmartMasterOn = getBoolParam ("smartMasterOn");
    lastMicMonitor = getBoolParam ("micMonitor");
    lastHostSync = getBoolParam ("hostSync");
    lastBeatSync = getBoolParam ("beatSync");

    logPerformanceEvent (sample, "parameter_change", {}, "crossfader", juce::String (lastCrossfader, 4));
    logPerformanceEvent (sample, "parameter_change", {}, "master_bpm", juce::String (lastMasterBpm, 2));
    logPerformanceEvent (sample, "parameter_change", {}, "master_gain", juce::String (lastMasterGain, 2));
    logPerformanceEvent (sample, "parameter_change", {}, "limiter_ceiling", juce::String (lastLimiterCeiling, 2));
    logPerformanceEvent (sample, "parameter_change", {}, "master_fx_x", juce::String (lastMasterFxX, 4));
    logPerformanceEvent (sample, "parameter_change", {}, "master_fx_y", juce::String (lastMasterFxY, 4));
    logPerformanceEvent (sample, "parameter_change", {}, "smart_master_amount", juce::String (lastSmartMasterAmount, 4));
    logPerformanceEvent (sample, "parameter_change", {}, "smart_master_mode", juce::String (lastSmartMasterMode));
    logPerformanceEvent (sample, "effect_toggle", {}, "smart_master_on", lastSmartMasterOn ? "on" : "off");
    logPerformanceEvent (sample, lastMicMonitor ? "mic_start" : "mic_stop", {}, "mic_monitor", lastMicMonitor ? "on" : "off");
    logPerformanceEvent (sample, "sync_change", {}, "host_sync", lastHostSync ? "on" : "off");
    logPerformanceEvent (sample, "sync_change", {}, "beat_sync", lastBeatSync ? "on" : "off");

    for (int i = 0; i < numDecks; ++i)
    {
        const auto deck = deckPrefix (i);
        auto& state = lastDeckPerformanceState[(size_t) i];
        state.play = getBoolParam (deck + "Play");
        state.keyLock = getBoolParam (deck + "KeyLock");
        state.liveKey = getBoolParam (deck + "LiveKey");
        state.bpm = getFloatParam (deck + "Bpm");
        state.volume = getFloatParam (deck + "Volume");
        state.fader = getFloatParam (deck + "Fader");
        state.pitch = getFloatParam (deck + "Pitch");
        state.beatOffset = getFloatParam (deck + "BeatOffset");
        state.downbeat = getFloatParam (deck + "Downbeat");
        state.low = getFloatParam (deck + "Low");
        state.mid = getFloatParam (deck + "Mid");
        state.high = getFloatParam (deck + "High");
        state.filter = getFloatParam (deck + "Filter");
        state.revMix = getFloatParam (deck + "RevMix");
        state.echoMix = getFloatParam (deck + "EchoMix");
        state.tempoInterpretation = (int) std::round (getFloatParam (deck + "TempoInterpretation"));
        state.syncMode = (int) std::round (getFloatParam (deck + "SyncMode"));

        const auto deckName = deckNameFor (i);
        const auto path = getDeckPath (i);
        if (path.isNotEmpty())
            logPerformanceEvent (sample, "track_loaded", deckName, "full_mix", juce::File (path).getFileName(), path, "Initial deck source");
        logPerformanceEvent (sample, state.play ? "play_start" : "play_stop", deckName, "play", state.play ? "on" : "off", path);
        logPerformanceEvent (sample, "parameter_change", deckName, "bpm", juce::String (state.bpm, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "volume", juce::String (state.volume, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "fader", juce::String (state.fader, 4));
        logPerformanceEvent (sample, "parameter_change", deckName, "pitch", juce::String (state.pitch, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "eq_low", juce::String (state.low, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "eq_mid", juce::String (state.mid, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "eq_high", juce::String (state.high, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "filter", juce::String (state.filter, 4));
        logPerformanceEvent (sample, "parameter_change", deckName, "reverb_mix", juce::String (state.revMix, 4));
        logPerformanceEvent (sample, "parameter_change", deckName, "echo_mix", juce::String (state.echoMix, 4));
        logPerformanceEvent (sample, "sync_change", deckName, "sync_mode", juce::String (state.syncMode));
    }
}

void MainPluginProcessor::updatePerformanceCaptureState (int64_t blockStartSample, const TransportSync::Snapshot& snapshot)
{
    if (! performanceRecorder.isActive())
        return;

    auto changedFloat = [] (float a, float b, float threshold) { return std::abs (a - b) > threshold; };

    const float crossfaderValue = getFloatParam ("crossfader");
    if (changedFloat (crossfaderValue, lastCrossfader, 0.002f))
    {
        lastCrossfader = crossfaderValue;
        logPerformanceEvent (blockStartSample, "parameter_change", {}, "crossfader", juce::String (crossfaderValue, 4));
    }

    const float masterBpmValue = (float) snapshot.bpm;
    if (changedFloat (masterBpmValue, lastMasterBpm, 0.01f))
    {
        lastMasterBpm = masterBpmValue;
        logPerformanceEvent (blockStartSample, "parameter_change", {}, "master_bpm", juce::String (masterBpmValue, 2));
    }

    const float micGain = getFloatParam ("micGain");
    if (changedFloat (micGain, lastMicGain, 0.05f))
    {
        lastMicGain = micGain;
        logPerformanceEvent (blockStartSample, "parameter_change", {}, "mic_gain", juce::String (micGain, 2));
    }

    auto logMasterFloat = [&] (juce::StringRef parameter, float value,
                               float& previous, float threshold)
    {
        if (changedFloat (value, previous, threshold))
        {
            previous = value;
            logPerformanceEvent (blockStartSample, "parameter_change", {},
                                 parameter, juce::String (value, 4));
        }
    };
    logMasterFloat ("master_gain", getFloatParam ("master"),
                    lastMasterGain, 0.05f);
    logMasterFloat ("limiter_ceiling", getFloatParam ("limiterCeiling"),
                    lastLimiterCeiling, 0.05f);
    logMasterFloat ("master_fx_x", getFloatParam ("masterFxX"),
                    lastMasterFxX, 0.002f);
    logMasterFloat ("master_fx_y", getFloatParam ("masterFxY"),
                    lastMasterFxY, 0.002f);
    logMasterFloat ("smart_master_amount",
                    getFloatParam ("smartMasterAmount"),
                    lastSmartMasterAmount, 0.002f);

    const int smartMasterMode =
        (int) std::round (getFloatParam ("smartMasterMode"));
    if (smartMasterMode != lastSmartMasterMode)
    {
        lastSmartMasterMode = smartMasterMode;
        logPerformanceEvent (blockStartSample, "parameter_change", {},
                             "smart_master_mode",
                             juce::String (smartMasterMode));
    }
    const bool smartMasterOn = getBoolParam ("smartMasterOn");
    if (smartMasterOn != lastSmartMasterOn)
    {
        lastSmartMasterOn = smartMasterOn;
        logPerformanceEvent (blockStartSample, "effect_toggle", {},
                             "smart_master_on",
                             smartMasterOn ? "on" : "off");
    }

    const bool micMonitor = getBoolParam ("micMonitor");
    if (micMonitor != lastMicMonitor)
    {
        lastMicMonitor = micMonitor;
        logPerformanceEvent (blockStartSample, micMonitor ? "mic_start" : "mic_stop", {}, "mic_monitor", micMonitor ? "on" : "off");
    }

    const bool hostSync = getBoolParam ("hostSync");
    if (hostSync != lastHostSync)
    {
        lastHostSync = hostSync;
        logPerformanceEvent (blockStartSample, "sync_change", {}, "host_sync", hostSync ? "on" : "off");
    }

    const bool beatSync = getBoolParam ("beatSync");
    if (beatSync != lastBeatSync)
    {
        lastBeatSync = beatSync;
        logPerformanceEvent (blockStartSample, "sync_change", {}, "beat_sync", beatSync ? "on" : "off");
    }

    for (int i = 0; i < numDecks; ++i)
    {
        const auto& ids = deckIds[(size_t) i];   // pre-concatenated ids — no RT alloc
        auto& state = lastDeckPerformanceState[(size_t) i];
        const auto& deckName = ids.displayName;   // cached constant, no per-block alloc

        auto logFloat = [&] (juce::StringRef parameter, float value, float& previous, float threshold)
        {
            if (changedFloat (value, previous, threshold))
            {
                previous = value;
                logPerformanceEvent (blockStartSample, "parameter_change", deckName, parameter, juce::String (value, 4));
            }
        };

        const bool play = getBoolParam (ids.play);
        if (play != state.play)
        {
            state.play = play;
            // getDeckPath allocates — fetch it only when a play event actually fires,
            // not every block.
            logPerformanceEvent (blockStartSample, play ? "play_start" : "play_stop", deckName, "play", play ? "on" : "off", getDeckPath (i));
        }

        logFloat ("bpm", getFloatParam (ids.bpm), state.bpm, 0.01f);
        logFloat ("volume", getFloatParam (ids.volume), state.volume, 0.05f);
        logFloat ("fader", getFloatParam (ids.fader), state.fader, 0.002f);
        logFloat ("pitch", getFloatParam (ids.pitch), state.pitch, 0.01f);
        logFloat ("beat_offset", getFloatParam (ids.beatOffset), state.beatOffset, 0.01f);
        logFloat ("downbeat", getFloatParam (ids.downbeat), state.downbeat, 0.01f);
        logFloat ("eq_low", getFloatParam (ids.low), state.low, 0.05f);
        logFloat ("eq_mid", getFloatParam (ids.mid), state.mid, 0.05f);
        logFloat ("eq_high", getFloatParam (ids.high), state.high, 0.05f);
        logFloat ("filter", getFloatParam (ids.filter), state.filter, 0.002f);
        logFloat ("reverb_mix", getFloatParam (ids.revMix),
                  state.revMix, 0.002f);
        logFloat ("echo_mix", getFloatParam (ids.echoMix),
                  state.echoMix, 0.002f);

        const bool keyLock = getBoolParam (ids.keyLock);
        if (keyLock != state.keyLock)
        {
            state.keyLock = keyLock;
            logPerformanceEvent (blockStartSample, "effect_toggle", deckName, "key_lock", keyLock ? "on" : "off");
        }

        const bool liveKey = getBoolParam (ids.liveKey);
        if (liveKey != state.liveKey)
        {
            state.liveKey = liveKey;
            logPerformanceEvent (blockStartSample, "effect_toggle", deckName, "live_key", liveKey ? "on" : "off");
        }

        const int tempoInterpretation = (int) std::round (getFloatParam (ids.tempoInterp));
        if (tempoInterpretation != state.tempoInterpretation)
        {
            state.tempoInterpretation = tempoInterpretation;
            logPerformanceEvent (blockStartSample, "sync_change", deckName, "tempo_interpretation", juce::String (tempoInterpretation));
        }

        const int syncMode = (int) std::round (getFloatParam (ids.syncMode));
        if (syncMode != state.syncMode)
        {
            state.syncMode = syncMode;
            logPerformanceEvent (blockStartSample, "sync_change", deckName, "sync_mode", juce::String (syncMode));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MainPluginProcessor();
}
