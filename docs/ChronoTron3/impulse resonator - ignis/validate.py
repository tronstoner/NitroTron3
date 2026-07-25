import numpy as np
from scipy.linalg import solve_toeplitz
from scipy.signal import hilbert, butter, sosfilt

FS = 48000.0

# ---------------------------------------------------------------- resonator
def comb_int(x, D, g):
    """Comb with integer delay D and 2-point average loop filter."""
    y = np.zeros(len(x))
    for n in range(len(x)):
        a = y[n - D] if n >= D else 0.0
        b = y[n - D - 1] if n >= D + 1 else 0.0
        y[n] = x[n] + g * 0.5 * (a + b)
    return y

def comb_frac(x, Dfrac, g):
    """Comb with linear-interpolated fractional delay + 2-point average loop."""
    Di = int(np.floor(Dfrac))
    frac = Dfrac - Di
    y = np.zeros(len(x))
    for n in range(len(x)):
        def tap(k):
            return y[n - k] if n >= k else 0.0
        d = (1.0 - frac) * tap(Di) + frac * tap(Di + 1)
        d2 = (1.0 - frac) * tap(Di + 1) + frac * tap(Di + 2)
        y[n] = x[n] + g * 0.5 * (d + d2)
    return y

def g_for_t60(t60, Dtot):
    """Invert T60(f) at DC: g = 10^(-3D / (T60 fs))."""
    return 10.0 ** (-3.0 * Dtot / (t60 * FS))

def t60_predicted(f, g, Dtot):
    """T60(f) = -3D / (fs log10 |g L(f)|), L = 2-point average."""
    G = np.abs(g * np.cos(np.pi * f / FS))
    return -3.0 * Dtot / (FS * np.log10(G))

# ------------------------------------------------------- measured decay fit
def t60_measured(sig, f0, bw=30.0):
    sos = butter(4, [(f0 - bw) / (FS / 2), (f0 + bw) / (FS / 2)], btype='band', output='sos')
    env = np.abs(hilbert(sosfilt(sos, sig)))
    peak = env.max()
    idx = np.where(env > peak * 1e-3)[0]
    if len(idx) < 2000:
        return np.nan
    # fit over the stable middle portion
    lo = idx[0] + int(0.02 * FS)
    hi = idx[-1] - int(0.02 * FS)
    if hi - lo < 2000:
        return np.nan
    t = np.arange(lo, hi) / FS
    a = np.polyfit(t, np.log(env[lo:hi]), 1)[0]
    return -3.0 * np.log(10.0) / a

# ------------------------------------------------------------ TEST 1
print("=" * 68)
print("TEST 1  T60(f) formula vs measured, impulse into comb")
print("=" * 68)

D = 436                      # ~110 Hz
Dtot = D + 0.5
f_res = FS / Dtot
T60_TARGET = 1.5
g = g_for_t60(T60_TARGET, Dtot)
print(f"D={D}  f0={f_res:.2f} Hz  g={g:.6f}  target T60(DC)={T60_TARGET}s\n")

x = np.zeros(int(4.0 * FS)); x[0] = 1.0
ir = comb_int(x, D, g)

print(f"{'partial':>8} {'freq Hz':>10} {'|L(f)|':>9} {'T60 pred':>10} {'T60 meas':>10} {'err %':>8}")
rows = []
for k in range(1, 13):
    f = k * f_res
    if f > 0.45 * FS:
        break
    tp = t60_predicted(f, g, Dtot)
    tm = t60_measured(ir, f)
    err = 100.0 * (tm - tp) / tp if np.isfinite(tm) else np.nan
    rows.append((k, f, tp, tm, err))
    print(f"{k:>8} {f:>10.1f} {np.cos(np.pi*f/FS):>9.4f} {tp:>10.4f} {tm:>10.4f} {err:>8.2f}")

errs = np.array([abs(r[4]) for r in rows if np.isfinite(r[4])])
print(f"\nmax |error| = {errs.max():.2f} %   mean = {errs.mean():.2f} %")

# ------------------------------------------------------------ synthetic pluck
def pluck(f0, dur=2.0, n_part=14, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(int(dur * FS)) / FS
    y = np.zeros_like(t)
    for k in range(1, n_part + 1):
        f = k * f0
        if f > 0.45 * FS:
            break
        amp = 1.0 / k
        tau = 1.2 / (1.0 + 0.35 * k)          # higher partials die faster
        y += amp * np.exp(-t / tau) * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
    y *= np.minimum(1.0, t / 0.002)            # 2 ms attack
    return y / np.max(np.abs(y))

# ------------------------------------------------------------ whitening
def whiten(x, lam, order=16):
    """LPC residual with bandwidth expansion. lam=0 -> passthrough, 1 -> full."""
    if lam <= 0:
        return x.copy()
    xw = x * np.hanning(len(x)) if False else x
    r = np.correlate(xw, xw, 'full')[len(xw) - 1:len(xw) + order]
    r = r / r[0]
    r[0] += 1e-6
    a = solve_toeplitz(r[:order], r[1:order + 1])
    a = a * (lam ** np.arange(1, order + 1))
    e = x.copy()
    for k in range(1, order + 1):
        e[k:] -= a[k - 1] * x[:-k]
    return e

def crest(x):
    return np.max(np.abs(x)) / np.sqrt(np.mean(x ** 2))

# ------------------------------------------------------------ TEST 2 + 3
print("\n" + "=" * 68)
print("TEST 2/3  whitening sweep: bank excitation spread + crest factor")
print("=" * 68)

F0 = 110.0
src = pluck(F0)
N_BINS = 25                                   # 2 octaves, semitone spaced
bin_f = F0 * 2.0 ** (np.arange(N_BINS) / 12.0)
T60_BANK = 0.8

print(f"input: synthetic pluck at {F0} Hz, bank = {N_BINS} semitone bins "
      f"{bin_f[0]:.1f}-{bin_f[-1]:.1f} Hz\n")
print(f"{'lambda':>7} {'crest':>7} {'bins>-20dB':>11} {'bins>-30dB':>11} {'entropy':>9}")

results = []
for lam in [0.0, 0.25, 0.5, 0.75, 1.0]:
    e = whiten(src, lam)
    e = e / np.sqrt(np.mean(e ** 2))          # RMS normalise: equal energy in
    energies = np.zeros(N_BINS)
    for i, f in enumerate(bin_f):
        Df = FS / f - 0.5
        gg = g_for_t60(T60_BANK, Df + 0.5)
        y = comb_frac(e, Df, gg)
        energies[i] = np.sqrt(np.mean(y ** 2))
    db = 20 * np.log10(energies / energies.max())
    p = energies ** 2 / np.sum(energies ** 2)
    ent = -np.sum(p * np.log(p + 1e-30)) / np.log(N_BINS)
    n20, n30 = int(np.sum(db > -20)), int(np.sum(db > -30))
    results.append((lam, crest(e), n20, n30, ent, db))
    print(f"{lam:>7.2f} {crest(e):>7.2f} {n20:>11} {n30:>11} {ent:>9.4f}")

print("\nper-bin level, dB re max (rows = lambda, cols = semitone bin):")
print("      " + "".join(f"{i:>5}" for i in range(0, N_BINS, 2)))
for lam, _, _, _, _, db in results:
    print(f"{lam:>5.2f} " + "".join(f"{db[i]:>5.0f}" for i in range(0, N_BINS, 2)))

np.save('/home/claude/sweep.npy', np.array([r[5] for r in results]))
np.save('/home/claude/meta.npy', np.array([[r[0], r[1], r[4]] for r in results]))
