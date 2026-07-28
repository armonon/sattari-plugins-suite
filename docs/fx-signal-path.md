# StemDeck FX signal path

There are three places in the UI that say "FX", and until you have this diagram
it is genuinely unclear which one to reach for. This document is the answer to
"how do I put a filter on the vocal stem of channel B?" — and the honest answer
is that there are **two different right answers** depending on whether you want
the built-in DSP or a hosted plugin.

Traced from source, not from memory:
`MainPluginProcessor.cpp` (`onStemFx`, `onPostBody`, the master stage) and
`HostGraph.h` (`Chain`, `stemChain`).

## The path

```mermaid
flowchart TD
    File[Track file] --> Sep[Separation<br/>neural ONNX or DSP fallback]
    Sep --> S1[drums] & S2[bass] & S3[music] & S4[vocals]

    S1 & S2 & S3 & S4 --> StemPlug["<b>1a. Per-stem plugin chain</b><br/>HostGraph::stemChain(deck, stem)<br/><i>SATTARI FX → scope ▸DRM ▸BAS ▸MUS ▸VOX</i>"]

    StemPlug --> StemDsp["<b>1b. Per-stem built-in DSP</b><br/>gain knob, Filt / Send / Pitch<br/><i>channel → FX tab → per-stem grid</i>"]

    StemDsp --> Sum[Stems summed]
    Sum --> DeckDsp["<b>2. Deck built-in DSP</b><br/>LOW / MID / HIGH / FILT,<br/>reverb, echo, macro<br/><i>channel mix band + FX tab</i>"]

    DeckDsp --> DeckPlug["<b>3. Deck plugin chain</b><br/>HostGraph::Chain::deckA..deckD<br/><i>SATTARI FX → scope 'Deck X'</i>"]

    DeckPlug --> Fader[VOL fader / crossfader assign / pitch]
    Fader --> MasterSum[Master sum]

    Mic[Mic input] --> MicPlug["HostGraph::Chain::mic"] --> MasterSum

    MasterSum --> MasterPlug["<b>4. Master plugin chain</b><br/>HostGraph::Chain::master<br/><i>SATTARI FX → scope 'Master'</i>"]
    MasterPlug --> MasterXY["<b>5. MASTER FX</b><br/>masterFx.process(x, y)<br/><i>the XY filter pad</i>"]
    MasterXY --> Smart[AI Master] --> Lim[Limiter] --> Out[Output]
```

## Which control do I reach for?

| I want to… | Go to |
|---|---|
| Filter one stem, built-in, zero latency | Channel → **FX** tab → per-stem grid, `Filt` row, `VO` column |
| Put a *plugin* on one stem | Focus that channel, then **SATTARI FX** → `▸VOX` scope → `+` |
| Filter the whole channel | Channel → mix band → `FILT` knob |
| Put a *plugin* on the whole channel | **SATTARI FX** → `Deck X` scope |
| Sweep a filter over the whole mix, by hand | **MASTER FX** XY pad |
| Put a *plugin* across the mix | **SATTARI FX** → `Master` scope |

## Two known traps

**1. The stem scopes don't name their deck.** `refreshFxScopes()` builds the
`▸DRM / ▸BAS / ▸MUS / ▸VOX` scopes for **`focusedDeck`**, but the tabs are
labelled with the stem only. So the rack silently re-points at a different deck
when focus changes, and the tab strip looks identical either way. The scopes
should carry the deck letter (`B ▸ VOX`), which is the fix applied alongside
this document.

**2. "FX" means two different mechanisms.** The channel FX tab is **built-in
DSP** — parameters on the deck, captured in the event stream, always present.
SATTARI FX is a **plugin host** — VST3s in an `AudioProcessorGraph`, absent if
nothing is loaded. They are not alternatives to each other and they compose in a
fixed order: per-stem plugin → per-stem DSP → deck DSP → deck plugin.

## Chain identity is persisted — do not renumber

`HostGraph::Chain` values are written into saved state. The 16 stem chains are
appended **after** `master` precisely so the six original ids keep their numeric
values. Renumbering silently reassigns a user's FX to a different chain on load.
