# Modular Mac signing and notarization — October 11, 2026

Internal candidate only. The owner selected planned open-source distribution and authorized Mac signing/notarization. No public installer or download was published.

All six Modular 0.2.0 AU/VST3/Standalone bundles were copied into isolated staging from the previously qualified universal build. Their pre-sign executable hashes exactly match `../modular-2026-10-10/qualification.json`. The original build and installed plug-in folders remain untouched.

Developer ID Application signing uses hardened runtime, a secure timestamp and the existing product entitlements. Apple accepted the six-bundle ZIP, submission `8ea74f4c-042f-4387-a279-d1cc74929931`. Both standalone apps are stapled and pass ticket validation and Gatekeeper.

The signed DMG was independently accepted as submission `b818787b-8867-44b7-ac1d-b34eaacd5211`, stapled, validated and accepted by Gatekeeper. Both apps inside the read-only mounted DMG pass strict signature, ticket and Gatekeeper checks. Final DMG SHA-256: `8461319a9087ec8d1bef87040396c89fb2eb1ba59ef0494ac847a2f157070988`.

A Developer ID Installer certificate is unavailable; this candidate is a DMG, not a signed PKG. Signing/notarization is trust evidence, not product acceptance. Supported DAWs, listening, installation/update/removal, minimum-OS and complete corresponding-source/notice qualification remain open. ONNX is disabled in these exact binaries. The open-source direction is recorded in `LICENSING.md`; third-party terms remain intact.

Retained local artifacts and signing scripts: `/Users/lillypad/Projects/remaining-app-releases-20261011/modular-signing`. All four signed VST3 checks pass pluginval strictness 10 (arm64 and Intel through Rosetta). All four AU validation checks pass on those architectures; temporary registration restores prior components. Logs and hashes accompany this record. The separate 4,658-file source/build-input archive is retained locally with per-file hashes; redistribution completeness still requires review.
