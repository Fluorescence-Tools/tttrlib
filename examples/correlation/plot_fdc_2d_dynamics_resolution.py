"""
=========================================================================
How fast, and how complex: 2D-FDC dynamics resolution on ground truth
=========================================================================

A methods benchmark, run entirely on simulated experiments whose answer is
known before the analysis starts. Two questions, both about the *dynamics* the
matrix measures — the coupling statistic D(dT) (total variation between the
pair distribution and the product of its marginals) should decay with the
system's relaxation time:

1. **How fast can it go?** Two states (1.5 / 3.5 ns) exchanging at equal
   rates, with the relaxation time swept over three decades
   (10 ms … 10 s) at a fixed macro window of 10 ms. Every point is the mean
   of three independent streams (seeds 11, 23, 31); the recovery is called
   resolved when the fitted relaxation lands within 25% of the truth.

2. **How complex can it be?** A three-state chain (1.5 / 2.5 / 3.5 ns,
   A <-> B fast at 1 s, B <-> C slow at 10 s) has *two* relaxation times --
   the eigenvalues of the rate matrix. A single exponential must fail on it,
   and a two-exponential fit must recover both times, for the method to
   claim it resolves complex dynamics.

Measured on this machine (2026-08-16, seed-replicated as stated):

* **Two-state sweep**: every relaxation from 50 ms to 10 s is recovered
  within 25% (e.g. 0.05 -> 0.038 s, 0.5 -> 0.451 s, 2 -> 1.754 s,
  10 -> 7.99 s). Below the lag-window width (ddT = 80 ms: 10 ms and 20 ms
  cases) the coupling is gone before the first usable lag and nothing is
  recoverable -- the resolution floor is the *window*, not the photon pass.
* **Three-state chain**: on the three-seed-averaged curve the
  two-exponential fit returns 0.86 s and 7.9 s against 1 s and 10 s true
  (both within 25%), and the single-exponential control (4.96 s) fits
  neither mode. Per-stream fits recover the fast mode every time (0.68,
  0.72, 1.03 s) but leave the slow mode noise-limited (4.6, 12.4, 16.1 s) --
  the slow component needs averaging or a longer stream at this photon
  count (300k photons / 1500 s).

The photon pass is the reference implementation (see ``plot_fdc_2d.py`` for
the method references and the original-author provenance).
"""

import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import curve_fit

import tttrlib

N_MICRO = 256          # micro-time channels
MICRO_DT = 0.032       # ns per channel
WINDOW_DT = 0.01       # s per macro window -- the resolution currency
L = 12                 # log bins per matrix axis
DDT = 8                # lag window width, in macro windows
WINDOWS = 150_000      # stream length (1500 s)

SEEDS = (11, 23, 31)


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def simulate(taus, rates_rowmajor, windows=WINDOWS, seed=5):
    """One immobile molecule, `taus` lifetime states, `rates_rowmajor` the
    flattened off-diagonal rate matrix in 1/s (row-major, diagonal unused)."""
    system = tttrlib.SimSystem()
    for tau in taus:
        species = tttrlib.SimSpecies()
        species.D = 0.0
        species.q = _vd([200.0])
        species.decay = tttrlib.SimDecay.multi_exponential(
            _vd([1.0]), _vd([tau]), N_MICRO, MICRO_DT)
        system.add_species(species)
    system.set_rate_matrices(_vd([0.0] * len(taus) ** 2),
                             _vd([float(v) for v in rates_rowmajor]))
    system.set_background(_vd([0.0]))
    system.set_box(50.0, 50.0)
    system.add_fluorophore(0.0, 0.0, 0.0, 0, False)

    integrator = tttrlib.SimIntegrator()
    integrator.dt = WINDOW_DT
    integrator.n_channels = 1
    integrator.n_ph_max = 10 ** 12
    integrator.max_windows = windows
    integrator.n_microtime_channels = N_MICRO
    integrator.microtime_resolution = MICRO_DT
    integrator.laser_period = N_MICRO * MICRO_DT
    integrator.seed_diffusion = seed
    integrator.seed_emission = seed + 1

    engine = tttrlib.SimEngine(
        system, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
        tttrlib.VectorSimGrid([]), integrator)
    engine.run()
    return (np.asarray(engine.macro_window(), dtype=np.int64),
            np.asarray(engine.micro_time(), dtype=np.int64))


def d_curve(macro, micro, lags):
    """The coupling D at each lag: build the matrices, take total variation
    against the marginals."""
    lags = np.asarray(lags, dtype=np.int64)
    out = np.zeros(lags.size * L * L, dtype=np.int64)
    tttrlib.fdc_scan_log(macro, micro, lags, DDT, 0, N_MICRO - 1, L, 4, out)
    d = []
    for m in out.reshape(lags.size, L, L):
        p = m.astype(float) / m.sum()
        d.append(0.5 * np.abs(p - np.outer(p.sum(1), p.sum(0))).sum())
    return np.asarray(d)


def fit_single_relaxation(lags_s, d):
    """Log-linear fit above the noise floor; returns (relaxation_s, ok)."""
    y = d - d[-1]
    usable = y > 0.25 * y[0]
    if usable.sum() < 3 or y[0] <= 0:
        return np.nan, False
    slope = np.polyfit(lags_s[usable], np.log(y[usable]), 1)[0]
    return -1.0 / slope, True


def lag_grid(relax_s):
    """Lags straddling the relaxation, every one clear of DDT/2 = 4 windows
    (self-pairs would otherwise spike the diagonal)."""
    lo = max(10, int(round(0.1 * relax_s / WINDOW_DT)))
    hi = min(40_000, int(round(6.0 * relax_s / WINDOW_DT)))
    lags = np.unique(np.round(np.geomspace(lo, hi, 10)).astype(np.int64))
    return lags


def _two_exp(t, a1, tau1, a2, tau2, b):
    return a1 * np.exp(-t / tau1) + a2 * np.exp(-t / tau2) + b


def run():
    # ------------------------------------------------------------------
    # 1. the rate sweep: two states, equal rates, three decades
    # ------------------------------------------------------------------
    true_relax = np.array([0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0])
    fitted = np.full((true_relax.size, len(SEEDS)), np.nan)

    for i, relax in enumerate(true_relax):
        k = 0.5 / relax                       # k01 = k10 = k -> relax = 1/(2k)
        rates = [0.0, k, k, 0.0]
        lags = lag_grid(relax)
        for j, seed in enumerate(SEEDS):
            macro, micro = simulate((1.5, 3.5), rates, seed=seed)
            d = d_curve(macro, micro, lags)
            fitted[i, j], _ = fit_single_relaxation(lags * WINDOW_DT, d)
        print(f"true {relax:6.2f} s -> fitted "
              f"{np.nanmean(fitted[i]):6.3f} s", flush=True)

    mean = np.nanmean(fitted, axis=1)
    resolved = np.abs(mean - true_relax) / true_relax < 0.25

    # ------------------------------------------------------------------
    # 2. complex dynamics: three states, two relaxation times
    # ------------------------------------------------------------------
    kfast, kslow = 0.5, 0.05                  # -> modes at 1 s and 10 s
    taus3 = (1.5, 2.5, 3.5)
    rates3 = [0.0, kfast, 0.0,
              kfast, 0.0, kslow,
              0.0, kslow, 0.0]
    # one grid straddling BOTH modes: 0.1 s … 60 s
    lags3 = np.unique(np.round(np.geomspace(10, 6000, 14)).astype(np.int64))
    t3 = lags3 * WINDOW_DT

    curves = [d_curve(*simulate(taus3, rates3, seed=s), lags3) for s in SEEDS]
    d3 = np.mean(curves, axis=0)
    # drop the lags where the signal has sank into the noise floor (the
    # floor is one-sided: D >= 0, so the quiet end estimates it)
    floor = np.mean(np.sort(d3)[:3])
    usable = d3 > 2.0 * floor

    popt, _ = curve_fit(_two_exp, t3[usable], d3[usable],
                        p0=(0.006, 1.0, 0.013, 10.0, 0.002),
                        bounds=([0, 0.05, 0, 0.5, 0], [1, 5, 1, 60, 0.05]),
                        maxfev=40000)
    # taus live at positions 1 and 3 of (a1, tau1, a2, tau2, b)
    tau_fast, tau_slow = sorted(popt[[1, 3]])
    one, ok1 = fit_single_relaxation(t3, d3)

    # per-stream spread: how much does a single stream alone support?
    per_stream = []
    for d_s in curves:
        u = d_s > 2.0 * np.mean(np.sort(d_s)[:3])
        try:
            p, _ = curve_fit(_two_exp, t3[u], d_s[u],
                             p0=(0.006, 1.0, 0.013, 10.0, 0.002),
                             bounds=([0, 0.05, 0, 0.5, 0], [1, 5, 1, 60, 0.05]),
                             maxfev=40000)
            per_stream.append(sorted(p[[1, 3]]))
        except RuntimeError:
            per_stream.append([np.nan, np.nan])
    per_stream = np.asarray(per_stream)
    print(f"3-state mean curve: fast {tau_fast:.2f} s, slow {tau_slow:.2f} s "
          f"(truth 1.0, 10.0); single exp fits {one:.2f} s", flush=True)
    print("3-state per-stream:", np.round(per_stream, 2).tolist(), flush=True)

    # ------------------------------------------------------------------
    # the figure
    # ------------------------------------------------------------------
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.2))

    ax = axes[0]
    ax.loglog(true_relax, true_relax, "k--", lw=1, label="identity")
    for j in range(len(SEEDS)):
        ax.loglog(true_relax, fitted[:, j], "o", ms=4, alpha=0.4,
                  color="tab:blue")
    ax.loglog(true_relax, mean, "o-", color="tab:blue",
              label="fitted (mean of 3 seeds)")
    ax.axvspan(0.01, DDT * WINDOW_DT, color="tab:red", alpha=0.15,
               label=f"below window width ({DDT * WINDOW_DT * 1e3:.0f} ms)")
    ax.set_xlabel("true relaxation (s)")
    ax.set_ylabel("fitted relaxation (s)")
    ax.set_title("two-state: recovery over three decades")
    ax.legend(fontsize=8)

    ax = axes[1]
    err = np.abs(mean - true_relax) / true_relax
    ax.semilogx(true_relax, 100 * err, "o-", color="tab:blue")
    ax.axhline(25, color="tab:red", ls="--", lw=1, label="25% boundary")
    ax.axvline(DDT * WINDOW_DT, color="gray", ls=":",
               label=f"window width {DDT * WINDOW_DT * 1e3:.0f} ms")
    ax.set_xlabel("true relaxation (s)")
    ax.set_ylabel("recovery error (%)")
    ax.set_title("resolved where error < 25%")
    ax.legend(fontsize=8)

    ax = axes[2]
    ax.semilogx(t3, d3, "o", color="tab:blue",
                label="coupling D(dT), 3 seeds mean")
    tt = np.geomspace(t3[0], t3[-1], 200)
    span = d3[0] - d3[-1]
    d1 = span * np.exp(-tt / one) + d3[-1]
    ax.semilogx(tt, d1, "-", color="gray", lw=1,
                label=f"single exp ({one:.1f} s) -- misses it")
    d2 = 0.4 * span * np.exp(-tt / tau_fast) + 0.6 * span * np.exp(-tt / tau_slow) + d3[-1]
    ax.semilogx(tt, d2, "-", color="tab:red", lw=1.5,
                label=f"two exp ({tau_fast:.1f}, {tau_slow:.1f} s)")
    ax.axvline(1.0, color="gray", ls=":", lw=0.8)
    ax.axvline(10.0, color="gray", ls=":", lw=0.8)
    ax.set_xlabel("lag dT (s)")
    ax.set_ylabel("D (total variation)")
    ax.set_title("three-state chain: two relaxations recovered\ntruth 1.0 and 10.0 s")
    ax.legend(fontsize=8)

    fig.suptitle("2D-FDC dynamics resolution, simulated ground truth "
                 f"(window {WINDOW_DT * 1e3:.0f} ms, ddT {DDT * WINDOW_DT * 1e3:.0f} ms)",
                 y=1.04)
    fig.tight_layout()
    plt.show()

    ok = np.flatnonzero(resolved)
    print("\n=== summary ===")
    print(f"resolved (within 25%, mean of 3 seeds): "
          f"{true_relax[ok].min():.3g} … {true_relax[ok].max():.3g} s")
    bad = np.flatnonzero(~resolved)
    if bad.size:
        print(f"NOT resolved: {true_relax[bad]} s "
              f"(window width = {DDT * WINDOW_DT} s)")
    print(f"3-state: fast {tau_fast:.2f} s (truth 1.0), "
          f"slow {tau_slow:.2f} s (truth 10.0), "
          f"single-exp control {one:.2f} s (neither)")
    print("3-state per-stream fast/slow:",
          np.round(per_stream, 2).tolist())


if __name__ == "__main__":
    run()
