import time, numpy as np, torch, torchcrepe, librosa, soundfile as sf
from csd_bench import tracks, section_notes, singer_f0, note_centre, WORK
def crepe(y, model, hop):
    y16=librosa.resample(y,orig_sr=48000,target_sr=16000)
    p,per=torchcrepe.predict(torch.tensor(y16)[None],16000,hop_length=int(16000*hop),fmin=60,fmax=1100,model=model,decoder=torchcrepe.decode.viterbi,return_periodicity=True,batch_size=2048,device='mps')
    per=torchcrepe.filter.median(per,3); p,per=p[0].numpy(),per[0].numpy()
    return np.where(per>0.5, 69+12*np.log2(p/440), np.nan)
for model,hop in [('tiny',0.01),('full',0.02)]:
    d=[]; t0=time.time()
    for stem in tracks('tune')[:4]:
        y,_=sf.read(WORK/f'{stem}.wav',dtype='float32'); m=crepe(y,model,hop); tm=np.arange(len(m))*hop; t,mr=singer_f0(stem)
        for s,e,tg in section_notes(stem):
            a=note_centre(tm,m,s,e); r=note_centre(t,mr,s,e)
            if np.isfinite(a) and np.isfinite(r): d.append(100*(a-r))
    d=np.abs(d); print(f'{model} hop {hop}: vs annotation median {np.median(d):.2f}c p90 {np.percentile(d,90):.2f}c | {(time.time()-t0)/4:.0f}s per track')
