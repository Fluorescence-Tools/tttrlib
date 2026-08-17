#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Runs FRETBursts' burst search on arrays from an .npz and writes the result.

Driven as a subprocess by ``test_ab_burst_reference.py`` inside the FRETBursts
virtualenv (``benchmarks/.venvs/fretbursts``), because FRETBursts is not a
dependency of this project's environment. One process handles many cases, since
importing FRETBursts costs seconds. Protocol::

    python _fretbursts_ab_driver.py IN.npz OUT.npz

IN.npz keys (``i`` = 0 .. n_cases-1):
    n_cases
    times_i           int64 timestamps (ticks) of the whole stream, sorted
    L_i, m_i, T_i     sliding-window parameters (T in ticks)
    mask_a_i, mask_b_i optional boolean masks selecting two photon streams for
                      the dual-channel (AND-gate) search

OUT.npz keys:
    bursts_c_i   (n, 4) int64 from the compiled ``bsearch`` on ``times``
    bursts_py_i  (n, 4) int64 from the pure-Python ``bsearch_py`` on ``times``
    dcbs_i       (k, 2) int64 inclusive [istart, istop] into ``times`` of the
                 AND-gated bursts, present when both masks are given
"""
import sys
import warnings

import numpy as np

warnings.filterwarnings("ignore")
import fretbursts.phtools.burstsearch as bs  # noqa: E402


def main(inp, outp):
    d = np.load(inp)
    out = {}
    for i in range(int(d["n_cases"])):
        times = np.asarray(d[f"times_{i}"], dtype=np.int64)
        L, m, T = int(d[f"L_{i}"]), int(d[f"m_{i}"]), int(d[f"T_{i}"])
        out[f"bursts_c_{i}"] = np.asarray(bs.bsearch_c(times, L, m, T, verbose=False), dtype=np.int64).reshape(-1, 4)
        out[f"bursts_py_{i}"] = np.asarray(bs.bsearch_py(times, L, m, T, verbose=False), dtype=np.int64).reshape(-1, 4)
        if f"mask_a_{i}" in d and f"mask_b_{i}" in d:
            res = []
            ba = bs.Bursts(bs.bsearch_c(times[d[f"mask_a_{i}"]], L, m, T, verbose=False))
            bb = bs.Bursts(bs.bsearch_c(times[d[f"mask_b_{i}"]], L, m, T, verbose=False))
            if ba.num_bursts and bb.num_bursts:
                gated = ba.and_gate(bb)
                # the AND-gate is decided in time; photons of the whole stream
                # inside [start, stop] belong to the coincident burst
                for start, stop in zip(gated.start, gated.stop):
                    i0 = int(np.searchsorted(times, start, side="left"))
                    i1 = int(np.searchsorted(times, stop, side="right")) - 1
                    res.append((i0, i1))
            out[f"dcbs_{i}"] = np.asarray(res, dtype=np.int64).reshape(-1, 2)
    np.savez(outp, **out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
