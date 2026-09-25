"""Load / process / unload a plugin N times in one process (pedalboard). A host-crash detector for
lifetime bugs (e.g. v0.5.0 releaseResources on an unprepared instance segfaulted within 1-2 cycles).

usage: plugin_load_loop.py <plugin path> <cycles> [librosa]   (librosa: import it first, as a DAW-ish
       process would already have numerical libraries loaded)
"""
import faulthandler, sys; faulthandler.enable()
import numpy as np, pedalboard
P = sys.argv[1]; n=int(sys.argv[2]); pre=sys.argv[3] if len(sys.argv)>3 else ''
if pre=='librosa':
    import librosa; y=np.random.randn(48000).astype('float32'); librosa.pyin(y,fmin=65,fmax=1100,sr=48000); librosa.yin(y,fmin=65,fmax=1100,sr=48000)
for i in range(n):
    p = pedalboard.load_plugin(P)
    x = np.random.randn(1, 48000).astype('float32')*0.1
    p(x, 48000, reset=True)
    del p
    print(i, end=' ', flush=True)
print('ok')
