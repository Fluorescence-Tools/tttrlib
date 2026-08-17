#!/usr/bin/env python
"""PyBroMo simulation benchmark (isolated pybromo venv).

PyBroMo is the established Python Brownian-motion single-molecule diffusion +
photon simulator (the pyBroMo/FRETBursts ecosystem). We run a workload matched
to tttrlib's SimEngine (same N, T, D, step count, box, PSF width, brightness,
background) from results/shared/sim_spec.json and time diffusion + timestamps.
"""
import json
import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import timeit, record, RESULTS   # noqa: E402

import pybromo as pbm   # noqa: E402

SHARED = os.path.join(RESULTS, "shared")
ENV = "venv-pybromo"


def bench_sim():
    spec = json.load(open(os.path.join(SHARED, "sim_spec.json")))
    N = int(spec["n_molecules"])
    T = float(spec["T_s"])
    t_step = float(spec["dt_s"])
    D = float(spec["D_m2_per_s"])
    max_rate = float(spec["max_rate_cps"])
    bg_rate = float(spec["bg_cps"])
    box_h = float(spec["box_half_um"]) * 1e-6
    w0 = float(spec["psf_w0_um"]) * 1e-6
    n_steps = int(spec["n_steps"])

    import time
    import tables

    def run(run_id):
        tmp = tempfile.mkdtemp(prefix="pybromo_bench_%d_" % run_id)
        try:
            box = pbm.Box(x1=-box_h, x2=box_h, y1=-box_h, y2=box_h,
                          z1=-2 * box_h, z2=2 * box_h)
            P = pbm.Particles.from_specs(num_particles=(N,), D=(D,), box=box, seed=1)
            # PyBroMo's emission is the Gaussian PSF *squared* (excitation x
            # detection), so tttrlib's exp(-2 r^2 / w0^2) needs s = w0/sqrt(2),
            # not w0/2 (which is 2^1.5 less volume and half the diffusion time
            # -- the two sides then time different physics). Same setting as
            # test/python/simulation/gen_pybromo_reference.py.
            psf = pbm.GaussianPSF(sx=w0 / 2 ** 0.5, sy=w0 / 2 ** 0.5, sz=3 * w0 / 2 ** 0.5)
            S = pbm.ParticlesSimulation(t_step=t_step, t_max=T, particles=P,
                                        box=box, psf=psf)
            S.simulate_diffusion(total_emission=False, save_pos=False,
                                 path=tmp, verbose=False)
            S.simulate_timestamps_mix(max_rates=[max_rate], populations=[slice(0, N)],
                                      bg_rate=bg_rate, seed=1, scale=10, path=tmp)
            nph = int(S._timestamps.shape[0])
            return nph
        finally:
            tables.file._open_files.close_all()   # PyBroMo leaves stores open
            shutil.rmtree(tmp, ignore_errors=True)

    times = []
    nph = 0
    for i in range(3):
        t0 = time.perf_counter()
        nph = run(i)
        times.append(time.perf_counter() - t0)
    best, mean = min(times), sum(times) / len(times)
    print("   [PyBroMo] %d timestamps, %d steps, %d particles, best=%.2fs"
          % (nph, n_steps, N, best))
    record("simulation", "PyBroMo", "%d molecules, 1s diffusion+photons" % N,
           best, mean, times, n_items=n_steps, unit="steps", dataset="synthetic",
           env=ENV, extra={"n_molecules": N, "T_s": T, "n_photons": nph,
                           "n_steps": n_steps})


if __name__ == "__main__":
    print("=" * 70)
    print("PyBroMo benchmarks (%s)" % ENV)
    print("=" * 70)
    try:
        bench_sim()
    except Exception as e:
        import traceback
        print("!! failed: %s: %s" % (type(e).__name__, e))
        traceback.print_exc()
