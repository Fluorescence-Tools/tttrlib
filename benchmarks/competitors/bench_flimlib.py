#!/usr/bin/env python
"""flimlib benchmarks (runs in the isolated flimlib venv).

flimlib is the C fitting library behind FLIMfit / FLIMJ. It fits the SAME
shared decay / decay-image that tttrlib produced (results/shared/), so the
lifetime numbers are directly comparable.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, record, timeit, RESULTS   # noqa: E402

import flimlib   # noqa: E402

SHARED = os.path.join(RESULTS, "shared")
ENV = "venv-flimlib"


def bench_single_curve():
    z = np.load(os.path.join(SHARED, "single_decay.npz"))
    decay = z["decay"].astype(np.float32)
    irf = z["irf"].astype(np.float32)
    dt = float(z["dt_ns"])
    NFIT = int(z["n_fits"])
    param0 = np.array([1.0, decay.max(), 1.5], dtype=np.float32)   # [Z, A, tau]

    # sanity
    r = flimlib.GCI_marquardt_fitting_engine(dt, decay, param0.copy(), instr=irf,
                                             noise_type="NOISE_POISSON_FIT")
    print("   [flimlib LMA sanity] tau=%.3f" % r.param[2])

    def run_one():
        flimlib.GCI_marquardt_fitting_engine(dt, decay, param0.copy(), instr=irf,
                                             noise_type="NOISE_POISSON_FIT",
                                             compute_covar=False, compute_erraxes=False)
    best, mean, allt = timeit(run_one, repeat=3, warmup=1)
    record("fit_curve", "flimlib (LMA)", "single-curve mono-exp reconv. LMA",
           best, mean, allt, n_items=1, unit="fits",
           dataset="synthetic 256-bin decay", env=ENV,
           extra={"recovered_tau": float(r.param[2])})

    # batch: stack NFIT copies, one vectorized C call
    stack = np.tile(decay, (NFIT, 1)).astype(np.float32)
    params = np.tile(param0, (NFIT, 1)).astype(np.float32)

    def run_batch():
        flimlib.GCI_marquardt_fitting_engine(dt, stack, params.copy(), instr=irf,
                                             noise_type="NOISE_POISSON_FIT",
                                             compute_covar=False, compute_erraxes=False,
                                             compute_fitted=False, compute_residuals=False)
    bb, bm, ball = timeit(run_batch, repeat=3, warmup=1)
    record("fit_curve", "flimlib (LMA batch)", "batch mono-exp reconv. LMA",
           bb / NFIT, bm / NFIT, [t / NFIT for t in ball],
           n_items=1, unit="fits", dataset="synthetic 256-bin decay x%d" % NFIT,
           env=ENV, extra={"n_fits": NFIT})

    # RLD (triple-integral) — fast, no reconvolution
    def run_rld():
        flimlib.GCI_triple_integral_fitting_engine(dt, stack,
                                                   compute_fitted=False,
                                                   compute_residuals=False)
    rb, rm, rall = timeit(run_rld, repeat=3, warmup=1)
    record("fit_curve", "flimlib (RLD)", "batch mono-exp RLD (no reconv.)",
           rb / NFIT, rm / NFIT, [t / NFIT for t in rall],
           n_items=1, unit="fits", dataset="synthetic 256-bin decay x%d" % NFIT,
           env=ENV, extra={"n_fits": NFIT})


def bench_lifetime_image():
    z = np.load(os.path.join(SHARED, "decay_image.npz"))
    img = z["decay_image"].astype(np.float32)          # (Y, X, H)
    irf = z["irf"].astype(np.float32)
    dt = float(z["dt_ns"])
    Y, X, H = img.shape
    npix = Y * X

    # RLD — fast lifetime estimate, comparable to tttrlib moments
    def run_rld():
        flimlib.GCI_triple_integral_fitting_engine(dt, img, compute_fitted=False,
                                                   compute_residuals=False)
    b, m, a = timeit(run_rld, repeat=3, warmup=1)
    record("lifetime_image", "flimlib (RLD)", "decay image -> RLD tau map",
           b, m, a, n_items=npix, unit="pixels", dataset="pq_ht3_clsm.ht3",
           env=ENV, extra={"method": "rapid-lifetime-determination"})

    # Phasor — fast
    def run_phasor():
        flimlib.GCI_Phasor(dt, img, compute_fitted=False, compute_residuals=False)
    b, m, a = timeit(run_phasor, repeat=3, warmup=1)
    record("lifetime_image", "flimlib (phasor)", "decay image -> phasor tau map",
           b, m, a, n_items=npix, unit="pixels", dataset="pq_ht3_clsm.ht3",
           env=ENV, extra={"method": "phasor"})

    # LMA reconvolution — the accurate, slow path (compare to tttrlib fit_map)
    param0 = np.zeros((Y, X, 3), dtype=np.float32)
    param0[..., 0] = 0.0
    param0[..., 1] = img.max(axis=-1)
    param0[..., 2] = 2.0

    def run_lma():
        flimlib.GCI_marquardt_fitting_engine(dt, img, param0.copy(), instr=irf,
                                             noise_type="NOISE_POISSON_FIT",
                                             compute_covar=False, compute_erraxes=False,
                                             compute_fitted=False, compute_residuals=False,
                                             compute_alpha=False)
    b, m, a = timeit(run_lma, repeat=2, warmup=0)
    record("lifetime_image", "flimlib (LMA)", "decay image -> reconv. LMA tau map",
           b, m, a, n_items=npix, unit="pixels", dataset="pq_ht3_clsm.ht3",
           env=ENV, extra={"method": "reconvolution-LMA"})


if __name__ == "__main__":
    print("=" * 70)
    print("flimlib benchmarks (%s)" % ENV)
    print("=" * 70)
    for fn in [bench_single_curve, bench_lifetime_image]:
        try:
            fn()
        except Exception as e:
            import traceback
            print("!! %s failed: %s: %s" % (fn.__name__, type(e).__name__, e))
            traceback.print_exc()
