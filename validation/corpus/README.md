# Vocal / audio corpus

**No audio lives in this directory or anywhere in git.** Manifests reference
absolute paths outside the repository, matching the policy already stated in
`validation/auto-pitch/corpus-manifest.example.json`:

> Only owned, cleared, or public-domain real vocals. Store audio outside git and
> use absolute paths.

This directory holds **manifests and results**, never source material.

## Status

`auto-pitch` requires `minimum_real_clip_count: 5` per
`validation/schemas/auto_pitch_corpus_manifest.schema.json`. As of the last
check there are **zero real clips** and only the example manifest exists, so
every Auto Pitch quality figure to date comes from synthetic signals.

Per the flagship rule in `sattari-production`, the Auto Pitch corpus should
cover: male, female, low, high, breathy, vibrato, falsetto, rap, legato,
slides, short notes, noisy, transparent tuning, hard tuning, and microtonal.

## Using it

    cp validation/auto-pitch/corpus-manifest.example.json validation/corpus/auto-pitch.json
    # edit: absolute paths, permission_status, vocal_tags
    scripts/qualify_auto_pitch_corpus.py validation/corpus/auto-pitch.json
