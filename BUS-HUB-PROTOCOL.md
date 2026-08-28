# Sattari — Bus & Hub Protocol Notes (Phase 0)

## Part A — Cross-plugin Bus

**Header:** `core/include/sattari_audio_core/SattariCrossPluginBus.h`
**Storage:** `<userAppData>/Sattari Audio/cross-plugin/` → two JSON channels:
- `key-bus.json` — key/scale (legacy `KeyMessage`)
- `tempo-bus.json` — tempo/transient (`TempoMessage`), a separate file so key and tempo never clobber each other.

### Current schema
| Message | Fields | Publish API | Read API |
|---|---|---|---|
| `KeyMessage` | `root`, `scale`, `publisherId`, `timestamp` | `publishKey(id, root, scale)` | `readPublishedKey()` |
| `SessionMessage` | `root`, `scale`, `bpm`, `transientId` | `publishSession(id, root, scale, bpm, tid)` | `SessionReader` |
| `TempoMessage` | `bpm`, `transientId`, `hasTempo` | `publishTempo(id, bpm, tid)` | `TempoReader` |

**Key finding:** the bus was *already* extended once (key → `SessionMessage` carrying key **+ bpm + transient**), so a
superset message with backward-compat is an **established pattern**, not new ground.

### Publishers → Subscribers (live adoption)
- **Publishers:** Sattari Key (authoritative), Auto Pitch (AutoKey), Rhythm Genie (tempo), Granulator (key broadcast).
- **Subscribers (21):** auto-pitch, comp, echo, 808, gate, glass-choir, granulator, harmony, maqam, pulse, riser,
  royal-chain, arp, scaler, scope, shimmer, side-chain-master, space, sub-conjurer, vocoder, voxkey.
  → **Over half the suite already follows the bus** — the Brain hook is real, not aspirational.

### Real-time safety (preserve this)
`SessionReader`/`TempoReader` are background `juce::Thread`s that poll the JSON and `store()` into `std::atomic`s;
the audio thread only reads a lock-free `current()` snapshot. Publishers write JSON on the **message/timer thread**,
throttled. **No file I/O, allocation, or locks on the audio thread.** Any extension must keep this shape.

### Backward-compatible path to full SessionState (Phase 1)
The JSON is parsed with `getProperty(field, default)` — **missing fields default safely**, so *adding* fields cannot
break existing readers. The extension is purely additive:

1. Add properties to the `SessionMessage` object + `SessionReader` atomics (all with safe defaults):
   `chordRoot`, `chordQuality`, `progression`, `timeSigNum/Den`, `swing`, `sectionId`, `energy`, `maqamId`, `referenceHz`.
2. Add an explicit **`schemaVersion` int** now (currently implicit) so future readers can branch cleanly.
3. Old key-only readers ignore unknown props and keep working; new readers opt into the fields they need.

**Verdict: YES — the bus extends to full SessionState backward-compatibly, with no break to the 21 current subscribers.**
Prove it (Phase-1 gate): Harmony follows `chord/progression`, Arp follows `bpm/section` — set them in Brain, watch them update live.

## Part B — Hub catalog

**Manifest:** `apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json`
**Top-level:** `schemaVersion, suiteName, suiteVersion, truthLabel, defaultUpdateChannel, products[], updateFeedURL`
**Per-product fields:** `id, name, category, accent, version, status, description, formats, bundles[], updateArchiveURL`

**Two findings:**
1. The manifest currently lists **25 products** — it's **stale** (missing the 16 newest plugins). It needs a refresh regardless.
2. **`bundles[]` already lets one product contain many plugin bundles.** The 40→6 repackage needs **no schema change** —
   it's a manifest restructure.

### 40 → 6 repackage plan (presentation only, no code merge)
1. Rewrite `sattari-products.json` to **6 product entries** (Brain/Vocal/Create/Composer/Mix/StemDeck); each product's
   `bundles[]` = the module list from [INVENTORY.md](INVENTORY.md).
2. Update the Swift Hub UI to group its grid by product (it already renders `products[]` + accent).
3. Update the site (`apps/sattari-web/plugins-data.js` + `index.html`) to a 6-product grid with the modules listed underneath;
   drop the "N plugins" count hero (per anti-goal: stop marketing a plugin count).
4. **Zero plugin code changes.** Ships independently of, and before, the engine extraction.

**Gate:** Hub + site present 6 products; every one of the 41 plugins is reachable inside exactly one product.
