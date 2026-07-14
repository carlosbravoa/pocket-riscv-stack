import numpy as np, scipy.signal as sig
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt

FIN, FOUT = 12.288e6/248, 12.288e6/256      # 49548.39 , 48000.00  Hz
print(f"OPL rate {FIN:.2f} Hz -> audio {FOUT:.2f} Hz ; ratio {FOUT/FIN:.6f} = 31/32 = {31/32:.6f}")
L, M = 31, 32                                # exact rational upsample/downsample

# --- prototype LPF for the polyphase resampler: cutoff below Nyquist of the
#     lower rate (48k/2=24k), designed on the L*FIN interpolated grid ---
TAPS_PER_PHASE = 16
N = TAPS_PER_PHASE * L                       # prototype filter length
fs_up = FIN * L
cutoff = 0.90 * (FOUT/2)                      # 21.6 kHz passband edge
h = sig.firwin(N|1, cutoff/(fs_up/2), window=("kaiser", 7.0)) * L
print(f"prototype FIR: {len(h)} taps, {TAPS_PER_PHASE} MAC/output-sample in HW")

def resample_zoh(x):                          # current: nearest-neighbour (zero-order hold)
    idx = (np.arange(int(len(x)*FOUT/FIN)) * FIN/FOUT).astype(int)
    return x[idx]
def resample_fir(x):                          # polyphase 31/32
    return sig.resample_poly(x, L, M, window=('kaiser', 7.0))

# --- test 1: log sweep 20 Hz..24 kHz, measure alias/hash ---
T = 1.0; t = np.arange(int(FIN*T))/FIN
sw = sig.chirp(t, 20, T, 24000, method='log') * 0.7
def spec(y, fs):
    f, P = sig.welch(y, fs, nperseg=8192); return f, 10*np.log10(P+1e-12)

fig, ax = plt.subplots(2, 1, figsize=(9, 8))
for name, y in [("zero-order hold (current)", resample_zoh(sw)),
                ("polyphase FIR 31/32 (proposed)", resample_fir(sw))]:
    f, P = spec(y, FOUT); ax[0].plot(f/1000, P, label=name, lw=1)
ax[0].set(title="Log sweep 20Hz-24kHz resampled 49548->48000: output spectrum",
          xlabel="kHz", ylabel="dB", xlim=(0,24)); ax[0].legend(); ax[0].grid(alpha=.3)

# --- test 2: single 10 kHz tone -> see imaging/alias spurs ---
tone = np.sin(2*np.pi*10000*t)*0.7
for name, y in [("zero-order hold", resample_zoh(tone)),
                ("polyphase FIR", resample_fir(tone))]:
    Y = 20*np.log10(np.abs(np.fft.rfft(np.resize(y,48000)*np.hanning(48000)))+1e-9)
    Y -= Y.max(); fr = np.fft.rfftfreq(48000, 1/FOUT)
    ax[1].plot(fr/1000, Y, label=name, lw=1)
ax[1].set(title="Pure 10 kHz tone: spectral purity (spurs = resampler artifacts)",
          xlabel="kHz", ylabel="dBc", xlim=(0,24), ylim=(-100,2)); ax[1].legend(); ax[1].grid(alpha=.3)
plt.tight_layout(); plt.savefig(f"{__import__('os').path.dirname(__file__)}/fir_proto.png", dpi=110)

# --- quantify: noise/alias floor of the 10 kHz tone (everything except the tone) ---
def spur_floor(y):
    Y = np.abs(np.fft.rfft(np.resize(y,48000)*np.hanning(48000))); k = Y.argmax()
    Y[max(0,k-3):k+4] = 0
    return 20*np.log10(Y.max()/np.abs(np.fft.rfft(np.resize(y,48000)*np.hanning(48000))).max()+1e-12)
print(f"worst spur vs tone:  ZOH {spur_floor(resample_zoh(tone)):6.1f} dBc   FIR {spur_floor(resample_fir(tone)):6.1f} dBc")
# 16-bit coeffs for HW
q = np.round(sig.firwin(N|1, cutoff/(fs_up/2), window=("kaiser",7.0))*32768).astype(int)
print(f"coeff peak {q.max()}, fits int16; HW = 1 DSP mult + {len(q)}x16b coeff ROM + 16-deep line, ~{TAPS_PER_PHASE} MAC/out at 256 cyc budget")
