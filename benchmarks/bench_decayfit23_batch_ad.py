"""A/B: DecayFit23's exact (AD) gradient, in the parallel batch/pixel path.

Companion to bench_decayfit23_ad.py, which measures one fit at a time through
the Python/SWIG boundary. This measures the path a real per-pixel or
per-burst *batch* actually uses -- ``DecayFit2("fit23", ...).fit_many()`` --
which crosses into C++ once and then runs every row through
``tttrlib::parallel_for`` (modules/util/include/ParallelFor.h), a hand-rolled
thread pool, not OpenMP; batches under 1024 rows run on a single worker
(DecayFitModel.cpp's batch_threads()). DecayFit23.cpp's thread_local
fit_signals/fit_corrections/fit_settings -- which decay23_gradient also reads
-- give each worker its own copy, so the analytic gradient is exercised
per-thread with no shared mutable state.

Same two-build protocol as bench_decayfit23_ad.py: run once with
`bfgs_o.set_gradient(decay23_gradient);` active and once with it commented
out, `pip install -e . --no-build-isolation` between the two.

    python benchmarks/bench_decayfit23_batch_ad.py [n_rows]
"""
from __future__ import division

import sys
import time

import numpy as np

import tttrlib


def cpu_ms(fn, reps=3):
    """process_time() sums CPU across every worker thread fit_many spawns --
    total work done, not what the caller waits for."""
    best = float("inf")
    for _ in range(reps):
        t0 = time.process_time()
        fn()
        best = min(best, time.process_time() - t0)
    return best * 1e3


def wall_ms(fn, reps=3):
    """perf_counter() is wall clock -- what the caller actually waits for,
    including whatever parallel_for's thread pool buys back."""
    best = float("inf")
    for _ in range(reps):
        t0 = time.perf_counter()
        fn()
        best = min(best, time.perf_counter() - t0)
    return best * 1e3


def main():
    n_rows = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    # 128 bins at 32 ps is a 4.1 ns period, comfortably above the tau=2.1 ns
    # ground truth -- see test_decay_fit_interface.py's _round_trip_problem
    # docstring for why 64 bins (a 2.048 ns period) rails every fit instead.
    n_bins = 128
    dt = 0.032

    irf = np.zeros(2 * n_bins)
    irf[0] = 1.0
    irf[n_bins] = 1.0
    setup = tttrlib.setup_vector("fit23", dt=dt, period=n_bins * dt,
                                 soft_bifl_scatter_flag=False)
    fit = tttrlib.DecayFit2("fit23", setup, irf.tolist())

    problem = tttrlib.DecayFitProblem(2, n_bins, dt)
    problem.irf = tttrlib.VectorDouble(irf.tolist())
    problem.background = tttrlib.VectorDouble(np.zeros(2 * n_bins).tolist())
    # normM scales the model by Sp+Ss, integrated from problem.data -- so
    # evaluate() needs a nonzero seed here or the model comes back all zero.
    seed = np.concatenate([1000 * np.exp(-np.arange(n_bins) * dt / 2.1) + 1] * 2)
    problem.data = tttrlib.VectorDouble(seed.tolist())

    # Ground truth to simulate from: tau=2.1, no gamma/scatter, r0=0.4,
    # rho=1.2 -- a realistic single-molecule anisotropy pixel.
    fit.evaluate([2.1, 0.0, 0.4, 1.2], problem)
    model = np.asarray(problem.model)
    model = model / model.sum() * 2.0e4

    rng = np.random.default_rng(20260813)
    matrix = rng.poisson(model, size=(n_rows, model.size)).astype(float)
    # Built once, outside the timed closure: converting a 2M-element NumPy
    # array to a Python list is itself ~hundreds of ms (SWIG's std::vector
    # typemap iterates element-by-element -- see okf/testing/benchmarking.md's
    # "SWIG vector-marshalling trap"), and that cost has nothing to do with
    # the fit -- charging it to every timed call would drown out the thing
    # this benchmark exists to measure.
    matrix_flat = matrix.flatten().tolist()

    # tau AND gamma free -- the general BFGS branch decay23_gradient is
    # registered on, matching bench_decayfit23_ad.py's single-fit case.
    constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, -1, -1]))
    x0 = [1.0, 0.05, 0.4, 1.2]

    def run_batch():
        return fit.fit_many(problem, matrix_flat, n_rows,
                            int(problem.total_size()), x0, constraints)

    out = run_batch()
    n_par = fit.n_parameters(problem)
    taus = [out.parameters[r * n_par] for r in range(n_rows)]
    print("n_rows = %d, n_bins = %d (period-bound, general BFGS branch)" % (n_rows, n_bins))
    print("median tau = %.4f (truth 2.100), objective mean = %.4f" %
          (float(np.median(taus)), float(np.mean(out.objective))))

    w_ms = wall_ms(run_batch, reps=5)
    c_ms = cpu_ms(run_batch, reps=5)
    print("\nmin wall time for the whole batch, best of 5: %.2f ms (%.5f ms/row)" %
          (w_ms, w_ms / n_rows))
    print("min total CPU time (all worker threads), best of 5: %.2f ms (%.5f ms/row)" %
          (c_ms, c_ms / n_rows))
    print("thread_count env override: TTTRLIB_NUM_THREADS (unset = all cores)")


if __name__ == "__main__":
    main()
