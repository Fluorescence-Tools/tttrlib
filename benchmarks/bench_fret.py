#!/usr/bin/env python
"""tttrlib vs the upstream FRET / burst references -- tttrlib side.

  pda       Pda.s1s2                     <- PAM PDA_histogram.cpp (Schrimpf et al. 2018), compiled natively
  burstml   BurstML.neg_log_likelihood   <- the original FRET_burstML MEX (mlhDiffNTRbkg_MT.cpp), compiled natively
  two_cde   TwoCDE (FRET-2CDE, Laplace)  <- FRETBursts phrates.kde_laplace + Tomov's burst formula
  fdc2d     fdc_scan_log                 <- Toru Kondo's TK_Create2DFDC_04.m in Octave
  cusum     TTTR.burst_search_cusum_sprt <- PAM CUSUM_burstsearch in Octave (behavioural pair)

Run in the base env. Writes the exact inputs to results/shared/fret/ so
competitors/bench_fret.py times the reference code on identical data (native
drivers under competitors/native/, Octave, the fretbursts venv), and
check_fret.py compares outputs. The A/Bs are the permanent tests
(test/python/pda/test_ab_pda_reference.py, burstfilter/test_ab_burst_reference.py,
bva/test_ab_bva_2cde_recurrence_reference.py, fcs/test_fdc2d.py); this file
measures speed.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "test", "python", "burstfilter"))
import tttrlib  # noqa: E402
from common import bench, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "fret")
os.makedirs(SHARED, exist_ok=True)
SEED = 20260817
RES = 1e-7          # seconds per tick in the synthetic streams (as in the burst tests)


# --------------------------------------------------------------------------- inputs

def make_pda(nmax=180, lam=60.0, bg1=0.7, bg2=0.4):
    from scipy.stats import poisson
    pf = poisson.pmf(np.arange(nmax + 1), lam)
    pf /= pf.sum()
    amps = np.array([0.5, 0.3, 0.2])
    probs = np.array([0.25, 0.55, 0.8])
    return nmax, pf, amps, probs, bg1, bg2


def make_burstml(n_bursts=200, seed=1, n_params=20):
    from test_burstml import simulate_bursts
    times, colours, offsets = simulate_bursts(n_bursts=n_bursts, min_photons=30, seed=seed)
    t100 = np.round(times * 1e4).astype(np.int64)      # the MEX's 100 ns unit
    lb = np.array([50, 100, 0.1, 0.3, 0.1, 0.2, 0.7, 0.3, 0.5, 0.5], float)
    ub = np.array([500, 600, 3.0, 10.0, 10.0, 0.8, 0.95, 0.6, 5.0, 3.0], float)
    rng = np.random.default_rng(seed)
    params = lb + rng.random((n_params, lb.size)) * (ub - lb)
    return t100, np.asarray(colours), np.asarray(offsets), params, lb, ub


def make_two_cde(n_bursts=200, n_ph=120, seed=21, gap=5000, macro_step=4):
    rng = np.random.default_rng(seed)
    macro, chan, bounds = [], [], []
    t = 0
    for p in rng.uniform(0.1, 0.9, size=n_bursts):
        ch = (rng.random(n_ph) < p).astype(int)
        start = len(macro)
        for c in ch:
            t += macro_step
            macro.append(t)
            chan.append(int(c))
        t += gap
        bounds.append((start, len(macro) - 1))
    return np.asarray(macro, np.int64), np.asarray(chan, np.int8), np.asarray(bounds, np.int64)


def make_fdc(n=4000, t_max=4096, seed=3):
    rng = np.random.default_rng(seed)
    macro = np.cumsum(rng.integers(1, 12, n)).astype(np.int64)
    micro = rng.integers(1, t_max, n).astype(np.int64)
    return macro, micro


def make_cusum(seed=0, background_cps=3000.0, duration=1.0,
               bursts=((0.2, 0.003, 30000.0), (0.5, 0.002, 60000.0), (0.8, 0.004, 25000.0))):
    rng = np.random.default_rng(seed)
    t = [rng.uniform(0.0, duration, rng.poisson(background_cps * duration))]
    for t0, length, rate in bursts:
        t.append(rng.uniform(t0, t0 + length, rng.poisson(rate * length)))
    t = np.sort(np.concatenate(t))
    ticks = np.maximum.accumulate(np.round(t / RES).astype(np.int64))
    return ticks


def tttr_from_ticks(ticks, channels=None):
    ticks = np.asarray(ticks, dtype=np.uint64)
    n = ticks.size
    ch = np.zeros(n, dtype=np.int8) if channels is None else np.asarray(channels, dtype=np.int8)
    t = tttrlib.TTTR(ticks, np.zeros(n, dtype=np.uint16), ch, np.zeros(n, dtype=np.int8))
    t.header.set_macro_time_resolution(RES)
    return t


# --------------------------------------------------------------------------- run

def main():
    # 1. PDA
    nmax, pf, amps, probs, bg1, bg2 = make_pda()
    np.savez(os.path.join(SHARED, "pda.npz"), nmax=nmax, pF=pf, amps=amps, probs=probs, bg1=bg1, bg2=bg2)
    pf_list = pf.tolist()

    def run_pda():
        pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0, background_ch1=bg1, background_ch2=bg2, pF=pf_list)
        for a, p in zip(amps, probs):
            pda.append(float(a), float(p))
        return np.asarray(pda.s1s2)

    bench("pda", "tttrlib", f"PDA S1/S2 histogram, nmax={nmax}, 3-species mixture",
          run_pda, repeat=5, warmup=1, n_items=(nmax + 1) ** 2, unit="cell", dataset="simulated")

    # 2. BurstML
    t100, colours, offsets, params, lb, ub = make_burstml()
    times_ms = t100 * 1e-4
    np.savez(os.path.join(SHARED, "burstml.npz"), t100=t100, colours=colours, offsets=offsets, params=params, lb=lb, ub=ub,
             jmax=15, qmax=4.0, t_th=0.3, n_th=30.0, n_states=2, n_colours=2)
    ml = tttrlib.BurstML()
    ml.set_burst_data(times_ms.tolist(), [int(c) for c in colours], [int(o) for o in offsets])
    ml.set_n_states(2); ml.set_n_colours(2); ml.set_jmax(15); ml.set_qmax(4.0); ml.set_t_th(0.3); ml.set_n_th(30.0)
    plist = [list(map(float, p)) for p in params]

    def run_ml():
        return [ml.neg_log_likelihood(p) for p in plist]

    bench("burstml", "tttrlib", f"BurstML NLL, {len(offsets) - 1} bursts x {len(plist)} parameter sets (2 states, 2 colours, jmax 15)",
          run_ml, repeat=5, warmup=1, n_items=len(plist), unit="evaluation", dataset="simulated")

    # 3. FRET-2CDE
    macro, chan, bounds = make_two_cde()
    np.savez(os.path.join(SHARED, "two_cde.npz"), macro=macro, chan=chan, bounds=bounds, tau=30.0)
    d = tttrlib.TTTR()
    d.append_events(macro.astype(np.uint64), np.zeros(macro.size, np.uint16), chan, np.zeros(macro.size, np.int8), False, 0)
    d.header.set_macro_time_resolution(1.0)

    def run_2cde():
        eng = tttrlib.TwoCDE(d)
        eng.set_donor([0]); eng.set_acceptor([1])
        eng.compute(bounds, 30.0, tttrlib.TwoCDE.FRET_2CDE, tttrlib.TwoCDE.LAPLACE)
        return np.asarray(eng.two_cde)

    bench("two_cde", "tttrlib", f"FRET-2CDE (Laplace KDE, tau 30), {bounds.shape[0]} bursts x 120 photons",
          run_2cde, repeat=5, warmup=1, n_items=bounds.shape[0], unit="burst", dataset="simulated")

    # 4. 2D-FDC
    fmacro, fmicro = make_fdc()
    lags = np.array([150, 500, 2000], dtype=np.int64)
    ddT, t_min, t_max, L = 200, 0, 4096, 16
    np.savez(os.path.join(SHARED, "fdc2d.npz"), macro=fmacro, micro=fmicro, lags=lags, ddT=ddT, t_min=t_min, t_max=t_max, logt_imax=L)

    def run_fdc():
        out = np.zeros(lags.size * L * L, dtype=np.int64)
        tttrlib.fdc_scan_log(fmacro, fmicro, lags, ddT, t_min, t_max, L, 1, out, 1)
        return out

    bench("fdc2d", "tttrlib", f"2D-FDC log matrices, {fmacro.size} photons x {lags.size} lags",
          run_fdc, repeat=5, warmup=1, n_items=fmacro.size * lags.size, unit="photon-lag", dataset="simulated")

    # 5. CUSUM burst search
    ticks = make_cusum()
    np.savez(os.path.join(SHARED, "cusum.npz"), ticks=ticks, IB_khz=3.0, IT_khz=15.0, L=20, res=RES)
    tttr = tttr_from_ticks(ticks)

    def run_cusum():
        return np.asarray(tttr.burst_search_cusum_sprt(20, 3000.0, 5.0, 0.05, 0.05))

    bench("cusum", "tttrlib", f"CUSUM/SPRT burst search, {ticks.size} photons",
          run_cusum, repeat=5, warmup=1, n_items=ticks.size, unit="photon", dataset="simulated")

    with open(os.path.join(SHARED, "meta.json"), "w") as fh:
        json.dump({"tttrlib": tttrlib.__version__}, fh)


if __name__ == "__main__":
    main()
