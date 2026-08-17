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

    d = np.load(os.path.join(SHARED, "max_tree.npz"))
    nodes = np.asarray(tttrlib.max_tree_1d(d["levels"]), dtype=np.int64)
    par = nodes[:, 3]
    comps = np.stack([nodes[:, 0], nodes[:, 1], nodes[:, 2],
                      np.where(par >= 0, nodes[np.maximum(par, 0), 0], -1),
                      np.where(par >= 0, nodes[np.maximum(par, 0), 1], -1)], axis=1)
    comps = comps[np.lexsort(comps.T[::-1])]
    same = comps.shape == ref["max_tree_components"].shape and np.array_equal(comps, ref["max_tree_components"])
    v["max_tree"] = {"n_components": int(comps.shape[0]), "identical": bool(same),
                     "note": "component set (level, lo, hi, parent level, parent lo) equals skimage's max_tree"}

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

    d = np.load(os.path.join(SHARED, "hmm_vb.npz"))
    K = d["seed_B"].shape[0]
    lb_at_ours = float(ref["hmm_vb_lower_bound_at_tttrlib_posterior"]); lb = float(ref["hmm_vb_lower_bound"])
    from scipy.special import digamma, gammaln
    ap, at, ao = d["alpha_prior"], d["alpha_trans"], d["alpha_obs"]
    tp = np.exp(digamma(ap) - digamma(ap.sum())); ta = np.exp(digamma(at) - digamma(at.sum(1, keepdims=True))); to = np.exp(digamma(ao) - digamma(ao.sum(1, keepdims=True)))
    off = np.concatenate([[0], np.cumsum(d["lengths"])]); X = d["X"]; tot = 0.0
    for i in range(len(d["lengths"])):
        s_ = X[off[i]:off[i + 1]]
        a = tp * to[:, s_[0]]; c = a.sum(); tot += np.log(c); a = a / c
        for k in range(1, len(s_)):
            a = (a @ ta) * to[:, s_[k]]; c = a.sum(); tot += np.log(c); a = a / c
    kl = lambda a, b: (gammaln(a.sum()) - gammaln(a).sum() - gammaln(b.sum()) + gammaln(b).sum() + ((a - b) * (digamma(a) - digamma(a.sum()))).sum())
    H = tot - kl(ap, np.ones(K)) - sum(kl(at[i], np.ones(K)) for i in range(K)) - sum(kl(ao[i], np.ones(ao.shape[1])) for i in range(K))
    v["hmm_vb"] = {"alpha_prior_max_rel_diff": rel(ap, ref["hmm_vb_alpha_prior"]), "alpha_trans_max_rel_diff": rel(at, ref["hmm_vb_alpha_trans"]),
                   "alpha_obs_max_rel_diff": rel(ao, ref["hmm_vb_alpha_obs"]),
                   "beal_bound_at_tttrlib_posterior_vs_hmmlearn": abs(H - lb_at_ours),
                   "hmmlearn_bound_at_its_optimum_minus_at_ours": lb - lb_at_ours,
                   "tttrlib_elbo_vs_hmmlearn_bound_at_our_posterior": abs(float(d["elbo"]) - lb_at_ours),
                   "elbo_normalised_minus_hmmlearn_bound": float(d["elbo_normalised"]) - lb, "K(K-1)/2": K * (K - 1) / 2}
    v["hmm_vb"]["identical"] = bool(v["hmm_vb"]["beal_bound_at_tttrlib_posterior_vs_hmmlearn"] < 1e-8
                                     and max(v["hmm_vb"]["alpha_prior_max_rel_diff"], v["hmm_vb"]["alpha_trans_max_rel_diff"], v["hmm_vb"]["alpha_obs_max_rel_diff"]) < 2e-3
                                     and v["hmm_vb"]["tttrlib_elbo_vs_hmmlearn_bound_at_our_posterior"] < 1e-8
                                     and abs(v["hmm_vb"]["elbo_normalised_minus_hmmlearn_bound"] - K * (K - 1) / 2) < 0.05)
    v["hmm_vb"]["note"] = "elbo == hmmlearn's lower bound at tttrlib's posterior to rounding; posteriors agree to ~1e-3 (engine iterates on row-normalised A~); the iteration's elbo_normalised is K(K-1)/2 nat above the bound (okf/design/hmmvb-elbo-decision.md)"

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
