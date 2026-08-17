#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Record astropy's Bayesian Blocks segmentation as a reference fixture.

Generates ``test/data/reference/bayesian_blocks_astropy_reference.npz`` for
``test_ab_burst_reference.py::TestBayesianBlocksAgainstAstropy``. Run it in an
environment that has astropy (it is deliberately not a dependency of the test
suite)::

    uv venv /tmp/astropy_env && uv pip install -p /tmp/astropy_env/bin/python astropy numpy
    /tmp/astropy_env/bin/python test/python/burstfilter/gen_ab_bayesian_blocks_astropy_reference.py

Inputs are stored beside the outputs so nothing depends on RNG stability.
astropy requires unique event times, so every case uses unique ticks; tttrlib's
kernel accepts duplicates, and that path is covered by the known-answer tests.
"""
import os

import numpy as np
from astropy import __version__ as astropy_version
from astropy.stats import bayesian_blocks

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "data", "reference",
                   "bayesian_blocks_astropy_reference.npz")

TICK = 1e-6  # seconds per tick


def ncp_prior_from_p0(p0, n):
    """Scargle 2013 eq. 21 -- the calibration both sides use."""
    return 4.0 - np.log(73.53 * p0 * n ** -0.478)


def main():
    rng = np.random.default_rng(20260817)
    cases = []
    specs = [
        # (n_background, duration_s, [(t0, length, n_photons)...], p0)
        (300, 1.0, [(0.40, 0.02, 80), (0.70, 0.005, 40)], 0.05),
        (500, 1.0, [(0.10, 0.01, 60), (0.55, 0.03, 120), (0.90, 0.002, 30)], 0.005),
        (2000, 2.0, [(0.30, 0.01, 90), (1.20, 0.05, 300)], 0.05),
        (150, 0.5, [], 0.05),                       # pure background
        (3000, 3.0, [(1.0, 0.004, 50)] * 1 + [(2.0, 0.02, 200)], 0.001),
        (40, 0.1, [(0.05, 0.005, 25)], 0.05),        # tiny
        (800, 1.0, [(0.5, 0.2, 800)], 0.05),         # one long bright block
    ]
    for n_bg, duration, bursts, p0 in specs:
        t = [rng.uniform(0.0, duration, n_bg)]
        for t0, length, n in bursts:
            t.append(rng.uniform(t0, t0 + length, n))
        ticks = np.unique(np.round(np.concatenate(t) / TICK).astype(np.int64))
        tsec = ticks * TICK
        ncp = ncp_prior_from_p0(p0, ticks.size)
        edges = bayesian_blocks(tsec, fitness="events", ncp_prior=ncp)
        # interior edges sit at midpoints between consecutive photons; the
        # block boundary index is the photon just after the edge
        idx = np.searchsorted(tsec, edges[1:-1])
        cases.append((ticks, p0, ncp, idx))

    payload = {"tick": TICK, "astropy_version": astropy_version, "n_cases": len(cases)}
    for i, (ticks, p0, ncp, idx) in enumerate(cases):
        payload[f"ticks_{i}"] = ticks
        payload[f"p0_{i}"] = p0
        payload[f"ncp_prior_{i}"] = ncp
        payload[f"change_points_{i}"] = idx.astype(np.int64)
    np.savez_compressed(OUT, **payload)
    print("wrote", OUT, "astropy", astropy_version, "cases", len(cases))


if __name__ == "__main__":
    main()
