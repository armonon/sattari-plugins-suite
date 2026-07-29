# StemDeck real-music corpus

The audio corpus is intentionally not committed. Put licensed or privately owned
full-length files beside a version-1 `corpus.json` manifest and run:

```bash
cmake --build build/stemdeck --target StemDeckCorpusGate
./build/stemdeck/StemDeckCorpusGate /path/to/corpus.json build/stemdeck/corpus-report.json
```

The gate refuses generated fixtures and undersized collections. By default it
requires at least five truth-labelled `real_music` tracks totalling twenty
minutes. BPM, key and meter expectations must come from an independent trusted
annotation, not from StemDeck itself.

Use `corpus.example.json` as the schema template. Paths may be absolute or
relative to the manifest.
