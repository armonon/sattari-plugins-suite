# Modular local qualification — 10 October 2026

The consolidated native source builds Modular FX and Modular Instrument as universal arm64/x86_64 AU, VST3 and Standalone bundles, using pinned JUCE 8.0.12 with plug-in copying and ONNX disabled.

Four processor/UI/hosting contracts passed on each architecture (Intel through Rosetta). All six bundles pass architecture and strict ad-hoc signature checks. Both final VST3 binaries pass official pluginval 1.0.4 at strictness 10 on each architecture. The attached JSON records their hashes and source provenance. The 32 Python release/packaging tests also pass.

Qualification exposed JUCE signing VST3 before generating moduleinfo.json. The shared CMake helper now seals the completed bundle afterward. Both VST3 targets were rebuilt and validated after that repair.

This is development qualification. Windows, AU registration, supported DAWs, listening acceptance, distribution licensing, Developer ID signing and notarization remain pending. No installer is published. GitHub Actions jobs were prevented from starting by the account payment/spending-limit failure; this does not establish Windows results. Earlier AutoPitch/R&D branches still require selective ports and regression qualification.
