# Sattari Audio Licensing

## Distribution Is Pending Review

The repository's `LICENSE` currently contains GPLv3. It has not been changed.
The earlier statement that GPLv3 alone covers the pinned JUCE version was
incorrect: `third_party/JUCE/LICENSE.md` describes JUCE 8 modules as dual-licensed
under **AGPLv3 or the commercial JUCE license**. See the upstream
[JUCE license](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md) and
[JUCE 8 agreement](https://juce.com/legal/juce-8-licence/).

Do not treat this file, a successful build, or an included source archive as
approval to distribute. The owner must confirm the actual license arrangement
and have the combined distribution terms reviewed before external beta or sale.
This includes any necessary permission to change the project's license; no
relicensing has been performed here.

## Dependency Inventory

| Dependency | Upstream terms / notice | Distribution action |
|---|---|---|
| JUCE 8 | `third_party/JUCE/LICENSE.md`: AGPLv3 or commercial | Confirm chosen license and corresponding obligations |
| JUCE bundled SDKs, codecs and fonts | VST3, AudioUnitSDK, FLAC, Ogg/Vorbis, JPEG, PNG, zlib, HarfBuzz, SheenBidi | Preserve their separate notices; the top-level JUCE license is not a substitute |
| Rubber Band | `third_party/rubberband/COPYING`: GPLv2-or-later or commercial | Confirm terms for products linking it, including Auto Pitch |
| ONNX Runtime | Pinned SDK `LICENSE` and `ThirdPartyNotices.txt` | Include both notices when shipping its runtime |
| CREPE-tiny model | `plugins/auto-pitch/models/CREPE-LICENSE.txt` and `model-release.json` | Include provenance and license; verify embedded model hashes |
| Lucide assets | `assets/icons/LICENSE.lucide` | Preserve the notice |
| JUCE-bundled dependencies | Listed in `third_party/JUCE/LICENSE.md` | Preserve applicable upstream notices |
| ARA SDK, when enabled | Notices in `third_party/ARA_SDK` | Include applicable terms for the exact enabled SDK |

Commercial JUCE licensing does not by itself settle Rubber Band, model, SDK,
or other third-party obligations. Consult the exact upstream texts and obtain
qualified advice on the chosen distribution model.

## Release Requirements

- Record the approved licensing decision for the exact candidate and dependencies.
- Preserve upstream notices and include all notices for linked or embedded assets.
- When applicable, provide complete corresponding source for the exact binaries,
  including local modifications, build scripts, and dependency versions.
- Do not add a restrictive EULA that conflicts with applicable open-source terms.
- Review JUCE compile-time branding/splash settings under the chosen license.

Developer ID signing and notarization establish platform trust, not license
compliance. Until this review is recorded, packages are internal candidates and
public distribution remains blocked.

This is an engineering release checklist, not legal advice.
