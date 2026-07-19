#!/usr/bin/env python3
# Self-trained SPECTROGRAM key model on the C++-reproducible pseudo-CQT feature.
#
#   teacher : chroma mean+cov MLP (+12-rotation TTA) trained on the TRUE corpus
#             (GiantSteps + GTZAN) -> pseudo-labels a large unlabeled CC corpus
#             (FMA), keeping only high-confidence tracks.
#   student : CNN over the pseudo-CQT log-spectrogram [84 x 256], trained on
#             TRUE + pseudo, evaluated 5-fold on TRUE labels only.
#
# The student beats its teacher because the spectrogram is a richer feature than
# chroma and the pseudo-labels supply the volume it needs (see key-model memory).
#
# Inputs (built by scripts/pcqt_featurize.py + build_key_corpus.py):
#   validation/pcqt_feats.npz        gs/gt/fma pseudo-CQT [N,84,256]
#   validation/giantsteps/grams.npz  + validation/gtzan/feats.npz  (teacher chroma)
#   validation/fma/fma_feats.npz     (teacher chroma for FMA)
#
# Usage: scripts/train_keynet_cqt.py [conf_thr] [--export out.onnx]
import sys, numpy as np, torch, torch.nn as nn, torch.nn.functional as F
torch.manual_seed(0); np.random.seed(0)
THR=float(sys.argv[1]) if len(sys.argv)>1 and not sys.argv[1].startswith("--") else 0.55
EXPORT=None
if "--export" in sys.argv: EXPORT=sys.argv[sys.argv.index("--export")+1]
dev="mps" if torch.backends.mps.is_available() else "cpu"; iu=np.triu_indices(12); NB=84; T=256

P=np.load("validation/pcqt_feats.npz")
gg=np.load("validation/giantsteps/grams.npz"); gt=np.load("validation/gtzan/feats.npz")
gs=np.load("validation/giantsteps/cqt.npz"); fma=np.load("validation/fma/fma_feats.npz")
Xc=np.concatenate([P["gs"],P["gt"]]).astype(np.float32)          # student TRUE feature
root=np.concatenate([gs["root"],gt["root"]]); minor=np.concatenate([gs["minor"],gt["minor"]]); N=len(Xc)
def gram2feat(G): return np.stack([np.concatenate([g.mean(1),np.cov(g)[iu]]) for g in G]).astype(np.float32)
Xt=np.concatenate([gram2feat(gg["G"]),gram2feat(gt["G"])]).astype(np.float32)   # teacher TRUE
Fc=P["fma"].astype(np.float32); Ff=fma["feat"].astype(np.float32)               # FMA student/teacher
print(f"true {N}  fma {len(Fc)}",file=sys.stderr)

def rotf(f,k):
    m=np.roll(f[:12],k); c=np.zeros((12,12)); c[iu]=f[12:]; c=c+c.T-np.diag(np.diag(c)); c=np.roll(np.roll(c,k,0),k,1)
    return np.concatenate([m,c[iu]]).astype(np.float32)
class MLP(nn.Module):
    def __init__(s): super().__init__(); s.n=nn.Sequential(nn.Linear(90,256),nn.ReLU(),nn.Dropout(.4),nn.Linear(256,128),nn.ReLU(),nn.Dropout(.4),nn.Linear(128,24))
    def forward(s,x): return s.n(x)
def train_teacher():
    xs,ys=[],[]
    for k in range(12):
        for t in range(N): xs.append(rotf(Xt[t],k)); ys.append(((root[t]+k)%12)*2+minor[t])
    Xa=torch.tensor(np.array(xs,np.float32),device=dev); ya=torch.tensor(np.array(ys,np.int64),device=dev)
    m=MLP().to(dev); opt=torch.optim.Adam(m.parameters(),1e-3,weight_decay=1e-4); lf=nn.CrossEntropyLoss(); m.train()
    for ep in range(150):
        p=torch.randperm(len(Xa),device=dev)
        for i in range(0,len(Xa),256):
            b=p[i:i+256]; opt.zero_grad(); lf(m(Xa[b]),ya[b]).backward(); opt.step()
    return m.eval()
def tta_label(m,Fmat):
    R=np.stack([[rotf(f,k) for k in range(12)] for f in Fmat]); acc=np.zeros((len(Fmat),24))
    with torch.no_grad():
        for k in range(12):
            p=F.softmax(m(torch.tensor(R[:,k],device=dev)),1).cpu().numpy()
            for r in range(12):
                for mm in range(2): acc[:,((r-k)%12)*2+mm]+=p[:,r*2+mm]
    acc/=12.0; lab=acc.argmax(1); return lab//2, lab%2, acc.max(1)

class Net(nn.Module):
    def __init__(s):
        super().__init__()
        def cb(i,o): return nn.Sequential(nn.Conv2d(i,o,3,padding=1),nn.BatchNorm2d(o),nn.ReLU(),
                                          nn.Conv2d(o,o,3,padding=1),nn.BatchNorm2d(o),nn.ReLU(),nn.MaxPool2d(2))
        s.n=nn.Sequential(cb(1,32),cb(32,64),cb(64,96),cb(96,128),nn.Dropout(0.5)); s.fc=nn.Linear(128,24)
    def forward(s,x): return s.fc(F.adaptive_avg_pool2d(s.n(x),1).flatten(1))
def mw(pr,pm,tr,tm):
    if pr==tr and pm==tm: return 1.0
    if pr==(tr+7)%12 and pm==tm: return .5
    if pm==0 and tm==1 and pr==(tr+3)%12: return .3
    if pm==1 and tm==0 and pr==(tr+9)%12: return .3
    if pr==tr and pm!=tm: return .2
    return 0.
def rollp(xb,k):
    if k==0: return xb
    o=torch.zeros_like(xb); o[:,:,k:,:]=xb[:,:,:NB-k,:]; return o
def specaug(xb):
    xb=xb.clone()
    for b in range(xb.size(0)):
        if np.random.rand()<.5: w=np.random.randint(1,12); f0=np.random.randint(0,NB-w); xb[b,:,f0:f0+w,:]=0
        if np.random.rand()<.5: w=np.random.randint(1,40); t0=np.random.randint(0,xb.size(3)-w); xb[b,:,:,t0:t0+w]=0
    return xb*(0.9+0.2*torch.rand(xb.size(0),1,1,1,device=xb.device))+0.05*torch.randn_like(xb)
def train_student(Xtr,rtr,mtr,epochs=80):
    m=Net().to(dev); opt=torch.optim.Adam(m.parameters(),1e-3,weight_decay=1e-4); lf=nn.CrossEntropyLoss()
    Xd=torch.tensor(Xtr,device=dev).unsqueeze(1)
    for ep in range(epochs):
        m.train(); perm=np.random.permutation(len(Xtr))
        for i in range(0,len(Xtr),32):
            b=perm[i:i+32]; k=int(np.random.randint(0,12)); xb=specaug(rollp(Xd[b],k))
            yb=torch.tensor((((rtr[b]+k)%12)*2+mtr[b]).astype(np.int64),device=dev)
            opt.zero_grad(); lf(m(xb),yb).backward(); opt.step()
    return m.eval()
def tta_eval(m,teI):
    Xd=torch.tensor(Xc[teI],device=dev).unsqueeze(1); acc=np.zeros((len(teI),24))
    with torch.no_grad():
        for k in range(12):
            p=F.softmax(m(rollp(Xd,k)),1).cpu().numpy()
            for r in range(12):
                for mm in range(2): acc[:,((r-k)%12)*2+mm]+=p[:,r*2+mm]
    pred=acc.argmax(1)
    return (sum(mw(p//2,p%2,root[t],minor[t]) for p,t in zip(pred,teI)),
            sum(1 for p,t in zip(pred,teI) if p//2==root[t] and p%2==minor[t]))

teacher=train_teacher(); pr,pm,conf=tta_label(teacher,Ff); keep=conf>=THR
print(f"pseudo kept {keep.sum()}/{len(Fc)} @conf>={THR}",file=sys.stderr)
PXc=Fc[keep]; Proot=pr[keep]; Pmin=pm[keep]

idx=np.arange(N); np.random.seed(1); np.random.shuffle(idx); folds=np.array_split(idx,5); tw=te=0
for f in range(5):
    teI=folds[f]; trI=np.concatenate([folds[j] for j in range(5) if j!=f])
    m=train_student(np.concatenate([Xc[trI],PXc]),np.concatenate([root[trI],Proot]),np.concatenate([minor[trI],Pmin]))
    w,ex=tta_eval(m,teI); tw+=w; te+=ex
    print(f"  fold {f}: MIREX {100*w/len(teI):.1f} exact {100*ex/len(teI):.1f}%",file=sys.stderr)
print(f"\n== pseudo-CQT self-trained (true {N} + pseudo {keep.sum()}): MIREX {100*tw/N:.2f} exact {100*te/N:.1f}% ==")
print(f"   bar: chroma+TTA 61.5/55.1 ; librosa-CQT self-trained 69.6/64.3")

if EXPORT:
    m=train_student(np.concatenate([Xc,PXc]),np.concatenate([root,Proot]),np.concatenate([minor,Pmin]),epochs=110)
    torch.onnx.export(m.cpu(),torch.zeros(1,1,NB,T),EXPORT,input_names=["spec"],output_names=["key_logits"],opset_version=13,dynamo=False)
    print(f"exported -> {EXPORT}")
