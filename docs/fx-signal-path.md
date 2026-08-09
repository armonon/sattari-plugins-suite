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

    S1 & S2 & S3 & S4 --> StemPlug["<b>1a. Per-stem plugin chain</b><br/>HostGraph::stemChain(deck, stem)<br/><i>FX ROUTE → STEM → Channel X → named stem</i>"]

    StemPlug --> StemDsp["<b>1b. Per-stem built-in DSP</b><br/>gain knob, Filt / Send / Pitch<br/><i>channel → FX tab → per-stem grid</i>"]

    StemDsp --> Sum[Stems summed]
    Sum --> DeckDsp["<b>2. Deck built-in DSP</b><br/>LOW / MID / HIGH / FILT,<br/>reverb, echo, macro<br/><i>channel mix band + FX tab</i>"]

    DeckDsp --> DeckPlug["<b>3. Channel plugin chain</b><br/>HostGraph::channelChain(slot)<br/><i>FX ROUTE → CHANNEL → named channel/input</i>"]

    DeckPlug --> Fader[VOL fader / crossfader assign / pitch]
    Fader --> MasterSum[Master sum]

    Mic[Mic input] --> MicPlug["HostGraph::Chain::mic"] --> MasterSum

    MasterSum --> MasterPlug["<b>4. Master plugin chain</b><br/>HostGraph::Chain::master<br/><i>FX ROUTE → MASTER → Master Bus</i>"]
    MasterPlug --> MasterXY["<b>5. MASTER FX</b><br/>masterFx.process(x, y)<br/><i>the XY filter pad</i>"]
    MasterXY --> Smart[AI Master] --> Lim[Limiter] --> Out[Output]
```

## Which control do I reach for?

| I want to… | Go to |
|---|---|
| Filter one stem, built-in, zero latency | Channel → **FX** tab → per-stem grid, `Filt` row, `VO` column |
| Put a *plugin* on one stem | **FX ROUTE** → **STEM** → channel → named stem → **add FX** |
| Filter the whole channel | Channel → mix band → `FILT` knob |
| Put a *plugin* on the whole channel or input | **FX ROUTE** → **CHANNEL** → named destination → **add FX** |
| Sweep a filter over the whole mix, by hand | **MASTER FX** XY pad |
| Put a *plugin* across the mix | **FX ROUTE** → **MASTER** → **add FX** |

## Important distinction

The selected plugin destination is stored by stable HostGraph chain identity.
Changing the focused deck does not change it. If a channel is removed, the rack
falls back to the Master Bus instead of silently selecting another channel.

"FX" still covers two mechanisms. The channel FX tab is **built-in
DSP** — parameters on the deck, captured in the event stream, always present.
FX ROUTE is a **plugin host** — trusted plugins in an `AudioProcessorGraph`, absent if
nothing is loaded. They are not alternatives to each other and they compose in a
fixed order: per-stem plugin → per-stem DSP → deck DSP → deck plugin.

## Chain identity is persisted — do not renumber

`HostGraph::Chain` values are written into saved state. The 16 stem chains are
appended **after** `master` precisely so the six original ids keep their numeric
values. Renumbering silently reassigns a user's FX to a different chain on load.
