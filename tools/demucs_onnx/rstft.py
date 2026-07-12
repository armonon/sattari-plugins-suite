"""Real-valued, ONNX-exportable STFT/ISTFT that exactly matches torch.stft/istft
with: window=hann(n_fft), win_length=n_fft, center=True, pad_mode='reflect',
normalized=True, return_complex. Implemented via Conv1d (analysis) and matmul +
ConvTranspose1d overlap-add (synthesis) — only real ops, no aten::stft / complex.
"""
import math, torch
import torch.nn.functional as F


def _dft_basis(n_fft):
    n = torch.arange(n_fft, dtype=torch.float64)
    k = torch.arange(n_fft // 2 + 1, dtype=torch.float64)
    ang = -2.0 * math.pi / n_fft * (k[:, None] * n[None, :])   # [F, n_fft]
    return torch.cos(ang), torch.sin(ang)


def spectro_real(x, n_fft, hop):
    """x: [B, L] -> (re, im) each [B, F, T], matching view_as_real(torch.stft(...))."""
    win = torch.hann_window(n_fft, dtype=torch.float64).to(x.dtype)
    cos, sin = _dft_basis(n_fft)
    cos = cos.to(x.dtype); sin = sin.to(x.dtype)
    # Conv1d kernels: [F, 1, n_fft] = window * basis
    kre = (cos * win[None, :]).unsqueeze(1).to(x.dtype)
    kim = (sin * win[None, :]).unsqueeze(1).to(x.dtype)
    pad = n_fft // 2
    xp = F.pad(x.unsqueeze(1), (pad, pad), mode='reflect')     # center pad [B,1,L+2pad]
    re = F.conv1d(xp, kre, stride=hop)                         # [B, F, T]
    im = F.conv1d(xp, kim, stride=hop)
    norm = 1.0 / math.sqrt(n_fft)                             # normalized=True
    return re * norm, im * norm


def ispectro_real(re, im, n_fft, hop, length):
    """(re, im) [B,F,T] -> x [B, length]. Matches torch.istft(normalized=True,center=True)."""
    B, Fb, T = re.shape
    win = torch.hann_window(n_fft, dtype=torch.float64).to(re.dtype)
    # Inverse DFT (real signal from half spectrum). For n in 0..N-1:
    #   x[n] = (1/N) * sum_{k=0}^{N-1} X[k] e^{+j 2pi k n /N}, X hermitian.
    # Using only k=0..N/2 with weights w_k (1 for k=0 and k=N/2, else 2):
    n = torch.arange(n_fft, dtype=torch.float64)
    k = torch.arange(Fb, dtype=torch.float64)
    ang = 2.0 * math.pi / n_fft * (n[:, None] * k[None, :])    # [n_fft, F]
    wk = torch.full((Fb,), 2.0, dtype=torch.float64)
    wk[0] = 1.0
    if n_fft % 2 == 0:
        wk[-1] = 1.0
    icos = (torch.cos(ang) * wk[None, :] / n_fft).to(re.dtype)  # [n_fft, F]
    isin = (torch.sin(ang) * wk[None, :] / n_fft).to(re.dtype)
    # frames_time[b,t,n] = sum_k icos[n,k]*re[b,k,t] - isin[n,k]*im[b,k,t]
    ret = re.transpose(1, 2)   # [B,T,F]
    imt = im.transpose(1, 2)
    frames = ret @ icos.T - imt @ isin.T                       # [B,T,n_fft]
    frames = frames * math.sqrt(n_fft)                         # undo normalized on synth
    frames = frames * win[None, None, :]                       # synthesis window
    # Overlap-add via ConvTranspose1d: input [B,n_fft,T], eye kernel -> fold each
    # frame's taps to t*hop+n.
    frames_c = frames.transpose(1, 2)                          # [B, n_fft, T]
    eye = torch.eye(n_fft, dtype=frames.dtype).unsqueeze(1)    # [n_fft,1,n_fft]
    y = F.conv_transpose1d(frames_c, eye, stride=hop).squeeze(1)   # [B,(T-1)*hop+n_fft]
    # Window normalization: OLA of win^2 (input ones [B,1,T], kernel [1,1,n_fft]).
    win2 = (win * win).reshape(1, 1, n_fft).to(frames.dtype)
    ola = F.conv_transpose1d(torch.ones(B, 1, T, dtype=frames.dtype), win2, stride=hop).squeeze(1)
    y = y / (ola + 1e-8)
    pad = n_fft // 2
    y = y[:, pad: pad + length]
    return y


if __name__ == "__main__":
    torch.manual_seed(0)
    n_fft, hop = 4096, 1024
    x = torch.randn(2, 300000, dtype=torch.float32) * 0.2
    # reference
    z = torch.stft(x, n_fft, hop, win_length=n_fft,
                   window=torch.hann_window(n_fft), normalized=True,
                   center=True, return_complex=True, pad_mode='reflect')
    zr = torch.view_as_real(z)
    re, im = spectro_real(x, n_fft, hop)
    d_re = (re - zr[..., 0]).abs().max().item()
    d_im = (im - zr[..., 1]).abs().max().item()
    print(f"STFT  max|dre|={d_re:.3e}  max|dim|={d_im:.3e}  shape={tuple(re.shape)} ref={tuple(z.shape)}")
    # istft round-trip
    xr = torch.istft(z, n_fft, hop, win_length=n_fft, window=torch.hann_window(n_fft),
                     normalized=True, center=True, length=x.shape[-1])
    xr2 = ispectro_real(re, im, n_fft, hop, x.shape[-1])
    d_ix = (xr - xr2).abs().max().item()
    print(f"ISTFT max|d(torch,mine)|={d_ix:.3e}  shapes {tuple(xr.shape)} {tuple(xr2.shape)}")
    print("STFT_OK" if max(d_re, d_im) < 2e-4 else "STFT_FAIL",
          "ISTFT_OK" if d_ix < 2e-3 else "ISTFT_FAIL")
