// Single source of truth for the plugin grid AND the dedicated detail pages.
// Each entry renders both a card on index.html and a full page on plugin.html?id=<id>.
window.SATTARI_PLUGINS = [
  {
    id: "auto-pitch", name: "Sattari Auto Pitch", icon: "🎤",
    category: "Vocal tuning", cats: "Vocal Key",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Realtime pitch correction — transparent to hard-tuned, always in key.",
    what: "Auto Pitch keeps a vocal in tune in real time. Dial it back for invisible, natural correction, or push it for the hard, snapped chart sound. It knows the song's key, so every note lands where it should without you automating anything.",
    how: "The incoming voice is tracked note-by-note by a YIN pitch follower (or, with a model installed, a CREPE neural network that doesn't octave-jump on breathy takes). Each detected note is snapped to the nearest tone in the key, and the shift is performed by the Rubber Band engine, which preserves formants so the voice never turns into a chipmunk. A per-note transition model retunes fast on new notes and relaxes on sustains so vibrato survives.",
    features: [
      "One tactile key button: Auto-detect (AutoKey), Follow Song Key, or set by hand",
      "AutoKey now detects both the root AND major/minor (full 24-key)",
      "Neural Pitch (optional CREPE model) for breathy / noisy vocals",
      "Live Engine (PSOLA) — near-zero-latency monitoring while you track",
      "Styles: Natural, Classic, Modern Rap, Robot, Big Harmony",
      "Preserve Vibrato, Flex-Tune, Throat/formant, Edit Notes per pitch class"
    ]
  },
  {
    id: "song-key", name: "Sattari Key", icon: "🔑",
    category: "Key detection", cats: "Key",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Put it on the master. It tells the whole suite what key you're in.",
    what: "Sattari Key listens to your full mix and continuously works out the song's key. It then broadcasts that key to every other Sattari plugin in the session, so Auto Pitch, Harmony, Maqam, VoxKey and Arp all follow automatically.",
    how: "It builds a chromagram (an FFT of the mix folded into the 12 pitch classes) and runs a 24-key Krumhansl–Schmuckler correlation to pick the best major or minor key. The result is published every ~700 ms to the cross-plugin bus — a lock-free shared channel — so consumers update with zero latency and no MIDI routing.",
    features: [
      "Scans the full mix (chords + bass), not just a solo line",
      "Full 24-key detection: root and major/minor",
      "One authoritative key for the whole session",
      "Confidence readout; manual override without affecting others"
    ]
  },
  {
    id: "voxkey", name: "Sattari VoxKey", icon: "🎵",
    category: "Voice-to-MIDI", cats: "Vocal MIDI",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Sing it, hum it, beatbox it — get MIDI out for any instrument.",
    what: "VoxKey turns your voice into MIDI in real time. Hum a bassline and play it on a synth; sing a lead and drive a sampler; beatbox and trigger drums. The notes are snapped to your key and scale so what you get back is musical, not raw pitch mush.",
    how: "Your voice is pitch-tracked every block (YIN, or an optional CREPE neural model for cleaner detection on breathy takes), then mapped to MIDI notes with onset/hold/release shaping so consonants and breaths don't spawn junk notes. A key/scale quantizer keeps output in tune, and modes reshape the mapping for melodies, basslines, harmony, drums, or pad layers.",
    features: [
      "Real-time voice → MIDI, route to any synth/sampler",
      "Neural Pitch (optional CREPE) for accurate tracking",
      "Modes: Melody, Bass Follow, Harmony, Drum Mouth, Pad/Soul Layer",
      "Key + scale snap, pitch bend, glide/legato",
      "Follows the session key from Sattari Key"
    ]
  },
  {
    id: "royal-chain", name: "Sattari Royal Chain", icon: "👑",
    category: "Vocal chain", cats: "Vocal",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "A whole vocal channel strip in one plugin — with a living visualizer.",
    what: "Royal Chain is a complete vocal signal path: clean up, control, tone, air, and space, all staged in the right order so a raw take sounds finished with a couple of macro moves. A Portal-inspired cymatic compass reacts to your audio while you work.",
    how: "The chain runs gate → EQ → compression → de-ess → saturation → reverb, driven by a few musical macros instead of dozens of knobs. It can pull the current key from the cross-plugin bus for its Tune stage, so it stays in the song's key.",
    features: [
      "Full strip: gate, EQ, comp, de-ess, saturation, reverb",
      "Macro controls — finished vocal in a couple of moves",
      "Cymatic 'compass' visualizer reacts to the audio",
      "Key-aware Tune stage via the cross-plugin bus"
    ]
  },
  {
    id: "harmony", name: "Sattari Harmony", icon: "🎼",
    category: "Vocal harmony engine", cats: "Vocal Key",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Up to four diatonic harmony voices + choir, locked to the song key.",
    what: "Harmony generates backing vocals from your lead. Add thirds and fifths above and below, thicken to a choir, and it stays in the song's key so the parts are always diatonically correct — no wrong notes, no manual intervals.",
    how: "Four independent Rubber Band voices shift the lead to the nearest chord/scale tones at the intervals you choose, following the key from the cross-plugin bus. A choir doubler adds chorus-modulated copies, humanize scatters timing/pitch slightly for realism, and each voice has its own formant shift and pan.",
    features: [
      "Four diatonic voices, above and below the lead",
      "Choir doubler for lush stacked backing vocals",
      "Follows the session key (Sattari Key) automatically",
      "Per-voice formant shift, pan, level; humanize for realism"
    ]
  },
  {
    id: "maqam", name: "Sattari Maqam", icon: "🌙",
    category: "Microtonal tuning", cats: "Vocal Key",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Quarter-tone pitch correction for Arabic, Persian & Turkish modes.",
    what: "Maqam is pitch correction that understands microtonal music. Instead of forcing everything onto the 12-note Western grid, it tunes to the neutral thirds and quarter-tones of the maqamat, so traditional vocals stay authentic and in tune.",
    how: "Each maqam (Rast, Bayati, Hijaz, Saba and more) is defined by a cents table with its characteristic neutral and quarter-tone degrees. The detected pitch is snapped to the nearest microtonal degree of the selected maqam rooted at the chosen key, using the same formant-preserving shifter as Auto Pitch.",
    features: [
      "8 maqamat with authentic neutral-third / quarter-tone degrees",
      "Snaps to microtonal scale degrees, not just semitones",
      "Formant-preserving correction",
      "Follows the session key; manual root selection"
    ]
  },
  {
    id: "vocal-master", name: "Sattari Vocal Master", icon: "✨",
    category: "Vocal cleanup + master", cats: "Vocal Stem",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Isolate the voice, clean it, and finish it — one chain.",
    what: "Vocal Master rescues and polishes vocals: pull the voice forward out of a busy backing, knock down noise and harsh sibilance, shape the tone and air, even it out, and finish with a gentle limiter. Great for cleaning up recorded or extracted vocals.",
    how: "The chain is mid-side isolation (push the sides/music down) → high-pass → tilt EQ → air shelf → a Linkwitz-Riley de-esser → a downward-expander noise gate → an auto-leveler → a soft tanh limiter, with a dry/wet blend. An AI stem-removal seam can isolate the vocal offline for the hardest cases.",
    features: [
      "Mid-side isolation to lift the vocal out of the music",
      "De-noise (expander gate) + Linkwitz-Riley de-ess",
      "Tilt tone, air shelf, auto-leveler, soft limiter",
      "AI stem-removal seam for offline vocal isolation"
    ]
  },
  {
    id: "stemdeck", name: "Sattari StemDeck", icon: "🎚",
    category: "Stem performance", cats: "Stem",
    formats: ["AU", "Standalone"],
    tagline: "Split any track into stems, mix them live, record the arrangement.",
    what: "StemDeck is a remix instrument. Load a finished track and pull it apart into drums, bass, vocals and other; then perform with the stems on faders, mute and swap parts live, and capture what you played as an arrangement you can keep building.",
    how: "An ONNX Demucs model separates the track into four stems offline (the same class of engine as Moises / RipX). A split workspace gives you a live performance mixer on one side and an arrangement recorder on the other, so a live jam becomes an editable session. An AU build lets you route stems inside a DAW.",
    features: [
      "4-stem separation (drums / bass / vocals / other) via ONNX Demucs",
      "Live mixer: stems on faders, mute/solo/swap",
      "Arrangement recorder — capture a performance",
      "Standalone app + AU for in-DAW stem routing"
    ]
  },
  {
    id: "rhythm-genie", name: "Sattari Rhythm Genie", icon: "🥁",
    category: "MIDI drum generator", cats: "MIDI",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Pick a groove, dial density and swing, feed any drum sampler.",
    what: "Rhythm Genie writes drum patterns as MIDI so you can start a beat in seconds. Choose a groove style, set how busy it is and how much swing, tweak the kick/snare/hat layers, and route the MIDI to whatever drums you like.",
    how: "It generates host-synced MIDI patterns locked to your project tempo, with independent control over each drum lane's density and placement, plus swing. Because it outputs MIDI (not audio), you keep full control of the sounds in your own sampler.",
    features: [
      "Host-synced MIDI drum patterns",
      "Groove styles + density and swing controls",
      "Per-lane kick / snare / hat shaping",
      "Feeds any drum sampler or kit"
    ]
  },
  {
    id: "sattari-arp", name: "Sattari Arp", icon: "🎹",
    category: "MIDI arpeggiator", cats: "MIDI Key",
    formats: ["AU", "VST3"],
    tagline: "A diatonic arpeggiator that follows the session key.",
    what: "Sattari Arp turns held chords or notes into flowing arpeggios that stay in the song's key. Set the rate, range and pattern and it does the rest — tempo-locked and always musical.",
    how: "Incoming notes are re-ordered and re-triggered on a tempo-synced grid, with the output snapped to the current scale. It reads the key from the cross-plugin bus, so the arp follows Sattari Key automatically. Rate, octave range, gate length, velocity and note order are all adjustable.",
    features: [
      "Diatonic — snaps the arp to the song's scale",
      "Follows the session key (Sattari Key)",
      "Rate, octaves, gate, pattern, velocity curve",
      "Tempo-synced to the host"
    ]
  },
  {
    id: "bus", name: "Sattari Bus", icon: "📊",
    category: "Bus / mix", cats: "Mix",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "The last thing on every bus: glue, tone, drive, width, ceiling.",
    what: "Sattari Bus is a channel strip for your groups and mix bus. Glue the parts together, shape the tone, add drive and width, blend in parallel, and cap the output — the finishing move on a drum bus, a vocal bus, or the master.",
    how: "A glue compressor evens the dynamics, a 3-band tone section shapes the balance, harmonic drive adds weight and edge, a width control opens the stereo image, and a parallel wet blend plus an output ceiling let you push hard while staying controlled.",
    features: [
      "Glue compressor for cohesion",
      "3-band tone + harmonic drive",
      "Stereo width, pan, parallel mix",
      "Output ceiling to stay in the green"
    ]
  },
  {
    id: "side-chain-master", name: "Side Chain Master", icon: "🔄",
    category: "Mix utility", cats: "Mix",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Precision ducking — musical pump or tight EDM sidechain.",
    what: "Side Chain Master ducks one signal to another with control. Get the classic pumping pad-under-kick feel, or a fast tight duck for EDM, without carving an envelope by hand.",
    how: "It shapes a ducking envelope from a trigger with adjustable attack, hold and release, plus a parallel blend so you keep some of the original level. An optical-style response gives musical pumping; a fast attack gives tight, rhythmic ducking.",
    features: [
      "Envelope-shaped ducking (attack / hold / release)",
      "Optical-style pump or fast EDM duck",
      "Parallel blend to keep body",
      "Reacts to the suite's transient bus"
    ]
  },
  {
    id: "sub-conjurer", name: "Sub Conjurer", icon: "⚡",
    category: "Low-end utility", cats: "Mix",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Club-ready sub and low-end that translates on any system.",
    what: "Sub Conjurer builds and controls the bottom end. Generate a clean sub, focus and mono the lows, and glide between notes so your bass hits hard on big systems and still reads on phones.",
    how: "It synthesizes harmonic sub content locked to the incoming bass (or a locked note that can follow the song key), blends it under a high-pass, and keeps the low end mono and focused. A glide control smooths note-to-note transitions for a solid, continuous bottom.",
    features: [
      "Harmonic sub generation under your bass",
      "Mono low-end focus for a tight bottom",
      "Lock note can follow the session key",
      "Glide for smooth note transitions"
    ]
  },
  {
    id: "warp", name: "Sattari Warp", icon: "🌀",
    category: "Creative vocal FX", cats: "Vocal",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Vocal transformer — one Dose knob takes you from subtle character to fully mangled.",
    what: "Warp is a creative vocal transformer built for damage in the best way. Bend the vocal tract, robotise it, dirty it up, and glitch it into stutters — from a subtle character move to a completely mangled, alien performance. One big Dose knob escalates the whole thing so you can automate 'clean → chaos' in a single move.",
    how: "It's a chain of real DSP blocks scaled together by the Dose macro: a morphable formant filter (two resonators sweeping A-E-I-O-U) for talkbox/vowel warp; ring modulation against a tunable carrier for metallic robot tones; a waveshaper drive plus bit/sample-rate crush for grit and lo-fi; and a tempo-synced beat-repeat stutter that captures a slice and loops it. Dry/wet blend keeps it mixable.",
    features: [
      "DOSE macro — one knob from subtle character to full chaos",
      "Vowel/formant morph (talkbox-style vocal-tract warp)",
      "Robot ring-mod with a tunable carrier",
      "Grit: waveshaper drive + bit / sample-rate crush",
      "Tempo-synced stutter / beat-repeat glitching",
      "Reactive audio-driven UI; full dry/wet blend"
    ]
  },
  {
    id: "tuner", name: "Sattari Tuner", icon: "🎯",
    category: "Tuning", cats: "Vocal",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Precision instrument & vocal tuner — big note readout with a steady strobe needle.",
    what: "Sattari Tuner is a pass-through tuner: drop it on any track and it shows the nearest note and exactly how many cents sharp or flat you are, without touching the audio. The needle stays steady thanks to smoothing, and it locks to whatever reference you set — from concert 440 to 432 or period tunings — so it fits guitars, voice, brass, strings and synths alike.",
    how: "It mono-sums the incoming signal and runs the same YIN pitch tracker that powers Auto Pitch, folded to the octave and smoothed with a one-pole so the readout doesn't jitter. Detected frequency is converted to a fractional MIDI note against your chosen Reference A, then split into the nearest note plus a signed cents deviation that drives the needle and the green in-tune zone. A Transpose control handles capos and transposing instruments. The audio path is untouched — pass-through is bit-exact.",
    features: [
      "Big note readout + strobe-style cents needle",
      "±5-cent in-tune zone with green lock glow",
      "Adjustable Reference A (415–466 Hz: 432, 440, period tunings)",
      "Transpose ±12 semitones (capo / transposing instruments)",
      "Adjustable smoothing for a steady vs snappy needle",
      "Bit-exact pass-through — safe anywhere in the chain"
    ]
  },
  {
    id: "align", name: "Sattari Align", icon: "🎼",
    category: "Timing alignment", cats: "Mix",
    formats: ["AU", "VST3", "Standalone"],
    tagline: "Phase-lock a layer to its guide — doubles, DI/amp, drum stems.",
    what: "Align nudges one signal into sample-accurate time with another. Line up a vocal double to the lead, a DI to a mic'd amp, or two drum stems, so layers reinforce instead of smearing.",
    how: "It computes the onset envelope of both the Main signal and a Guide sidechain and cross-correlates them to find the exact time offset, then shifts Main to match. Because it works on transients, it aligns any material with clear attacks.",
    features: [
      "Onset-envelope cross-correlation alignment",
      "Sample-accurate phase lock to a Guide",
      "Works on vocals, guitars, drum stems",
      "Fixes smeary doubles and multi-mic phase"
    ]
  }
];
