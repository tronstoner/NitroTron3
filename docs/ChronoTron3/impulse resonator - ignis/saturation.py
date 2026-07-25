import numpy as np
from scipy.signal import lfilter

FS = 48000.0

# ------------------------------------------------ comb as an IIR (fast path)
def comb_coeffs(Dfrac, g):
    Di = int(np.floor(Dfrac)); f = Dfrac - Di
    a = np.zeros(Di + 3); a[0] = 1.0
    a[Di]     = -0.5 * g * (1.0 - f)
    a[Di + 1] = -0.5 * g
    a[Di + 2] = -0.5 * g * f
    return np.array([1.0]), a

def comb(x, Dfrac, g):
    b, a = comb_coeffs(Dfrac, g)
    return lfilter(b, a, x)

def g_for_t60(t60, Dtot):
    return 10.0 ** (-3.0 * Dtot / (t60 * FS))

# ------------------------------------------------------------ source
def pluck(f0, dur=2.0, n_part=14, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(int(dur * FS)) / FS
    y = np.zeros_like(t)
    for k in range(1, n_part + 1):
        f = k * f0
        if f > 0.45 * FS: break
        y += (1.0 / k) * np.exp(-t / (1.2 / (1.0 + 0.35 * k))) * \
             np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
    y *= np.minimum(1.0, t / 0.002)
    return y / np.max(np.abs(y))

# ------------------------------------------------------------ nonlinearities
def nl_none(x, d):   return x
def nl_tanh(x, d):   return np.tanh(d * x) / np.tanh(d) if d > 0 else x
def nl_clip(x, d):   return np.clip(d * x, -1, 1)
def nl_cubic(x, d):
    u = np.clip(d * x, -1, 1); return 1.5 * u - 0.5 * u ** 3
def nl_asym(x, d):
    u = d * x; return np.tanh(u + 0.3) - np.tanh(0.3)
def nl_fold(x, d):   return np.sin(np.pi * 0.5 * np.clip(d * x, -3, 3))
def nl_rect(x, d):
    u = np.tanh(d * x); return 0.7 * u + 0.3 * np.abs(u)

NLS = [("none", nl_none), ("tanh", nl_tanh), ("hardclip", nl_clip),
       ("cubic", nl_cubic), ("asym tanh", nl_asym),
       ("wavefold", nl_fold), ("tanh+rect", nl_rect)]

# ------------------------------------------------------------ bank + metrics
F0 = 110.0
N_BINS = 25
BIN_F = F0 * 2.0 ** (np.arange(N_BINS) / 12.0)
T60_BANK = 0.8
OCT_BINS = {0, 12, 24}
NONOCT = [i for i in range(N_BINS) if i not in OCT_BINS]

def bank_energies(e):
    out = np.zeros(N_BINS)
    for i, f in enumerate(BIN_F):
        Dfrac = FS / f - 0.5
        y = comb(e, Dfrac, g_for_t60(T60_BANK, Dfrac + 0.5))
        out[i] = np.sqrt(np.mean(y ** 2))
    return out

def metrics(e):
    en = bank_energies(e)
    db = 20 * np.log10(en / en.max())
    p = en ** 2 / np.sum(en ** 2)
    ent = -np.sum(p * np.log(p + 1e-30)) / np.log(N_BINS)
    return db, int(np.sum(db > -20)), ent

def crest(x):
    return np.max(np.abs(x)) / np.sqrt(np.mean(x ** 2))

src = pluck(F0)
src = src / np.sqrt(np.mean(src ** 2)) * 0.1        # RMS ref
db_ref, _, _ = metrics(src)

print("=" * 78)
print("TEST 4  saturation type x drive: gap filling, gnarl, dynamics")
print("=" * 78)
print("baseline = raw pluck. lift = mean gain on the 22 non-octave bins, dB")
print("dyn = output level change for a 20 dB quieter input (20 = fully preserved)\n")
print(f"{'nonlinearity':>13} {'drive':>6} {'crest':>7} {'lift dB':>8} "
      f"{'bins>-20':>9} {'entropy':>8} {'dyn dB':>7}")

rows = []
for name, fn in NLS:
    for d in ([1.0] if name == "none" else [1.0, 4.0, 16.0, 64.0]):
        e = fn(src, d)
        e = e - np.mean(e)                          # DC block
        e = e / np.sqrt(np.mean(e ** 2))            # equal energy into bank
        db, n20, ent = metrics(e)
        lift = float(np.mean((db - db_ref)[NONOCT]))

        # dynamics: same chain, input 20 dB down, no output renormalisation
        q = fn(src * 0.1, d); q = q - np.mean(q)
        l = fn(src, d);       l = l - np.mean(l)
        dyn = 20 * np.log10(np.sqrt(np.mean(l ** 2)) / np.sqrt(np.mean(q ** 2)))

        rows.append((name, d, crest(e), lift, n20, ent, dyn, db))
        print(f"{name:>13} {d:>6.0f} {crest(e):>7.2f} {lift:>8.2f} "
              f"{n20:>9} {ent:>8.4f} {dyn:>7.2f}")

np.save('/home/claude/sat_db.npy', np.array([r[7] for r in rows]))
np.save('/home/claude/sat_meta.npy', np.array([[r[1], r[2], r[3], r[4], r[5], r[6]] for r in rows]))
with open('/home/claude/sat_names.txt', 'w') as fh:
    fh.write("\n".join(f"{r[0]}\t{r[1]:.0f}" for r in rows))
print("\nbaseline non-octave mean level: "
      f"{np.mean(db_ref[NONOCT]):.2f} dB re max")
