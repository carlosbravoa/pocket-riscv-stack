# Locked config: 31/32 polyphase FIR, 16 taps/phase, Kaiser beta=9, cutoff Fout/2.
# Fixed-point int16 (Q15). Emits fir_coeffs.hex (496 x 16b, phase-major tap-minor)
# and a bit-exact reference output for the Verilog bench to match.
import numpy as np, scipy.signal as sig, os
L,M,TAPS,BETA = 31,32,16,9.0
D=os.path.dirname(__file__)
N=TAPS*L
h=sig.firwin(N|1, 1.0/M, window=('kaiser',BETA))*L
q=np.clip(np.round(h*32768),-32768,32767).astype(np.int64)
poly=np.zeros((L,TAPS),np.int64)
for p in range(L):
    for j in range(TAPS):
        k=p+j*L; poly[p,j]=q[k] if k<len(q) else 0
with open(f"{D}/fir_coeffs.hex","w") as f:
    for p in range(L):
        for j in range(TAPS):
            f.write(f"{poly[p,j]&0xFFFF:04x}\n")
print(f"fir_coeffs.hex: {L*TAPS} x int16 (16 taps/phase, 31 phases), peak {abs(poly).max()}")

# bit-exact software model = the exact MAC the Verilog performs, so the bench can diff.
def fx(x):
    x=np.asarray(x,np.int64); y=[]
    for n in range(int(len(x)*L//M)):
        ph=(n*M)%L; ii=(n*M)//L; acc=0
        for j in range(TAPS):
            xi=ii-j
            if 0<=xi<len(x): acc+=poly[ph,j]*x[xi]
        acc=(acc+(1<<14))>>15; y.append(max(-32768,min(32767,acc)))
    return np.array(y,np.int64)

# emit a test input (sweep, int16) and its golden fixed-point output for the bench
t=np.arange(4000)/(12.288e6/248)
sw=np.round(sig.chirp(t,200,t[-1],23000,method='log')*0.7*32767).astype(np.int64)
np.savetxt(f"{D}/tb_in.txt",  sw & 0xFFFF, fmt="%04x")
np.savetxt(f"{D}/tb_gold.txt",fx(sw)&0xFFFF, fmt="%04x")
print(f"tb_in.txt {len(sw)} in-samples, tb_gold.txt golden outputs")
