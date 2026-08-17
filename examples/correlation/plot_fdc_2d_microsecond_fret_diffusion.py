"""
===================================================================
Microsecond 2D-FDC with FREELY DIFFUSING molecules (tau_D = 2 ms)
===================================================================

The immobilized-molecule benchmark (``plot_fdc_2d_microsecond_fret.py``)
answers the physics question but not the experiment most people run: freely
diffusing single molecules at single-molecule concentration. This example
repeats the microsecond recovery with **open-volume diffusion**, tau_diff =
w0^2/(4 D) = 2 ms (w0 = 0.3 um, D = 11.25 um2/s), surface-flux injected at
~40 molecules in a 1.5 x 3 um box (~0.15 molecules in the focus on average,
254 kcps donor-channel count rate) -- the same two FRET states E = 0.2 / 0.8
(tau_D 3.2 / 0.8 ns), T3 macro clock at the 25 ns laser period.

**A statistic lesson the diluted regime forces.** With immobilized molecules
essentially every pair is same-molecule, and the total-variation coupling D
works well. Dilute diffusing molecules make only ~half the pairs
same-molecule, the coupling amplitude drops ~10x -- and the TV statistic
turns out to carry a *shot-noise pedestal* of order sqrt(K / 4N) (K = 144
matrix cells, N pairs per lag): measured 0.018 at 36k pairs, flat at every
lag, present verbatim in a single-lifetime control (no lifetime information
at all) and in a micro-time-shuffled null. The immobilized benchmark never
saw it because its signal was 10x taller. The fix is not a fudge: the
**pair micro-time covariance** -- Cov(bin1, bin2) over the pairs, computed
from the same ``fdc_scan_log`` matrix -- has per-pair noise, no positive
bias, and recovers the relaxation where the TV fit collapses (0.52 us
fitted against 1.0 us true, versus 1.04 us with the covariance).

Measured on this machine (2026-08-16, three seeds per point, 5 s streams,
null-free covariance fit):

========= =========== ===== =====================================
true      fitted      error note
========= =========== ===== =====================================
200 ns    193 ns      3 %
500 ns    575 ns      15 %
1.0 us    1.15 us     15 %
2.0 us    1.93 us     4 %
5.0 us    6.4 us      27 %  one seed at the edge of usable
10.0 us   9.9 us      1 %
5.0 ms    ~3.4 ms     --    NOT resolved: beyond tau_diff
========= =========== ===== =====================================

So microsecond dynamics survive diffusion easily (all lags here sit far
below the 2 ms diffusion time), with wider seed scatter than the
immobilized case -- the price of the same-molecule dilution. And the
**diffusion time is the ceiling**, exactly as the window width is the
floor: the 5 ms relaxation is gated away by molecules leaving the focus
(each seed fits 2.6-4.8 ms, i.e. roughly tau_diff, not the kinetics).
Between the two bounds -- 75 ns window span and 2 ms diffusion -- the
method sees everything this example throws at it.

Simulation detail worth knowing: the engine's window (2.5 us = 100 laser
periods) is coarser than the T3 clock; macro ticks are reconstructed as
``round((window * dt + arrival) / 25 ns)``, the sync-divider picture.
Diffusion per window is 2.4% of w0 and the kinetics stay continuous-time,
so nothing in the reconstruction is load-bearing for the recovery.

Method and provenance as in ``plot_fdc_2d.py`` (Ishii & Tahara 2013;
Kondo et al. 2019). Full runtime ~4 minutes; the numbers above regenerate
from the fixed seeds.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib

N_MICRO = 256
LASER_NS = 25.0               # T3 sync period: the macro clock
TICK_S = LASER_NS * 1e-9
DT_WIN = 2.5e-6               # engine window: 100 laser periods
MICRO_DT = LASER_NS / N_MICRO
L, DDT = 12, 2                # 12 log bins; lag window +/-1 clock (75 ns span)
W0 = 0.3                      # um
TAU_DIFF = 2e-3               # s
D_UM2_S = W0 * W0 / (4 * TAU_DIFF)      # 11.25 um2/s
TAUS = (3.2, 0.8)             # donor lifetimes of E = 0.2 / 0.8 at tau0 = 4 ns
POP = (20, 20)                # expected molecules per species in the box
T_STREAM = 5.0
SEEDS = (11, 23, 31)
RELAX = [200e-9, 500e-9, 1e-6, 2e-6, 5e-6, 10e-6, 5e-3]


def _cfg(k, seed):
    return {
        "settings": {
            "dt": DT_WIN, "n_ph_max": 10 ** 12,
            "max_windows": int(T_STREAM / DT_WIN), "n_channels": 1,
            "n_microtime_channels": N_MICRO,
            "microtime_resolution": MICRO_DT, "laser_period": LASER_NS,
            "seed_diffusion": seed, "seed_emission": seed + 1,
        },
        "box": {"xy": 1.5, "z": 3.0},
        "species": [
            {"D": D_UM2_S, "q": [500000.0], "decay": {"lifetimes": [TAUS[0]]}},
            {"D": D_UM2_S, "q": [500000.0], "decay": {"lifetimes": [TAUS[1]]}},
        ],
        "k_rad": [0, 0, 0, 0],
        "k_nrad": [0.0, k, k, 0.0],
        "background": [0.0],
        "population": [float(POP[0]), float(POP[1])],
        "excitation": {"type": "gaussian3d", "w0": W0, "z0": 2.0,
                       "extent_xy": 1.5, "extent_z": 3.0, "spacing": 0.05},
    }


def simulate(k, seed):
    eng = tttrlib.SimEngine.from_dict(_cfg(k, seed))
    eng.run()
    w = np.asarray(eng.macro_window(), dtype=np.float64)
    a = np.asarray(eng.arrival_time(), dtype=np.float64)
    macro = np.maximum.accumulate(
        np.rint((w * DT_WIN + a) / TICK_S).astype(np.int64))
    micro = np.asarray(eng.micro_time(), dtype=np.int64)
    return macro, micro


def cov_curve(macro, micro, lags):
    """Pair micro-time covariance per lag, from the 2D-FDC matrix.

    Cov(bin1, bin2) over the photon pairs is the matrix quantity with
    per-pair noise. The total-variation coupling used by the immobilized
    examples carries a sqrt(K/4N) shot-noise pedestal that the ~2x dilution
    of diffusing molecules pushes up to the signal level; the covariance has
    no such positive bias (see the module docstring for the measurement).
    """
    lags = np.asarray(lags, dtype=np.int64)
    out = np.zeros(lags.size * L * L, dtype=np.int64)
    tttrlib.fdc_scan_log(macro, micro, lags, DDT, 0, N_MICRO - 1, L, 4, out)
    bins = np.arange(L)
    c = []
    for m in out.reshape(lags.size, L, L):
        p = m.astype(float) / m.sum()
        r, q = p.sum(1), p.sum(0)
        c.append(float((bins[:, None] * bins[None, :] * p).sum())
                 - float((bins * r).sum()) * float((bins * q).sum()))
    return np.asarray(c)


def lag_grid(relax_s):
    lo = max(4, int(round(0.15 * relax_s / TICK_S)))
    hi = max(48, int(round(6.0 * relax_s / TICK_S)))
    return np.unique(np.round(np.geomspace(lo, hi, 8)).astype(np.int64))


def fit_relaxation(lags, c):
    base = c[-2:].mean()
    y = c - base
    usable = y > 0.25 * y[0]
    if usable.sum() < 3 or y[0] <= 0:
        return np.nan
    slope = np.polyfit(lags[usable] * TICK_S, np.log(y[usable]), 1)[0]
    return -1.0 / slope


fitted = np.full((len(RELAX), len(SEEDS)), np.nan)
for i, relax in enumerate(RELAX):
    lags = lag_grid(relax)
    for j, seed in enumerate(SEEDS):
        macro, micro = simulate(0.5 / relax, seed)
        fitted[i, j] = fit_relaxation(lags, cov_curve(macro, micro, lags))
    print(f"true {relax * 1e9:8.0f} ns -> fitted "
          f"{np.nanmean(fitted[i]) * 1e9:9.1f} ns", flush=True)

mean = np.nanmean(fitted, axis=1)

# one curve for the figure: the 1 us case
lags = lag_grid(1e-6)
macro, micro = simulate(0.5e6, SEEDS[0])
c1 = cov_curve(macro, micro, lags)

fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.4))

ax = axes[0]
ax.loglog(RELAX[:6], RELAX[:6], "k--", lw=1, label="identity")
for j in range(len(SEEDS)):
    ax.loglog(RELAX[:6], fitted[:6, j], "o", ms=4, alpha=0.4,
              color="tab:blue")
ax.loglog(RELAX[:6], mean[:6], "o-", color="tab:blue",
          label="fitted (mean of 3 seeds)")
ax.loglog([RELAX[6]], [mean[6]], "x", ms=9, color="tab:red",
          label="5 ms relaxation: gated by diffusion")
ax.axvspan(25e-9 * 3, 75e-9, color="tab:orange", alpha=0.2,
           label="below window span (75 ns)")
ax.axvline(TAU_DIFF, color="gray", ls=":",
           label=f"tau_diff = {TAU_DIFF * 1e3:.0f} ms")
ax.set_xlabel("true relaxation (s)")
ax.set_ylabel("fitted relaxation (s)")
ax.set_title("freely diffusing single FRET molecules\n"
             "resolved from 200 ns to 10 us; ceiling = diffusion")
ax.legend(fontsize=8)

ax = axes[1]
ax.semilogx(lags * TICK_S * 1e9, c1, "o-", color="tab:blue",
            label="pair covariance, 1 us case")
base = c1[-2:].mean()
tt = np.geomspace(lags[0], lags[-1], 100) * TICK_S * 1e9
ax.semilogx(tt, (c1[0] - base) * np.exp(-tt / mean[2]) + base, "-",
            color="tab:red", lw=1.5,
            label=f"fit {mean[2] * 1e9:.0f} ns (true 1000 ns)")
ax.axhline(base, ls=":", color="gray", lw=1)
ax.set_xlabel("lag dT (ns)")
ax.set_ylabel("Cov(bin1, bin2)  (bin^2)")
ax.set_title("the diluted regime needs the covariance\n"
             "(total variation drowns in its own noise pedestal)")
ax.legend(fontsize=8)

fig.suptitle("2D-FDC microsecond dynamics, diffusing molecules: "
             f"tau_diff = {TAU_DIFF * 1e3:.0f} ms, 254 kcps, T3 at 40 MHz",
             y=1.03)
fig.tight_layout()
plt.show()
