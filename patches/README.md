# Vendored patches

## `tracktion_engine-juce8-fixes.patch`

JUCE 8 API-compatibility fixes for Tracktion Engine (9 files). Without them the
submodule does not compile against the JUCE version this repo pins — see commit
`4f0460d "Fix Tracktion Engine JUCE 8 API incompatibilities — clean build"`.

**Why this file exists:** these edits live as *uncommitted working-tree changes
inside the submodule*. Nothing in git tracks them, so any of the following wipes
them and breaks the build with confusing template errors:

```
git submodule update --force
git submodule deinit / update --init
a fresh clone of the repo
```

**Submodule base commit:** `0a5f4e6a5f53d09c89b414a44386a12df7fa1ec6`

### Re-applying after a fresh clone or a forced submodule update

```bash
git submodule update --init --recursive
cd third_party/tracktion_engine
git apply ../../patches/tracktion_engine-juce8-fixes.patch
```

### Regenerating this patch after further edits

```bash
git -C third_party/tracktion_engine diff > patches/tracktion_engine-juce8-fixes.patch
```

The durable fix is to push these to a Tracktion fork and point `.gitmodules` at
it; until then, this patch is the only backup.
