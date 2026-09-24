"""Reference pitch tracking (CREPE) and note segmentation shared by the accuracy tests."""
import numpy as np, soundfile as sf, torch, torchcrepe

SR = 48000
HOP_S = 0.01  # 10 ms


def load(path):
    y, sr = sf.read(str(path), dtype="float32", always_2d=False)
    if y.ndim > 1:
        y = y.mean(axis=1)
    assert sr == SR, (path, sr)
    return y


def f0(y, fmin=60.0, fmax=1100.0, model="full"):
    """Returns (midi, periodicity) at 10 ms hops; midi is NaN where unvoiced."""
    import librosa
    y16 = librosa.resample(y, orig_sr=SR, target_sr=16000)
    audio = torch.tensor(y16)[None]
    pitch, per = torchcrepe.predict(audio, 16000, hop_length=160, fmin=fmin, fmax=fmax, model=model,
                                    decoder=torchcrepe.decode.viterbi, return_periodicity=True,
                                    batch_size=2048, device="mps")
    per = torchcrepe.filter.median(per, 3)
    pitch, per = pitch[0].numpy(), per[0].numpy()
    midi = 69 + 12 * np.log2(pitch / 440.0)
    rms = np.array([np.sqrt(np.mean(y16[i * 160:i * 160 + 1024] ** 2) + 1e-12) for i in range(len(midi))])
    voiced = (per > 0.5) & (rms > 10 ** (-45 / 20))
    return np.where(voiced, midi, np.nan), per


def notes(midi, min_len=12, max_jitter=0.6):
    """Segments held notes: runs of voiced frames staying within the same rounded
    semitone (with hysteresis) for >= min_len frames (120 ms). Returns a list of
    (start_frame, end_frame_exclusive, intended_midi, singer_median_midi)."""
    out, i, n = [], 0, len(midi)
    while i < n:
        if not np.isfinite(midi[i]):
            i += 1
            continue
        target = round(midi[i]); j = i
        while j < n and np.isfinite(midi[j]) and abs(midi[j] - target) < max_jitter:
            j += 1
        if j - i >= min_len:
            seg = midi[i:j]
            med = float(np.median(seg))
            out.append((i, j, int(round(med)), med))
        i = max(j, i + 1)
    return out
