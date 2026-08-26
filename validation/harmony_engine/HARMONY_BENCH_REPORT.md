# Harmony Engine — Bench Report

## Voice Count Scaling

| Config | Frames | Illegal | Chatter% | Accuracy% | Throughput |
|--------|--------|---------|----------|-----------|------------|
| 1-voice +3rd | 600 | 0 | 0.0% | 100.0% | 123040 fps |
| 2-voice ±3rd | 600 | 0 | 0.0% | 100.0% | 67185 fps |
| 4-voice ±3rd ±5th | 600 | 0 | 0.0% | 100.0% | 34073 fps |
| 6-voice full | 600 | 0 | 0.0% | 100.0% | 23594 fps |

## Cross-Key Consistency

| Key | Scale | Illegal | Accuracy% |
|-----|-------|---------|-----------|
| C | major | 0 | 100.0% |
| A | minor | 0 | 100.0% |
| G | major | 0 | 100.0% |
| E | minor | 0 | 100.0% |
| F | major | 0 | 100.0% |
| D | minor | 0 | 100.0% |

## Summary
- Total illegal scale tones: 0
- All configs ≥ 95% accuracy: True
- Chatter acceptable: True

## Gate
```
HARMONY ENGINE RC
```