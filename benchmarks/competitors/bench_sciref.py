#!/usr/bin/env python
"""Scientific-Python competitors for the general kernels: scikit-image, scikit-learn,
filterpy, hmmlearn, phasorpy. Loads the shared inputs written by ``bench_sciref.py``,
times the reference on identical data, and saves its outputs for check_sciref.py.
Run inside the ``sciref`` venv (``build_envs.sh``).
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from common import bench, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "sciref")
OUT = {}


def bench_skimage():
    from skimage.segmentation import watershed
    from skimage.measure._find_contours_cy import _get_contour_segments
    from skimage.restoration import richardson_lucy
    d = np.load(os.path.join(SHARED, "watershed.npz"))
    image, markers, mask, level = d["image"], d["markers"], d["mask"].astype(bool), float(d["level"])
    n = image.size
    OUT["watershed"] = watershed(image, markers, mask=mask, connectivity=1)
    bench("watershed", "scikit-image", "watershed 1024x1024, 200 markers, connectivity 1",
          lambda: watershed(image, markers, mask=mask, connectivity=1), repeat=5, warmup=1,
          n_items=n, unit="pixel", dataset="simulated")
    # find_contours = these segments + assembling them into polylines; the kernel
    # tttrlib ports is the segment extraction, compared as such (see the A/B test)
    img = np.ascontiguousarray(image, dtype=np.float64)
    seg = _get_contour_segments(img, level, False, np.ones(image.shape, dtype=np.uint8))
    OUT["marching_squares"] = np.asarray(seg, dtype=np.float64).reshape(-1, 4) if len(seg) else np.zeros((0, 4))
    bench("marching_squares", "scikit-image (segments)", "marching squares 1024x1024, one level",
          lambda: _get_contour_segments(img, level, False, np.ones(image.shape, dtype=np.uint8)),
          repeat=5, warmup=1, n_items=n, unit="pixel", dataset="simulated")

    d = np.load(os.path.join(SHARED, "richardson_lucy.npz"))
    blurred, psf, n_iter = d["blurred"], d["psf"], int(d["n_iter"])
    OUT["richardson_lucy"] = richardson_lucy(blurred, psf, num_iter=n_iter, clip=False, filter_epsilon=None)
    bench("richardson_lucy", "scikit-image", f"Richardson-Lucy 512x512, 15x15 PSF, {n_iter} iterations",
          lambda: richardson_lucy(blurred, psf, num_iter=n_iter, clip=False, filter_epsilon=None),
          repeat=5, warmup=1, n_items=blurred.size, unit="pixel", dataset="simulated", extra={"iterations": n_iter})


def bench_skimage_max_tree():
    from skimage.morphology import max_tree
    d = np.load(os.path.join(SHARED, "max_tree.npz"))
    x = d["levels"].astype(np.int64)

    def run():
        return max_tree(x, connectivity=1)

    P, T = run()
    # component set derived from the parent array (canonical pixel = parent has a lower value)
    n = x.size
    canon = np.empty(n, dtype=np.int64)
    for p in range(n):
        q = p
        while not (P[q] == q or x[P[q]] < x[q]):
            q = P[q]
        canon[p] = q
    lo = np.full(n, n, dtype=np.int64); hi = np.full(n, -1, dtype=np.int64)
    for p in range(n):
        c = canon[p]
        while True:
            lo[c] = min(lo[c], p); hi[c] = max(hi[c], p)
            if P[c] == c:
                break
            c = canon[P[c]]
    canon_ids = np.flatnonzero(hi >= 0)
    parent_lo = np.array([-1 if P[c] == c else lo[canon[P[c]]] for c in canon_ids])
    parent_level = np.array([-1 if P[c] == c else x[canon[P[c]]] for c in canon_ids])
    comps = np.stack([x[canon_ids], lo[canon_ids], hi[canon_ids], parent_level, parent_lo], axis=1)
    OUT["max_tree_components"] = comps[np.lexsort(comps.T[::-1])]
    bench("max_tree", "scikit-image max_tree", f"1-D max-tree (component tree), {x.size} samples, 1024 levels",
          run, repeat=3, warmup=1, n_items=x.size, unit="sample", dataset="simulated")


def bench_sklearn():
    import warnings
    from sklearn.cluster import KMeans, HDBSCAN
    d = np.load(os.path.join(SHARED, "kmeans.npz"))
    x, k, init = d["x"], int(d["k"]), d["init_centres"]

    def run_km():
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            return KMeans(n_clusters=k, init=init, n_init=1, algorithm="lloyd", tol=0.0, max_iter=300).fit(x)

    km = run_km()
    OUT["kmeans_centres"], OUT["kmeans_labels"], OUT["kmeans_inertia"] = km.cluster_centers_, km.labels_, km.inertia_
    bench("kmeans", "scikit-learn KMeans (lloyd, given init)", f"k-means n={x.shape[0]} d={x.shape[1]} k={k}, Lloyd from the same k-means++ centres",
          run_km, repeat=5, warmup=1, n_items=x.shape[0], unit="sample", dataset="simulated",
          extra={"n_iter": int(km.n_iter_), "note": "seeding excluded: init=array"})

    def run_km_full():
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            return KMeans(n_clusters=k, init="k-means++", n_init=1, algorithm="lloyd", tol=0.0, max_iter=300,
                          random_state=0).fit(x)

    kf = run_km_full()
    bench("kmeans", "scikit-learn KMeans (k-means++ + lloyd)", f"k-means n={x.shape[0]} d={x.shape[1]} k={k}, one init to convergence",
          run_km_full, repeat=5, warmup=1, n_items=x.shape[0], unit="sample", dataset="simulated",
          extra={"n_iter": int(kf.n_iter_), "note": "same job as the tttrlib row: greedy k-means++ (2+ln k trials) then Lloyd"})

    d = np.load(os.path.join(SHARED, "hdbscan.npz"))
    xh, mcs, ms = d["x"], int(d["min_cluster_size"]), int(d["min_samples"])

    def run_h():
        return HDBSCAN(min_cluster_size=mcs, min_samples=ms).fit(xh)

    h = run_h()
    OUT["hdbscan_labels"] = h.labels_
    bench("hdbscan", "scikit-learn HDBSCAN", f"HDBSCAN n={xh.shape[0]} d={xh.shape[1]} (core distances, MST, condense, EOM, labels)",
          run_h, repeat=3, warmup=1, n_items=xh.shape[0], unit="sample", dataset="simulated")


def bench_filterpy():
    from filterpy.kalman import KalmanFilter
    d = np.load(os.path.join(SHARED, "kalman.npz"))
    y, x0, P0, Q, dt, r_scale = d["y"], d["x0"], d["P0"], d["Q"], float(d["dt"]), float(d["r_scale"])
    T, dim = y.shape

    def run():
        kf = KalmanFilter(dim_x=dim, dim_z=dim)
        kf.x = x0.copy(); kf.P = P0.copy(); kf.Q = Q.copy()
        kf.F = np.eye(dim); kf.H = np.eye(dim)
        xs = np.empty((T, dim)); Ps = np.empty((T, dim, dim)); D = np.empty(T)
        for t in range(T):
            kf.predict()
            # measurement noise from the predicted rate (the model tttrlib's kernel implements)
            kf.R = np.diag(np.maximum(kf.x_prior, 1e-12) / dt) * r_scale
            kf.update(y[t])
            xs[t] = kf.x; Ps[t] = kf.P
            D[t] = float(np.sqrt(kf.y @ np.linalg.solve(kf.S, kf.y)))
        return xs, Ps, D

    xs, Ps, D = run()
    OUT["kalman_x"], OUT["kalman_P"], OUT["kalman_D"] = xs, Ps, D
    bench("kalman", "filterpy KalmanFilter", f"Kalman filter, {T} steps x {dim} channels",
          run, repeat=2, warmup=1, n_items=T, unit="step", dataset="simulated")


def bench_hmmlearn():
    from hmmlearn import _hmmc
    d = np.load(os.path.join(SHARED, "hmm_lattice.npz"))
    log_start, log_trans, log_frame = d["log_start"], d["log_trans"], d["log_frame"]
    start, trans = np.exp(log_start), np.exp(log_trans)
    T, K = log_frame.shape

    def run():
        lp, fwd = _hmmc.forward_log(start, trans, log_frame)
        bwd = _hmmc.backward_log(start, trans, log_frame)
        gamma = fwd + bwd
        gamma -= gamma.max(axis=1, keepdims=True)
        gamma = np.exp(gamma)
        gamma /= gamma.sum(axis=1, keepdims=True)
        xi = np.exp(_hmmc.compute_log_xi_sum(fwd, trans, bwd, log_frame))
        vs, states = _hmmc.viterbi(start, trans, log_frame)
        return lp, gamma, xi, vs, states

    lp, gamma, xi, vs, states = run()
    OUT["hmm_logprob"], OUT["hmm_posteriors"], OUT["hmm_xi"], OUT["hmm_viterbi_score"], OUT["hmm_states"] = lp, gamma, xi, vs, states
    bench("hmm_lattice", "hmmlearn (_hmmc)", f"HMM lattice T={T} K={K}: forward + posteriors/xi + Viterbi",
          run, repeat=5, warmup=1, n_items=T, unit="step", dataset="simulated")


def bench_hmmlearn_vb():
    from hmmlearn.vhmm import VariationalCategoricalHMM
    d = np.load(os.path.join(SHARED, "hmm_vb.npz"))
    X, lengths = d["X"].reshape(-1, 1), d["lengths"]
    K, P = d["seed_B"].shape

    def make():
        m = VariationalCategoricalHMM(n_components=K, n_features=P, n_iter=5000, tol=1e-12,
                                      init_params="", params="ste", implementation="log")
        m.startprob_prior_, m.transmat_prior_, m.emissionprob_prior_ = np.ones(K), np.ones((K, K)), np.ones((K, P))
        return m

    def run():
        m = make()
        # same seed as tttrlib's: prior + pseudo-counts of the seed model
        m.startprob_posterior_ = 1 + 20 * d["seed_pi"]
        m.transmat_posterior_ = 1 + 100 * d["seed_A"]
        m.emissionprob_posterior_ = 1 + 100 * d["seed_B"]
        m.fit(X, lengths)
        return m

    m = run()
    OUT["hmm_vb_alpha_prior"], OUT["hmm_vb_alpha_trans"], OUT["hmm_vb_alpha_obs"] = m.startprob_posterior_, m.transmat_posterior_, m.emissionprob_posterior_
    OUT["hmm_vb_lower_bound"] = np.array(m.monitor_.history[-1])
    # hmmlearn's bound evaluated at tttrlib's converged posterior (one E-step, no update used)
    m1 = make(); m1.n_iter = 1
    m1.startprob_posterior_, m1.transmat_posterior_, m1.emissionprob_posterior_ = d["alpha_prior"].copy(), d["alpha_trans"].copy(), d["alpha_obs"].copy()
    m1.fit(X, lengths)
    OUT["hmm_vb_lower_bound_at_tttrlib_posterior"] = np.array(m1.monitor_.history[0])
    bench("hmm_vb", "hmmlearn VariationalCategoricalHMM",
          f"VB-HMM (Dirichlet mean-field) on {len(lengths)} dense chains, {int(lengths.sum())} ticks, K={K}, to convergence",
          run, repeat=3, warmup=1, n_items=int(lengths.sum()), unit="tick", dataset="simulated")


def bench_phasorpy():
    from phasorpy.phasor import phasor_from_signal
    d = np.load(os.path.join(SHARED, "phasor.npz"))
    counts = d["counts"].astype(np.float64)

    def run():
        return phasor_from_signal(counts, axis=-1, harmonic=1)

    mean, g, s = run()
    OUT["phasor_g"], OUT["phasor_s"] = g, s
    bench("phasor", "phasorpy", f"phasor of {counts.shape[0]} decays x {counts.shape[1]} bins (first harmonic)",
          run, repeat=5, warmup=1, n_items=counts.shape[0], unit="decay", dataset="simulated")


def main():
    for name, fn in (("skimage", bench_skimage), ("skimage_max_tree", bench_skimage_max_tree), ("sklearn", bench_sklearn), ("filterpy", bench_filterpy),
                     ("hmmlearn", bench_hmmlearn), ("hmmlearn_vb", bench_hmmlearn_vb), ("phasorpy", bench_phasorpy)):
        try:
            fn()
        except Exception as e:
            print(f"[sciref] {name} failed: {type(e).__name__}: {e}", file=sys.stderr)
    np.savez(os.path.join(SHARED, "reference_outputs.npz"), **OUT)


if __name__ == "__main__":
    main()
