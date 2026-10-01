#pragma once

#include "PluginProcessor.h"

class MixController
{
public:
    enum class Mode { draft, proposed, original, kept };
    explicit MixController (SattariMixAudioProcessor& p) : processor (p) {}

    sattari::automix::Plan plan;
    std::vector<sattari::automix::Track> tracks;
    juce::String status { "No mix group" };
    Mode mode = Mode::draft;
    bool levelMatch = true;

    bool busy() const { return pending.isNotEmpty(); }
    bool auditioning() const { return mode == Mode::proposed || mode == Mode::original; }
    bool analyzing() const { return analysis.isNotEmpty(); }
    void startViewing() { ++viewers; poll(); }
    void stopViewing() { viewers = juce::jmax (0, viewers - 1); }

    void analyze (int seconds)
    {
        using namespace sattari::automix;
        if (busy() || auditioning()) return;
        tracks = processor.linkedTracks();
        if (tracks.size() < 2 || tracks.size() > 64) { status = "Link 2 to 64 tracks to analyze a mix."; return; }
        analysis = juce::Uuid().toString(); plan = {}; mode = Mode::draft;
        analysisMembers.clear();
        for (const auto& t : tracks)
        {
            auto c = object(); put (c, "action", "analyze"); put (c, "token", analysis); put (c, "seconds", seconds);
            if (! processor.sendCommand (t.id, c)) { status = "Could not start every track. Retry analysis."; analysis.clear(); return; }
            analysisMembers.add (t.id);
        }
        status = "Analyzing group";
    }

    void configure (const sattari::automix::Track& t, int role, bool locked)
    {
        using namespace sattari::automix;
        if (busy() || auditioning()) return;
        auto c = object(); put (c, "action", "configure"); put (c, "token", juce::Uuid().toString());
        put (c, "beforeRole", (int) t.role); put (c, "beforeLock", t.locked);
        put (c, "role", role); put (c, "locked", locked);
        if (! processor.sendCommand (t.id, c)) status = "Could not update track role.";
        plan = {}; mode = Mode::draft;
    }

    void buildProposal()
    {
        if (busy() || auditioning()) return;
        tracks = processor.linkedTracks();
        plan = sattari::automix::makePlan (tracks, processor.trackSnapshot().capture);
        mode = Mode::draft;
        status = plan.valid() ? "Proposal ready" : plan.error;
    }

    void audition (bool proposed) { dispatch (proposed ? Mode::proposed : Mode::original, false); }
    void keep() { dispatch (Mode::kept, false); }
    void undo() { dispatch (Mode::draft, true); }

    void poll()
    {
        using namespace sattari::automix;
        if (viewers == 0 && ! busy() && ! analyzing()) return;
        tracks = processor.linkedTracks();
        if (processor.groupName().isEmpty()) { status = "No mix group"; return; }
        if (processor.sessionError().isNotEmpty()) { status = processor.sessionError(); return; }
        if (busy())
        {
            bool done = true;
            juce::String error;
            for (const auto& target : waiting)
            {
                const auto* t = find (target.id);
                if (t == nullptr || t->ack != pending) { done = false; continue; }
                if (t->error.isNotEmpty()) error = t->name + ": " + t->error;
            }
            if (error.isNotEmpty() || juce::Time::getMillisecondCounterHiRes() > deadline)
            {
                // Only restore settings that still match our command. Local user
                // edits always win, including edits made during a failed batch.
                rollback();
                status = error.isNotEmpty() ? error : "Some tracks did not respond; applied changes were rolled back where reachable.";
                pending.clear(); plan = {}; mode = Mode::draft;
                return;
            }
            if (done)
            {
                pending.clear(); mode = nextMode;
                status = mode == Mode::proposed ? "Auditioning proposal" : mode == Mode::original ? "Auditioning original"
                    : mode == Mode::kept ? "Mix kept" : "Original restored";
                if (clearOnComplete) plan = {};
            }
        }
        if (analyzing())
        {
            bool ready = true;
            float progress = 1;
            for (const auto& id : analysisMembers)
            {
                const auto* t = find (id);
                if (t == nullptr) { analysis.clear(); status = "A track left the group. Analyze again."; return; }
                ready = ready && t->capture == analysis && t->measured.complete;
                progress = juce::jmin (progress, t->capture == analysis ? t->measured.progress : 0.0f);
            }
            status = "Analyzing group: " + juce::String (juce::roundToInt (progress * 100)) + "%";
            if (ready) { analysis.clear(); buildProposal(); }
        }
        else if (! busy() && plan.tracks.empty() && status == "No mix group")
            status = juce::String ((int) tracks.size()) + " linked tracks";
    }

    void cancelAnalysis()
    {
        using namespace sattari::automix;
        for (const auto& t : tracks)
        {
            auto c = object(); put (c, "action", "stop"); put (c, "token", juce::Uuid().toString());
            processor.sendCommand (t.id, c);
        }
        analysis.clear(); status = "Analysis stopped";
    }
    void clear()
    {
        if (busy() || auditioning()) return;
        plan = {}; analysis.clear(); mode = Mode::draft; status = "No mix group";
    }
    void resetForGroup()
    {
        pending.clear(); waiting.clear(); plan = {}; analysis.clear();
        mode = Mode::draft; appliedTrim = 0; status = "No mix group";
    }

private:
    struct Waiting
    {
        juce::String id;
        sattari::automix::Settings before, after;
    };
    const sattari::automix::Track* find (const juce::String& id) const
    {
        for (const auto& t : tracks) if (t.id == id) return &t;
        return nullptr;
    }
    void dispatch (Mode targetMode, bool clearAfter)
    {
        using namespace sattari::automix;
        if (busy() || ! plan.valid()) return;
        tracks = processor.linkedTracks();
        if (tracks.size() != plan.tracks.size()) { status = "Group membership changed. Analyze again."; return; }
        waiting.clear();
        const bool proposed = targetMode == Mode::proposed || targetMode == Mode::kept;
        const float trim = levelMatch && (targetMode == Mode::proposed || targetMode == Mode::original)
            ? comparisonTrim (plan, proposed) : 0.0f;
        for (const auto& s : plan.tracks)
        {
            const auto* t = find (s.source.id);
            if (t == nullptr || t->role != s.source.role || t->locked != s.source.locked)
            { status = "Track roles or locks changed. Build a new proposal."; waiting.clear(); return; }
            Settings expected = mode == Mode::proposed || mode == Mode::kept ? s.proposed : s.source.settings;
            if (mode == Mode::proposed || mode == Mode::original)
                expected.trim = juce::jlimit (-36.0f, 0.0f, expected.trim + appliedTrim);
            if (! equivalent (t->settings, expected))
            { status = "Settings changed on " + t->name + ". Build a new proposal."; waiting.clear(); return; }
            if (! clearAfter && proposed && t->capture != s.source.capture)
            { status = "Analysis changed. Analyze the group again."; waiting.clear(); return; }
            auto after = proposed ? s.proposed : s.source.settings;
            after.trim = juce::jlimit (-36.0f, 0.0f, after.trim + trim);
            waiting.push_back ({ t->id, t->settings, after });
        }
        pending = juce::Uuid().toString(); nextMode = targetMode; clearOnComplete = clearAfter;
        deadline = juce::Time::getMillisecondCounterHiRes() + 7000;
        bool sent = true;
        for (const auto& w : waiting)
        {
            auto c = object(); put (c, "action", "settings"); put (c, "token", pending);
            put (c, "before", encode (w.before)); put (c, "after", encode (w.after));
            if (proposed && ! clearAfter) put (c, "capture", find (w.id)->capture);
            sent = processor.sendCommand (w.id, c) && sent;
        }
        appliedTrim = trim;
        status = sent ? "Updating group" : "Waiting for group; a command could not be delivered";
    }
    void rollback()
    {
        using namespace sattari::automix;
        for (const auto& w : waiting)
        {
            const auto* t = find (w.id);
            if (t == nullptr || (! equivalent (t->settings, w.after) && ! equivalent (t->settings, w.before))) continue;
            auto original = w.before;
            for (const auto& s : plan.tracks) if (s.source.id == w.id) original = s.source.settings;
            auto c = object(); put (c, "action", "settings"); put (c, "token", juce::Uuid().toString());
            put (c, "before", encode (t->settings)); put (c, "after", encode (original));
            processor.sendCommand (w.id, c);
        }
    }
    SattariMixAudioProcessor& processor;
    juce::String analysis, pending;
    juce::StringArray analysisMembers;
    std::vector<Waiting> waiting;
    double deadline = 0;
    float appliedTrim = 0;
    Mode nextMode = Mode::draft;
    bool clearOnComplete = false;
    int viewers = 0;
};
