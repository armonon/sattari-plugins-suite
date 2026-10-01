# Build And Install

Source checkout: `/Users/lillypad/Projects/sattari-plugins-main` on the build machine.
This internal package comes from a working tree; the Git commit alone is not a
complete description of its source. The `plugins/mix` folder contains the product
changes, and `core/include/sattari_audio_core/SattariRackProcessor.h` contains the
append-only parameter extension used by Mix. JUCE is the repository submodule.

```sh
cmake -S plugins/mix -B build/mix -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build/mix --target SattariMix_AU SattariMix_VST3 SattariMix_Standalone SattariMixProductContract SattariMixAutoMixContract -j 1
build/mix/SattariMixProductContract
build/mix/SattariMixAutoMixContract "$PWD/build/mix/screenshots"
bash plugins/mix/scripts/install_user_plugins.sh build/mix/SattariMix_artefacts/Release
bash scripts/validate_plugin.sh build/mix/SattariMix_artefacts/Release aumf Mx01 Satr
```

For the packaged build, run `bash scripts/install_user_plugins.sh` from its root.
Then run `bash scripts/validate_local.sh . aumf Mx01 Satr` to verify signatures,
the installed AU, and the packaged VST3. Restart/rescan the DAW after installation.

The September 2026 local build can also be reproduced with the cached JUCE build
helper using the CMake entry point at `build/mix-workspace/CMakeLists.txt` and
output directory `build/mix-automix`. It uses the same source modules, compiler
and product CMake target; it avoids configuring the other plugins in the suite.

Machine validation does not replace human DAW listening or release qualification.
