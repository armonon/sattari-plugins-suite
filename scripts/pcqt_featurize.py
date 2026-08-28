#!/usr/bin/env python
# PSEUDO-CQT: a log-frequency spectrogram that C++ can reproduce EXACTLY.
#   FFT magnitude -> 84 semitone-spaced triangular filters (C1 + b semitones)
#   -> dB (ref=max) -> per-track standardise -> time-pool to 256 frames.
# No multi-rate CQT, no librosa internals: just FFT + a filterbank the plugin
# builds from the same closed form. Featurises giantsteps / gtzan / fma in the
# SAME ORDER as the existing caches so labels + teacher feats stay aligned.
import os, glob, sys, numpy as np, librosa
SR=22050; NFFT=8192; HOP=4096; NB=84; T=256; FMIN=32.703195662574829  # C1

def filterbank(sr=SR, nfft=NFFT, nb=NB, fmin=FMIN):
    """84 triangular filters, centres at fmin*2^(b/12); edges at neighbours."""
    freqs=np.fft.rfftfreq(nfft, 1.0/sr)                    # [nfft/2+1]
    cen=fmin*2.0**(np.arange(nb+2)/12.0)                   # need b-1 .. b+1
    fb=np.zeros((nb,len(freqs)),dtype=np.float32)
    for b in range(nb):
        lo,c,hi=cen[b],cen[b+1],cen[b+2]
        left=(freqs>lo)&(freqs<=c); right=(freqs>c)&(freqs<hi)
        fb[b,left]=(freqs[left]-lo)/max(c-lo,1e-9)
        fb[b,right]=(hi-freqs[right])/max(hi-c,1e-9)
        s=fb[b].sum()
        if s>0: fb[b]/=s                                   # area-normalise
    return fb
FB=filterbank()

def pcqt_of(y, sr=SR):
    if sr!=SR: y=librosa.resample(y,orig_sr=sr,target_sr=SR)
    n=len(y)
    if n<NFFT: y=np.pad(y,(0,NFFT-n)); n=len(y)
    win=np.hanning(NFFT).astype(np.float32)
    frames=[]
    for s in range(0,n-NFFT+1,HOP):
        spec=np.abs(np.fft.rfft(y[s:s+NFFT]*win))
        frames.append(FB@spec)
    if not frames: return None
    M=np.stack(frames,axis=1)                              # [84, t]
    M=20.0*np.log10(np.maximum(M,1e-10)); M-=M.max()       # dB, ref=max
    M=(M-M.mean())/(M.std()+1e-6)
    c=M.shape[1]
    if c<T: o=np.zeros((NB,T),np.float32); o[:,:c]=M; return o
    e=np.linspace(0,c,T+1).astype(int)
    return np.stack([M[:,e[i]:max(e[i]+1,e[i+1])].mean(1) for i in range(T)],1).astype(np.float32)

def load(p,dur):
    y,sr=librosa.load(p,sr=SR,mono=True,duration=dur); return y,SR

PC={'C':0,'C#':1,'DB':1,'D':2,'D#':3,'EB':3,'E':4,'F':5,'F#':6,'GB':6,'G':7,'G#':8,'AB':8,'A':9,'A#':10,'BB':10,'B':11}
def parsekey(s):
    s=s.strip()
    for suf,m in [(' minor',1),(' major',0),(' min',1),(' maj',0)]:
        if s.lower().endswith(suf): return (PC[s[:-len(suf)].strip().upper()],m)
    return None

out={}
# --- GiantSteps (order: labels.csv order, matching extract_gram/extract_cqt) ---
G=[];
gslab={}
for ln in open("validation/giantsteps/labels.csv"):
    p=ln.strip().split(",")
    if len(p)>=2:
        k=parsekey(p[1])
        if k: gslab[p[0].replace('.wav','')]=k
for i,f in enumerate(sorted(glob.glob("validation/giantsteps/wav/*.wav"))):
    stem=os.path.basename(f).replace(".wav","")
    if stem not in gslab: continue
    y,sr=load(f,120); m=pcqt_of(y)
    if m is not None: G.append(m)
    if i%100==0: print(f"  gs {i}",file=sys.stderr)
out["gs"]=np.array(G,np.float32); print("gs",out["gs"].shape,file=sys.stderr)

# --- GTZAN (order: sorted wavs, tonal only — matches extract_gtzan) ---
labs={}
for f in glob.glob("validation/gtzan/keys/**/*.lerch.txt",recursive=True):
    try: labs[os.path.basename(f).replace(".lerch.txt","")]=int(open(f).read().strip())
    except ValueError: pass
GT=[]
for i,f in enumerate(sorted(glob.glob("validation/gtzan/genres/**/*.wav",recursive=True))):
    stem=os.path.basename(f).replace(".wav","")
    if stem not in labs or labs[stem]<0: continue
    y,sr=load(f,30); m=pcqt_of(y)
    if m is not None: GT.append(m)
    if i%200==0: print(f"  gt {i}",file=sys.stderr)
out["gt"]=np.array(GT,np.float32); print("gt",out["gt"].shape,file=sys.stderr)

# --- FMA (exact ids kept by st_featurize_fma, preserves teacher-feat alignment) ---
ids=list(np.load("validation/fma/fma_feats.npz")["ids"])
byname={os.path.basename(p):p for p in glob.glob("validation/fma/fma_small/**/*.mp3",recursive=True)}
FM=[]; ok=[]
for i,nm in enumerate(ids):
    p=byname.get(str(nm))
    if p is None: FM.append(np.zeros((NB,T),np.float32)); ok.append(False); continue
    try:
        y,sr=load(p,30); m=pcqt_of(y)
        FM.append(m if m is not None else np.zeros((NB,T),np.float32)); ok.append(m is not None)
    except Exception:
        FM.append(np.zeros((NB,T),np.float32)); ok.append(False)
    if i%300==0: print(f"  fma {i}",file=sys.stderr)
out["fma"]=np.array(FM,np.float32); out["fma_ok"]=np.array(ok)
print("fma",out["fma"].shape,"ok",out["fma_ok"].sum(),file=sys.stderr)
np.savez("validation/pcqt_feats.npz",**out)
np.save("validation/pcqt_filterbank.npy",FB)
print("saved validation/pcqt_feats.npz + pcqt_filterbank.npy")
