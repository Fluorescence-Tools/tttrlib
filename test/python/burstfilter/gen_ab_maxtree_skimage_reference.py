#!/usr/bin/env python
"""Record scikit-image's max-tree of 1-D signals for ``TestMaxTreeAgainstSkimage``.

Runs under the sciref venv (``benchmarks/.venvs/sciref/bin/python``). Signals
are generated here with NumPy and stored beside the outputs. skimage's
``max_tree`` returns a parent array + traversal order; the component set is
derived from it: a pixel is *canonical* when its parent has a strictly lower
value (or it is the root), and a component is the run of pixels whose
canonical ancestors include it (a child lies inside its parent). Stored per case: the signal, the sorted
(level, lo, hi, parent_level, parent_lo) tuples.

    benchmarks/.venvs/sciref/bin/python test/python/burstfilter/gen_ab_maxtree_skimage_reference.py
"""
import os

import numpy as np
import skimage
from skimage.morphology import max_tree

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "reference",
                   "maxtree_skimage_reference.npz")


def components(x):
    P, _ = max_tree(x, connectivity=1)
    n = x.size
    canon = np.empty(n, dtype=np.int64)
    for p in range(n):
        q = p
        while not (P[q] == q or x[P[q]] < x[q]):
            q = P[q]
        canon[p] = q
    # a component is every pixel whose chain of canonical ancestors passes
    # through its canonical pixel (children are inside their parents)
    comps = {}
    for p in range(n):
        c = canon[p]
        while True:
            lo, hi = comps.get(c, (p, p))
            comps[c] = (min(lo, p), max(hi, p))
            if P[c] == c:
                break
            c = canon[P[c]]
    rows = []
    for c, (lo, hi) in comps.items():
        if P[c] == c:                                  # root
            rows.append((int(x[c]), lo, hi, -1, -1))
        else:
            pcan = canon[P[c]]
            rows.append((int(x[c]), lo, hi, int(x[pcan]), comps[pcan][0]))
    return np.array(sorted(rows), dtype=np.int64)


def main():
    rng = np.random.default_rng(5)
    cases = {
        "tiny": np.array([1, 3, 3, 2, 5, 5, 1, 4, 0]),
        "random_small_alphabet": rng.integers(0, 6, 400),
        "random_wide_alphabet": rng.integers(0, 1024, 3000),
        "bursty": np.clip(np.round(rng.normal(300, 20, 5000) + 400 * np.exp(-((np.arange(5000)[:, None]
                          - rng.uniform(0, 5000, 25)[None, :]) ** 2) / (2 * 15.0 ** 2)).sum(1)), 0, 1023).astype(int),
        "flat": np.full(50, 7),
        "monotone": np.arange(30),
    }
    out = {"cases": np.array(list(cases)), "skimage_version": np.array(skimage.__version__)}
    for k, x in cases.items():
        x = np.asarray(x, dtype=np.int64)
        out[f"{k}/signal"] = x
        out[f"{k}/components"] = components(x)
        print(k, x.size, "samples ->", out[f"{k}/components"].shape[0], "components")
    np.savez(OUT, **out)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
