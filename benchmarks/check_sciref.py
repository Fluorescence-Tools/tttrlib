#!/usr/bin/env python
"""Output identity of the scientific-Python benchmark pairs (run after
``bench_sciref.py`` and ``competitors/bench_sciref.py``): recomputes tttrlib's
outputs on the shared inputs and compares them with the reference outputs the
competitor saved. Writes results/shared/sciref/check.json.

Conventions allowed for: HDBSCAN end-to-end may differ where the reference's
unstable argsort orders tied MST edges (compared by adjusted Rand index; the
per-stage identity -- condensed tree from either side's tree -- is in the A/B
test); the k-means comparison starts both from the same k-means++ centres.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttrlib  # noqa: E402
from common import RESULTS  # noqa: E402
from bench_sciref import hdbscan_labels  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "sciref")


def rel(a, b):
    b = np.asarray(b, float)
    return float(np.max(np.abs(np.asarray(a, float) - b)) / max(float(np.max(np.abs(b))), 1e-300))


def ari(a, b):
    """Adjusted Rand index (no sklearn in the base env dependency list)."""
    a = np.asarray(a); b = np.asarray(b)
    ua, ia = np.unique(a, return_inverse=True)
    ub, ib = np.unique(b, return_inverse=True)
    m = np.zeros((ua.size, ub.size), dtype=np.int64)
    np.add.at(m, (ia, ib), 1)
    comb = lambda x: x * (x - 1) / 2.0
    s_ij = comb(m).sum(); s_a = comb(m.sum(1)).sum(); s_b = comb(m.sum(0)).sum(); s_n = comb(a.size)
    exp = s_a * s_b / s_n
    mx = 0.5 * (s_a + s_b)
    return float((s_ij - exp) / (mx - exp)) if mx != exp else 1.0


def main():
    ref = np.load(os.path.join(SHARED, "reference_outputs.npz"))
    v = {}

    d = np.load(os.path.join(SHARED, "watershed.npz"))
    got = tttrlib.watershed(d["image"], d["markers"], d["mask"], 1)
    ndiff = int(np.count_nonzero(np.asarray(got) != ref["watershed"]))
    v["watershed"] = {"differing_pixels": ndiff, "identical": ndiff == 0}
    seg = np.asarray(tttrlib.marching_squares(d["image"], float(d["level"]), False)).reshape(-1, 4)
    same = seg.shape == ref["marching_squares"].shape and np.array_equal(seg, ref["marching_squares"])
    v["marching_squares"] = {"n_segments": int(seg.shape[0]), "identical": bool(same),
                             "note": "segments and raster order equal skimage's _get_contour_segments"}

    d = np.load(os.path.join(SHARED, "richardson_lucy.npz"))
    got = tttrlib.richardson_lucy_2d(d["blurred"], d["psf"], int(d["n_iter"]), False, 0.0, False)
    r = rel(got, ref["richardson_lucy"])
    v["richardson_lucy"] = {"max_rel_diff": r, "identical": r < 1e-9}

    d = np.load(os.path.join(SHARED, "kmeans.npz"))
    c, l, s = tttrlib.kmeans(d["x"], int(d["k"]), d["uniforms"], 1, 300, 0.0)
    c = np.asarray(c).reshape(int(d["k"]), -1)
    rc = rel(c, ref["kmeans_centres"]); same_l = bool(np.array_equal(np.asarray(l), ref["kmeans_labels"]))
    ri = abs(float(s[0]) / float(ref["kmeans_inertia"]) - 1.0)
    v["kmeans"] = {"centres_max_rel_diff": rc, "labels_identical": same_l, "inertia_rel_diff": ri,
                   "identical": bool(rc < 1e-9 and same_l and ri < 1e-9)}

    d = np.load(os.path.join(SHARED, "hdbscan.npz"))
    lab = hdbscan_labels(d["x"], int(d["min_cluster_size"]), int(d["min_samples"]))
    a = ari(lab, ref["hdbscan_labels"])
    v["hdbscan"] = {"ari_vs_sklearn": a, "n_clusters": int(lab.max() + 1), "n_clusters_sklearn": int(ref["hdbscan_labels"].max() + 1),
                    "identical": bool(a >= 1.0 - 1e-12),      # same partition (label numbering is arbitrary)
                    "note": "partition compared by ARI; a value < 1 would be tied MST edges the reference orders with an unstable argsort"}

    d = np.load(os.path.join(SHARED, "kalman.npz"))
    x, P, D = tttrlib.kalman_filter(d["y"], d["x0"], d["P0"], d["Q"], float(d["dt"]), float(d["r_scale"]))
    rx, rP, rD = rel(x, ref["kalman_x"]), rel(P, ref["kalman_P"]), rel(D, ref["kalman_D"])
    v["kalman"] = {"x_max_rel_diff": rx, "P_max_rel_diff": rP, "D_max_rel_diff": rD,
                   "identical": bool(max(rx, rP, rD) < 1e-9), "note": "filterpy updates P in Joseph form; agreement to rounding"}

    d = np.load(os.path.join(SHARED, "hmm_lattice.npz"))
    lf = d["log_frame"]; T, K = lf.shape
    fwd = np.empty_like(lf)
    lp = tttrlib.hmm_forward_log(d["log_start"], d["log_trans"], lf, fwd)
    post = np.empty_like(lf); xi = np.zeros((K, K))
    tttrlib.hmm_backward_posteriors_xi(d["log_trans"], lf, fwd, lp, post, xi)
    states = np.empty(T, dtype=np.int64)
    vs = tttrlib.hmm_viterbi_log(d["log_start"], d["log_trans"], lf, states)
    v["hmm_lattice"] = {"logprob_diff": abs(float(lp) - float(ref["hmm_logprob"])),
                        "posteriors_max_abs_diff": float(np.max(np.abs(post - ref["hmm_posteriors"]))),
                        "xi_max_rel_diff": rel(xi, ref["hmm_xi"]),
                        "viterbi_score_diff": abs(float(vs) - float(ref["hmm_viterbi_score"])),
                        "viterbi_paths_identical": bool(np.array_equal(states, ref["hmm_states"]))}
    v["hmm_lattice"]["identical"] = bool(v["hmm_lattice"]["logprob_diff"] < 1e-8 and v["hmm_lattice"]["posteriors_max_abs_diff"] < 1e-10
                                         and v["hmm_lattice"]["xi_max_rel_diff"] < 1e-9 and v["hmm_lattice"]["viterbi_paths_identical"])

    d = np.load(os.path.join(SHARED, "phasor.npz"))
    gs = np.asarray(tttrlib.DecayPhasor.compute_phasor_bincounts_batch(d["counts"], float(d["frequency"]), 1, 1.0, 0.0))
    rg, rs = rel(gs[:, 0], ref["phasor_g"]), rel(gs[:, 1], ref["phasor_s"])
    v["phasor"] = {"g_max_rel_diff": rg, "s_max_rel_diff": rs, "identical": bool(max(rg, rs) < 1e-9)}

    for k, r in v.items():
        print(f"[{k}] {'IDENTICAL' if r['identical'] else 'differs'}  {json.dumps({kk: vv for kk, vv in r.items() if kk != 'identical'})}")
    with open(os.path.join(SHARED, "check.json"), "w") as fh:
        json.dump(v, fh, indent=2)


if __name__ == "__main__":
    main()
