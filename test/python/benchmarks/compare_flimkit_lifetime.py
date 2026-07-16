"""Benchmark TTTRlib Poisson-MLE lifetime maps against FLIMKit.

FLIMKit's fast one-component per-pixel estimator is a 200-point
least-squares grid scan, not a Poisson MLE. This benchmark therefore reports
both time and lifetime error; it never presents the two objectives as
mathematically identical. Both libraries receive the same photon realization,
IRF, bin width, and valid-pixel population. TTTRlib's VV/VH
input is an information-preserving binomial split whose exact sum is passed to
FLIMKit.

FLIMKit is an optional benchmark-only dependency. Example::

    VECLIB_MAXIMUM_THREADS=1 PYTHONPATH=/path/to/FLIMKit \
      python test/python/benchmarks/compare_flimkit_lifetime.py
"""

from __future__ import annotations

import argparse
import gc
import math
import statistics
import time
from collections.abc import Callable

import numpy as np
import tttrlib


def _measure(fn: Callable[[], np.ndarray], repeats: int):
    for _ in range(2):
        fn()
    samples = []
    output = None
    gc.disable()
    try:
        for _ in range(repeats):
            start = time.perf_counter_ns()
            output = np.asarray(fn()).copy()
            samples.append((time.perf_counter_ns() - start) / 1e6)
    finally:
        gc.enable()
    return output, min(samples), statistics.median(samples)


def _print_result(name, output, best_ms, median_ms, tau_ns, n_pixels, reference=None):
    error = output - tau_ns
    speedup = ""
    if reference is not None:
        speedup = f"{reference / median_ms:.2f}x"
    print(
        f"{name:<24}{median_ms:11.3f}{best_ms:11.3f}"
        f"{n_pixels * 1000.0 / median_ms:12.0f}{speedup:>10}"
        f"{np.mean(np.abs(error)):11.4f}"
        f"{math.sqrt(float(np.mean(error * error))):11.4f}"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pixels", type=int, default=4096)
    parser.add_argument("--bins", type=int, default=256)
    parser.add_argument("--dt-ns", type=float, default=0.05)
    parser.add_argument("--tau-ns", type=float, default=2.5)
    parser.add_argument("--photons", type=int, default=1000)
    parser.add_argument("--irf-bin", type=int, default=16)
    parser.add_argument("--repeats", type=int, default=9)
    parser.add_argument("--seed", type=int, default=20260716)
    args = parser.parse_args()

    side = math.isqrt(args.pixels)
    if side * side != args.pixels:
        parser.error("--pixels must be a perfect square for FLIMKit's image API")
    if not 0 <= args.irf_bin < args.bins:
        parser.error("--irf-bin must lie within the histogram")

    from flimkit.FLIM.fitters import fit_per_pixel

    rng = np.random.default_rng(args.seed)
    irf = np.zeros(args.bins, dtype=np.float64)
    irf[args.irf_bin] = 1.0
    basis = np.roll(
        np.exp(-np.arange(args.bins) * args.dt_ns / args.tau_ns),
        args.irf_bin,
    )
    probability = basis / basis.sum()
    pooled = rng.multinomial(
        args.photons, probability, size=args.pixels
    ).astype(np.int32)
    vv = rng.binomial(pooled, 0.5).astype(np.int32)
    jordi = np.ascontiguousarray(
        np.concatenate((vv, pooled - vv), axis=1), dtype=np.float64
    )
    if not np.array_equal(jordi[:, : args.bins] + jordi[:, args.bins :], pooled):
        raise RuntimeError("polarization split did not conserve photons")
    stack = pooled.reshape(side, side, args.bins)

    fit23 = tttrlib.Fit23(
        dt=args.dt_ns,
        irf=np.concatenate((irf, irf)),
        background=np.zeros(2 * args.bins),
        period=args.bins * args.dt_ns,
        g_factor=1.0,
        l1=0.0,
        l2=0.0,
        convolution_stop=args.bins - 1,
        soft_bifl_scatter_flag=False,
        p2s_twoIstar_flag=False,
    )
    x0 = np.array([args.tau_ns, 0.0, 0.0, 1.0])
    fixed = np.array([0, 1, 1, 1], dtype=np.int16)

    def run_tttrlib():
        return fit23.fit_many(jordi, x0, fixed=fixed)[:, 0]

    flimkit_common = dict(
        stack=stack,
        tcspc_res=args.dt_ns * 1e-9,
        n_bins=args.bins,
        irf_prompt=irf,
        has_tail=False,
        fit_bg=False,
        fit_sigma=False,
        global_popt=np.array([
            args.tau_ns * 1e-9,
            float(stack.max()),
            0.0,
        ]),
        n_exp=1,
        min_photons=1,
        tau_min_ns=max(0.001, args.tau_ns / 12.5),
        tau_max_ns=min(args.bins * args.dt_ns, args.tau_ns * 3.2),
        correct_pileup=False,
        n_sync=None,
        progress_callback=None,
        free_tau=False,
        tvb_profile=None,
        fit_tvb=False,
    )

    def run_flimkit_cpu():
        result = fit_per_pixel(use_gpu=False, gpu_backend=None, **flimkit_common)
        return result["tau_1"].ravel()

    competitors = [("FLIMKit grid CPU", run_flimkit_cpu)]
    try:
        from flimkit.GPU import get_backend

        backend = get_backend("mlx")

        def run_flimkit_mlx():
            result = fit_per_pixel(
                use_gpu="mlx", gpu_backend=backend, **flimkit_common
            )
            return result["tau_1"].ravel()

        competitors.append(("FLIMKit grid MLX", run_flimkit_mlx))
    except Exception as error:
        print(f"MLX backend unavailable: {error}")

    results = []
    for name, fn in competitors:
        results.append((name, *_measure(fn, args.repeats)))
    tt_output, tt_best, tt_median = _measure(run_tttrlib, args.repeats)

    print(
        f"\nLifetime map: {args.pixels} pixels x {args.bins} bins, "
        f"{args.photons} photons/pixel"
    )
    print("TTTRlib=continuous Poisson MLE; FLIMKit=200-point LSQ grid")
    print(
        f"{'path':<24}{'median ms':>11}{'best ms':>11}{'fits/s':>12}"
        f"{'speedup':>10}{'MAE ns':>11}{'RMSE ns':>11}"
    )
    print("-" * 90)
    for name, output, best_ms, median_ms in results:
        _print_result(
            name, output, best_ms, median_ms, args.tau_ns, args.pixels
        )
    strongest = min(result[3] for result in results)
    _print_result(
        "TTTRlib Fit23 MLE",
        tt_output,
        tt_best,
        tt_median,
        args.tau_ns,
        args.pixels,
        reference=strongest,
    )


if __name__ == "__main__":
    main()
