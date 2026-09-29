# Sattari Mac And Windows Candidates

The five redesigned plugins share the embedded Sattari logo and existing
parameter/session formats. Arp is MIDI-only and has no standalone application.

| Platform | Plugins | Applications |
| --- | --- | --- |
| macOS Universal (Apple Silicon + Intel) | AU and VST3 | Four .app bundles |
| Windows x64 | VST3 | Four .exe applications |

Close your DAW before installing. On Mac, run `Install-macOS.command`. On
Windows, open PowerShell in the extracted package and run
`& ./Install-Windows.ps1`. This installs for the current user without requiring
administrator privileges. For a system-wide installation, run the script from
an elevated PowerShell window with `-Scope AllUsers`. Do not change system-wide
security policies to run an unsigned test package.

The Windows user VST3 folder is `%LOCALAPPDATA%/Programs/Common/VST3`; the
system-wide folder is `%CommonProgramFiles%/VST3`, as documented in the
[VST3 location specification](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Locations.html).
Restart/rescan the DAW and check that its scan paths include the installation
folder. Standalone apps are in `%LOCALAPPDATA%/Programs/Sattari` or
`%ProgramFiles%/Sattari`. The Windows installer backs up replaced files.

Windows x64 binaries require a compatible 64-bit Windows host. They are not
native Windows ARM64 builds. AU is an Apple format and is not included on Windows.
Cross-platform parameter formats do not guarantee that a DAW will automatically
replace an AU instance with a VST3 instance when moving a project between OSes.

## Build From Source

Extract `source-snapshot.tar.gz`. With CMake and the native C++ toolchain installed:

```sh
# macOS with Xcode command-line tools and Ninja
cmake -S packaging/candidates -B build/candidates -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/candidates --target SattariCandidateBuild --parallel 2
ctest --test-dir build/candidates --output-on-failure
```

```powershell
# Windows with Visual Studio 2022 C++ workload and Windows SDK
cmake -S packaging/candidates -B build/candidates -G "Visual Studio 17 2022" -A x64
cmake --build build/candidates --config Release --target SattariCandidateBuild --parallel 2
ctest --test-dir build/candidates -C Release --output-on-failure
```

`manifest.json` records the platform, versions, source revision and file hashes.
Native test evidence is in `validation`. The source archive includes the vendored
JUCE and Rubber Band dependencies and their notices. Windows and Mac screenshots
may differ slightly because native fonts are used on each platform.

## Release Status

These are internal candidates, not notarized or commercially release-qualified
installers. Manual DAW testing, listening, clean-machine installation, signing,
and license review remain required. Windows evidence must come from a Windows
build; Mac validation alone does not qualify Windows binaries.
