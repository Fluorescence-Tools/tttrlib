#!/usr/bin/env python
"""FLIMKit benchmarks (isolated flimkit venv, Python 3.12).

FLIMKit (github.com/alex1075/FLIMKit) is a full FLIM toolkit with per-pixel
reconvolution fitting (scipy differential-evolution / least-squares) and phasor
analysis. We fit the SAME decay image tttrlib produced (results/shared/) so the
per-pixel lifetime numbers are directly comparable to tttrlib's fit_map.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# FLIMKit is a source tree, not pip-installed
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "junk", "FLIMKit"))

from common import timeit, record, RESULTS   # noqa: E402

SHARED = os.path.join(RESULTS, "shared")
ENV = "venv-flimkit"


def bench_lifetime():
    from flimkit.FLIM.fitters import fit_summed, fit_per_pixel   # noqa: E402
    z = np.load(os.path.join(SHARED, "decay_image.npz"))
    img = z["decay_image"].astype(np.float64)          # (Y, X, H)
    irf = z["irf"].astype(np.float64)
    dt_ns = float(z["dt_ns"])
    Y, X, H = img.shape
    npix = Y * X
    tcspc_res = dt_ns * 1e-9                            # seconds/bin

    # summed-decay global fit (fast, one curve) -> also gives global_popt for per-pixel
    summed = img.sum(axis=(0, 1))
    popt, summary = fit_summed(summed, tcspc_res, H, irf,
                               has_tail=False, fit_bg=True, fit_sigma=False,
                               n_exp=1, tau_min_ns=0.2, tau_max_ns=8.0,
                               optimizer="de", n_restarts=2, de_maxiter=200,
                               workers=1)
    print("   [FLIMKit] summed tau_mean=%.3f ns" % summary.get("tau_mean_amp_ns", float("nan")))

    def run_summed():
        fit_summed(summed, tcspc_res, H, irf, has_tail=False, fit_bg=True,
                   fit_sigma=False, n_exp=1, tau_min_ns=0.2, tau_max_ns=8.0,
                   optimizer="de", n_restarts=2, de_maxiter=200, workers=1)
    b, m, a = timeit(run_summed, repeat=3, warmup=1)
    record("fit_curve", "FLIMKit (summed)", "single-curve mono-exp reconv. (scipy DE)",
           b, m, a, n_items=1, unit="fits", dataset="synthetic 256-bin decay",
           env=ENV)

    # per-pixel reconvolution fit (the heavy path; CPU, no GPU)
    def run_map():
        return fit_per_pixel(img, tcspc_res, H, irf, has_tail=False, fit_bg=True,
                             fit_sigma=False, global_popt=popt, n_exp=1,
                             min_photons=20, use_gpu=False)   # match tttrlib fit_map
    maps = run_map()
    b, m, a = timeit(run_map, repeat=1, warmup=0)
    record("lifetime_image", "FLIMKit (per-pixel, CPU)", "decay image -> reconv. tau map (CPU)",
           b, m, a, n_items=npix, unit="pixels", dataset="pq_ht3_clsm.ht3",
           env=ENV, extra={"method": "reconvolution-scipy"})

    # GPU per-pixel fit (MLX backend on the machine's GPU), if a backend loads
    try:
        from flimkit.GPU import get_backend
        from flimkit.FLIM.fitters import warmup_gpu_backend
        backend = get_backend("auto")
        if backend is not None:
            gpu_name = type(backend).__name__
            try:
                warmup_gpu_backend()
            except Exception:
                pass

            def run_gpu():
                return fit_per_pixel(img, tcspc_res, H, irf, has_tail=False,
                                     fit_bg=True, fit_sigma=False, global_popt=popt,
                                     n_exp=1, min_photons=20, use_gpu=True)
            run_gpu()   # warm
            gb, gm, ga = timeit(run_gpu, repeat=2, warmup=0)
            record("lifetime_image", "FLIMKit (per-pixel, GPU)",
                   "decay image -> reconv. tau map (GPU)",
                   gb, gm, ga, n_items=npix, unit="pixels", dataset="pq_ht3_clsm.ht3",
                   env=ENV, extra={"method": "reconvolution", "gpu_backend": gpu_name})
            print("   [FLIMKit GPU backend: %s]" % gpu_name)
    except Exception as e:
        print("   [FLIMKit GPU path unavailable: %s: %s]" % (type(e).__name__, e))


if __name__ == "__main__":
    print("=" * 70)
    print("FLIMKit benchmarks (%s)" % ENV)
    print("=" * 70)
    try:
        bench_lifetime()
    except Exception as e:
        import traceback
        print("!! failed: %s: %s" % (type(e).__name__, e))
        traceback.print_exc()
