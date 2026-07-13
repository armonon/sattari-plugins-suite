#!/usr/bin/env python
# Train the shippable key-net on the PLUGIN's HPCP feature (chroma_cov.csv: mean(12) +
# cov upper-tri incl diag(78) = 90). 12x pitch-rotation aug, track-level 5-fold CV,
# ONNX export. Input 90-dim, output 24 logits (root*2+minor).
import sys, numpy as np, torch, torch.nn as nn
torch.manual_seed(0); np.random.seed(0)
CSV = sys.argv[1] if len(sys.argv) > 1 else "validation/giantsteps/chroma_cov.csv"
OUT = sys.argv[2] if len(sys.argv) > 2 else "keynet.onnx"

rows = np.loadtxt(CSV, delimiter=",")
F = rows[:, :90].astype(np.float32)
root = rows[:, 90].astype(int); minor = rows[:, 91].astype(int)
N = len(F); print(f"{N} tracks, feat dim {F.shape[1]}")

iu = np.triu_indices(12)   # 78 incl diagonal, matches the C++ dump order (i..j for j>=i)
def rot(f, k):
    mean = np.roll(f[:12], k)
    cov = np.zeros((12, 12)); cov[iu] = f[12:]; cov = cov + cov.T - np.diag(np.diag(cov))
    cov = np.roll(np.roll(cov, k, 0), k, 1)
    return np.concatenate([mean, cov[iu]]).astype(np.float32)
def augment(idx):
    xs, ys = [], []
    for k in range(12):
        for t in idx: xs.append(rot(F[t], k)); ys.append(((root[t]+k) % 12)*2 + minor[t])
    return np.array(xs, np.float32), np.array(ys)
def mw(pr,pm,tr,tm):
    if pr==tr and pm==tm: return 1.0
    if pr==(tr+7)%12 and pm==tm: return 0.5
    if pm==0 and tm==1 and pr==(tr+3)%12: return 0.3
    if pm==1 and tm==0 and pr==(tr+9)%12: return 0.3
    if pr==tr and pm!=tm: return 0.2
    return 0.0

class Net(nn.Module):
    def __init__(s):
        super().__init__(); s.n = nn.Sequential(
            nn.Linear(90,256), nn.ReLU(), nn.Dropout(.4),
            nn.Linear(256,128), nn.ReLU(), nn.Dropout(.4), nn.Linear(128,24))
    def forward(s,x): return s.n(x)

dev = "mps" if torch.backends.mps.is_available() else "cpu"
def fit(trI, epochs=150):
    Xa, ya = augment(trI); Xa = torch.tensor(Xa, device=dev); ya = torch.tensor(ya, device=dev)
    m = Net().to(dev); opt = torch.optim.Adam(m.parameters(),1e-3,weight_decay=1e-4); lf = nn.CrossEntropyLoss(); m.train()
    for ep in range(epochs):
        p = torch.randperm(len(Xa), device=dev)
        for i in range(0,len(Xa),256):
            b=p[i:i+256]; opt.zero_grad(); lf(m(Xa[b]),ya[b]).backward(); opt.step()
    return m.eval()

idx = np.arange(N); np.random.shuffle(idx); folds = np.array_split(idx,5)
tw=te=tn=0
for f in range(5):
    teI=folds[f]; trI=np.concatenate([folds[j] for j in range(5) if j!=f]); m=fit(trI)
    with torch.no_grad(): pred=m(torch.tensor(F[teI],device=dev)).argmax(1).cpu().numpy()
    w=sum(mw(p//2,p%2,root[t],minor[t]) for p,t in zip(pred,teI)); ex=sum(1 for p,t in zip(pred,teI) if p//2==root[t] and p%2==minor[t])
    tw+=w; te+=ex; tn+=len(teI); print(f"  fold {f}: MIREX {100*w/len(teI):.1f}  exact {100*ex/len(teI):.1f}%")
print(f"\n== HPCP cov key-net 5-fold CV: MIREX {100*tw/tn:.2f}  exact {100*te/tn:.1f}%  (n={tn}) ==")
print(f"   (Aarden profile = 52.4 / 45% ; librosa-cov = 63.5 / 57.7%)")

final = fit(idx, epochs=200)
torch.onnx.export(final.cpu(), torch.zeros(1,90), OUT, input_names=["feat"], output_names=["key_logits"], opset_version=13, dynamo=False)
print(f"exported -> {OUT}")
