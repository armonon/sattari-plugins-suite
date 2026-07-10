# Sattari Suite — Inventory (Phase 0)

**41 plugins**, audited from source. This maps every plugin onto the **6-product** consolidation
(Brain · Vocal · Create · Composer · Mix · StemDeck). The 41 become *modules* underneath the 6
products — a presentation/bundling change, not a code merge (see [BUS-HUB-PROTOCOL.md](BUS-HUB-PROTOCOL.md)).

Legend: **Code** = AU subtype · **Bus** = R(reads) / P(publishes) the cross-plugin bus · **Syn** = instrument.

## 🧠 Brain (free) — session context
> The hook. Detects/edits/publishes session state; everything else follows. Chord/Pulse/Reference are Phase-1 additions that fold in here.

| Plugin | slug | Code | Bus | What |
|---|---|---|---|---|
| Sattari Key | song-key | Sk01 | **P** | Master-bus 24-key detector; the authoritative publisher |
| Sattari Bus | bus | Bus1 | — | Cross-plugin bus utility / glue + oversampled drive |
| Sattari Tuner | tuner | Tn01 | — | YIN reference-pitch tuner (→ Reference module) |

## 🎤 Vocal (flagship) — "finish the vocal"
| Plugin | slug | Code | Bus | What |
|---|---|---|---|---|
| Sattari Auto Pitch | auto-pitch | Ap01 | **R/P** | Real-time correction (YIN/CREPE + Rubber Band), AutoKey |
| Sattari Harmony | harmony | Hm01 | R | Key-aware diatonic harmony voices |
| Sattari Maqam | maqam | Mq01 | R | Microtonal / maqam correction |
| Sattari Vocal Master | vocal-master | Vm01 | — | Vocal cleanup / isolation chain |
| Sattari Royal Chain | royal-chain | Rc01 | R | Full vocal channel strip (macros) |
| Sattari VoxKey* | voxkey | Vk01 | R | Voice→MIDI (*also Composer) |
| Sattari Double | double | Db01 | — | Vocal doubler (timing/detune drift) |
| Sattari Face | face | Fc01 | — | Formant / gender shifter (RubberBand) |
| Sattari De-Ess | deess | Ds01 | — | Dynamic de-esser |
| Sattari Glass Choir | glass-choir | Gc01 | R | Vocal ensemble + HUMAN→MASSIVE→DESTROYED |

## ✨ Create — creative FX / sound design
| Plugin | slug | Code | Bus | What |
|---|---|---|---|---|
| Sattari Warp | warp | Wp01 | R | Creative vocal transformer (vowel/ring/crush/stutter) |
| Sattari Granulator | granulator | Gr01 | R/Syn | Granular instrument (2-layer, MPE, formant) |
| Sattari Space | space | Sp01 | R | Reverb + tempo delay |
| Sattari Echo | echo | Ech1 | R | Tempo-locked ping-pong delay |
| Sattari Shimmer | shimmer | Shm1 | R | Pitch-shifted shimmer reverb (key-snap) |
| Sattari Heat | heat | Ht01 | — | Saturation / tone box |
| Sattari Warble | warble | Wrb1 | — | Tape / lo-fi character |
| Sattari Pulse | pulse | Pl01 | R | Tempo-drawn pump / gate |
| Sattari Riser | riser | Rs01 | R | Tempo-synced riser / downlifter |
| Sattari 808 | eightoheight | S808 | R | Audio-follow 808 / sub designer |
| Sattari Vocoder | vocoder | Voc1 | R/Syn | Carrier-synthesis vocoder (key-voiced) |

## 🎹 Composer — MIDI / generative
| Plugin | slug | Code | Bus | What |
|---|---|---|---|---|
| Sattari Arp | sattari-arp | Ar01 | R | Key/tempo-aware arpeggiator |
| Sattari Rhythm Genie | rhythm-genie | Rg01 | **P** | Drum/groove generator (publishes tempo) |
| Sattari Scaler | scaler | Skl1 | R | Key-following chord MIDI effect |
| *(VoxKey → also here as voice→MIDI)* | voxkey | Vk01 | R | Voice→MIDI |

## 🎛 Mix — mix / master utilities
| Plugin | slug | Code | Bus | What |
|---|---|---|---|---|
| Sattari EQ | eq | Eq01 | — | Dynamic / surgical EQ |
| Sattari Comp | comp | Cmp1 | R | Compressor (tempo-locked release) |
| Sattari Multiband | multiband | Mb01 | — | 3-band multiband compressor |
| Sattari Gate | gate | Gt01 | R | Gate / expander (tempo-gate) |
| Sattari Transient | transient | Ts01 | — | Attack / sustain shaper |
| Sattari Meter | meter | Mt01 | — | LUFS / true-peak metering |
| Sattari Peak | peak | Pk01 | — | True-peak limiter / maximizer |
| Sattari Side Chain Master | side-chain-master | Sc01 | R | Ducker / tempo pump |
| Sattari Sub Conjurer | sub-conjurer | Sub1 | R | Sub-bass generator |
| Sattari Imager | imager | Img1 | — | Mono-safe stereo imager |
| Sattari Align | align | Al01 | — | Phase / time alignment (cross-corr) |
| Sattari Clean | clean | Cl01 | — | Spectral denoise (3-band expander) |
| Sattari Scope | scope | Scp1 | R | Spectrum / correlation analyzer |

## 🥁 StemDeck (standalone app + plugin)
| Plugin | slug | Code | Bus | What |
|---|---|---|---|---|
| Sattari StemDeck | stemdeck | Sd01 | — | ONNX/Demucs stem separation *(model pending — owner-tracked)* |

---

### Product counts
Brain 3 · Vocal 10 · Create 11 · Composer 4 (VoxKey shared with Vocal) · Mix 13 · StemDeck 1 → **41 unique**.

### Notes / judgement calls
- **VoxKey** spans Vocal (it's a vocal tool) and Composer (it emits MIDI). List under both; ships in whichever bundle, wired once.
- **Vocoder / 808** are creative instruments → placed in **Create**; arguable as Composer instruments.
- **Deess / Imager / Align / Scope** are borderline Vocal↔Mix; placed in Mix as engineering utilities.
- Brain currently = Key + Bus + Tuner; **Chord, Pulse, Reference** are Phase-1 modules that absorb into Brain when the SessionState bus lands.
