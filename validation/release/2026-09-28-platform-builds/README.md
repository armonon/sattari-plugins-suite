# Mac And Windows Candidate Builds

Scope: Sattari Arp, Sub Conjurer, Mix, Vocal and Create, with the redesigned UI
and embedded Sattari logo. Arp/Sub/Vocal/Create are 0.1.1; Mix is 0.2.1.

## macOS

Package: `deliverables/sattari-platform-builds/Sattari-Candidates-macos-universal.zip`
(77 MB). Final archive verification passed: 165 payload SHA-256 hashes, all
14 executable bundles, five products, installer executable mode and ZIP CRCs.

Archive SHA-256: `782ab88210b1042609171412sop exacly what plugins come native with the aud075344fedc2c7ab4cfefa7ad82045de8a20d3d44`.

Universal AU/VST3 and four standalone applications were built and validated in
the [UI qualification](../2026-09-28-ui-overhaul/README.md). This platform pass
repackages those exact binaries, rechecking both architecture slices, versions
and ad-hoc signatures. No new Mac DSP build is claimed. The package contains
an installer command, source snapshot, licenses, native validation evidence and
a SHA-256 manifest. The current working tree is identified as dirty in the
manifest; the Git revision alone is not the complete source description.

## Windows: Blocked Before Build

The native Windows x64 workflow was submitted to the private repository:
[Windows candidate run](https://github.com/armonon/sattari-plugins-suite/actions/runs/36530442179).
GitHub refused to start the runner because recent payments had failed or the
account's spending limit needed attention. The job has no executed steps.
**No Windows binaries have been built or validated.**

Build snapshot: `c9f14f7b70841f0d0781bb9dc784a8ba1001b875`, on
`codex/release-candidates-macos-windows-20260928`. The user's `main` checkout and
index were not switched, committed or pushed by this operation. The isolated
snapshot includes the existing uncommitted shared sources used by the candidates.

After the account restriction is resolved, rerun that job in GitHub Actions.
Alternatively, build on a Windows x64 machine using the commands in
[the platform build guide](../../../packaging/candidates/README.md).

The workflow builds five VST3 plugins and four EXEs using Visual Studio 2022,
runs 12 processor/UI contracts (including logo visibility), validates all five
VST3s using checksum-pinned pluginval 1.0.4 at strictness 5, and only then packages
the binaries. CI artifacts remain private and are retained for 30 days.

## Implemented And Checked

- Platform-aware formats; no AU target or privileged copy-after-build on Windows.
- Native Windows typography and embedded Sattari logo; original parameter IDs.
- Static MSVC runtime selection for the candidate build.
- Windows installer with payload checksums, current-user/system scopes and backups.
- Packaging rejects non-x64 PE binaries, incomplete test reports and skipped tests.
- Four packaging unit tests passed locally. Candidate CMake configuration passed
  on macOS and registered all 12 contracts. These checks do not replace a Windows build.

Manual DAW/listening tests, clean-machine installation, release signing,
notarization where applicable and license review remain separate release gates.
