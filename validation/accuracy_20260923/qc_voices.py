import json, numpy as np
from pathlib import Path
from pitchref import load, f0, notes
rows = {}
for wav in sorted(Path('voices').glob('v*.wav')):
    y = load(wav); m, per = f0(y)
    nt = notes(m)
    voiced = np.isfinite(m)
    offs = [100 * (med - tgt) for (_, _, tgt, med) in nt]
    rows[wav.stem] = dict(voiced_pct=round(100 * voiced.mean(), 1), median_hz=round(float(440 * 2 ** ((np.nanmedian(m) - 69) / 12)), 1) if voiced.any() else None,
                          n_notes=len(nt), median_periodicity=round(float(np.median(per[voiced])), 2) if voiced.any() else None,
                          singer_abs_offset_c=round(float(np.median(np.abs(offs))), 1) if offs else None,
                          notes=[(a, b, t, round(md, 3)) for a, b, t, md in nt])
    r = rows[wav.stem]
    print(f"{wav.stem:32} voiced {r['voiced_pct']:5.1f}% | f0 {r['median_hz']} Hz | notes {r['n_notes']:3d} | periodicity {r['median_periodicity']} | singer off {r['singer_abs_offset_c']}c", flush=True)
json.dump(rows, open('voices/reference_notes.json', 'w'))
