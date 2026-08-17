#!/usr/bin/env python
"""tttrlib vs the scientific-Python references of its general kernels -- tttrlib side.

  watershed        watershed                     <- skimage.segmentation.watershed
  marching_squares marching_squares              <- skimage.measure.find_contours (raw segments)
  richardson_lucy  richardson_lucy_2d            <- skimage.restoration.richardson_lucy
  kmeans           kmeans                        <- sklearn.cluster.KMeans (lloyd, one init)
  hdbscan          core_distances + MST + condensed tree + labels <- sklearn.cluster.HDBSCAN
  kalman           kalman_filter                 <- filterpy.kalman.KalmanFilter
  hmm_lattice      hmm_forward_log / posteriors / viterbi <- hmmlearn._hmmc
  hmm_vb           fit_vb (dense stream, dt == 1)  <- hmmlearn.vhmm.VariationalCategoricalHMM
  phasor           DecayPhasor.compute_phasor_bincounts_batch <- phasorpy.phasor.phasor_from_signal

Run in the base env; writes the exact inputs to results/shared/sciref/ for
competitors/bench_sciref.py (``sciref`` venv). check_sciref.py compares outputs.
Every pair is also a permanent A/B test (test/python/misc/test_math_ab_*.py,
test/python/clsm/test_ab_phasor_reference.py, test/python/hmm/test_ab_hmm_reference.py);
this file measures speed.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tttrlib  # noqa: E402
from common import bench, RESULTS  # noqa: E402

SHARED = os.path.join(RESULTS, "shared", "sciref")
os.makedirs(SHARED, exist_ok=True)
SEED = 20260817


# --------------------------------------------------------------------------- inputs

def make_watershed(n=1024, n_markers=200, seed=SEED):
    rng = np.random.default_rng(seed)
    gy, gx = np.mgrid[0:n, 0:n]
    image = np.zeros((n, n))
    centres = rng.uniform(0, n, (n_markers, 2))
    for cy, cx in centres:                       # basins around the markers
        image -= np.exp(-((gx - cx) ** 2 + (gy - cy) ** 2) / (2 * 25.0 ** 2))
    image += rng.normal(0, 0.02, image.shape)
    markers = np.zeros((n, n), dtype=np.int32)
    for i, (cy, cx) in enumerate(centres):
        markers[int(cy) % n, int(cx) % n] = i + 1
    mask = np.ones((n, n), dtype=np.uint8)
    return image, markers, mask


def make_rl(n=512, psf=15, seed=SEED):
    rng = np.random.default_rng(seed)
    gy, gx = np.mgrid[0:n, 0:n]
    truth = np.zeros((n, n))
    for _ in range(40):
        cy, cx = rng.uniform(20, n - 20, 2)
        truth += rng.uniform(50, 300) * np.exp(-((gx - cx) ** 2 + (gy - cy) ** 2) / (2 * 3.0 ** 2))
    k = np.arange(psf) - psf // 2
    p = np.exp(-(k[:, None] ** 2 + k[None, :] ** 2) / (2 * 2.5 ** 2))
    p /= p.sum()
    from scipy.signal import fftconvolve
    blurred = rng.poisson(np.clip(fftconvolve(truth, p, mode="same"), 0, None) + 5).astype(float)
    return np.ascontiguousarray(blurred), np.ascontiguousarray(p)


def make_kmeans(n=200_000, d=8, k=10, seed=SEED):
    rng = np.random.default_rng(seed)
    centres = rng.uniform(-10, 10, (k, d))
    lab = rng.integers(0, k, n)
    x = centres[lab] + rng.normal(0, 1.5, (n, d))
    n_trials = 2 + int(np.log(k))
    uniforms = rng.random(k * n_trials)          # one k-means++ init
    return np.ascontiguousarray(x), uniforms


def make_hdbscan(n=20_000, d=4, seed=SEED):
    rng = np.random.default_rng(seed)
    centres = rng.uniform(-20, 20, (12, d))
    lab = rng.integers(0, 12, n)
    x = centres[lab] + rng.normal(0, 1.0, (n, d))
    x[: n // 20] = rng.uniform(-25, 25, (n // 20, d))   # noise
    return np.ascontiguousarray(x)


def make_kalman(T=50_000, dim=2, seed=SEED):
    rng = np.random.default_rng(seed)
    rates = rng.uniform(2e3, 1e5, dim)
    true = np.where(np.arange(T)[:, None] < T // 2, rates, rates * 1.7)
    dt = 1e-3
    y = rng.poisson(true * dt).astype(float) / dt
    return y, rates.copy(), np.eye(dim) * 1e6, np.eye(dim) * 100.0, dt, 1.0


def make_hmm(T=200_000, K=4, seed=SEED):
    rng = np.random.default_rng(seed)
    start = rng.dirichlet(np.ones(K))
    trans = rng.dirichlet(np.ones(K) * 5, K)
    log_frame = np.log(rng.dirichlet(np.ones(K), T))
    return np.log(start), np.log(trans), np.ascontiguousarray(log_frame)


def make_hmm_vb(n_bursts=200, K=3, P=3, seed=SEED):
    """Dense tick chains (a photon at every tick) -- there the photon-stream VB-HMM
    is exactly a categorical VB-HMM, which hmmlearn implements."""
    rng = np.random.default_rng(seed)
    A = np.array([[0.96, 0.03, 0.01], [0.02, 0.95, 0.03], [0.02, 0.04, 0.94]])
    B = np.array([[0.7, 0.2, 0.1], [0.2, 0.6, 0.2], [0.1, 0.2, 0.7]])
    pi = np.array([0.5, 0.3, 0.2])
    X, lengths = [], []
    for _ in range(n_bursts):
        L = int(rng.integers(150, 350))
        z = np.empty(L, int); z[0] = rng.choice(K, p=pi)
        for t in range(1, L):
            z[t] = rng.choice(K, p=A[z[t - 1]])
        X.append(np.array([rng.choice(P, p=B[k]) for k in z])); lengths.append(L)
    seed_pi = np.full(K, 1.0 / K)
    seed_A = np.full((K, K), 0.1) + np.eye(K) * 0.7
    seed_B = np.array([[0.5, 0.3, 0.2], [0.3, 0.4, 0.3], [0.2, 0.3, 0.5]])
    return np.concatenate(X), np.array(lengths), seed_pi, seed_A, seed_B


def make_phasor(n_decays=100_000, n_bins=256, seed=SEED):
    rng = np.random.default_rng(seed)
    t = np.arange(n_bins)
    tau = rng.uniform(20, 80, n_decays)
    lam = np.exp(-t[None, :] / tau[:, None]) * 400 / tau[:, None] + 0.5
    return rng.poisson(lam).astype(np.int32)


def kmeanspp_seed(X, n_clusters, uniforms):
    """Greedy k-means++ from caller-supplied uniforms -- the seeding tttrlib's
    kmeans performs (bit-for-bit with ChiSurf's `_kmeanspp_seed`); vectorised
    here so the reference can start from the identical centres."""
    n_samples = X.shape[0]
    n_trials = uniforms.shape[0] // n_clusters
    centres = np.empty((n_clusters, X.shape[1]))
    centres[0] = X[min(int(uniforms[0] * n_samples), n_samples - 1)]
    closest = ((X - centres[0]) ** 2).sum(1)
    for c in range(1, n_clusters):
        total = float(closest.sum())
        cum = np.cumsum(closest)
        best_potential, best_index = np.inf, -1
        for trial in range(n_trials):
            u = uniforms[c * n_trials + trial]
            if total <= 0.0:
                idx = min(int(u * n_samples), n_samples - 1)
            else:
                idx = int(np.searchsorted(cum, u * total, side="left"))
                idx = min(idx, n_samples - 1)
            cand = ((X - X[idx]) ** 2).sum(1)
            pot = float(np.minimum(cand, closest).sum())
            if pot < best_potential:
                best_potential, best_index = pot, idx
        centres[c] = X[best_index]
        closest = np.minimum(closest, ((X - centres[c]) ** 2).sum(1))
    return centres


# --------------------------------------------------------------------------- run

def eom_select(parent, child, value, size, n_points):
    """Excess-of-mass cluster selection, allow_single_cluster=False -- the policy
    the kernel deliberately leaves to the caller; verbatim the validated copy in
    test/python/misc/test_math_ab_clustering.py (identical partitions to sklearn
    from the same tree)."""
    parent = np.asarray(parent); child = np.asarray(child)
    value = np.asarray(value); size = np.asarray(size)
    nodes = np.unique(np.concatenate([[n_points], child[child >= n_points]]))
    birth = {int(n_points): 0.0}
    for c, v in zip(child, value):
        if c >= n_points:
            birth[int(c)] = float(v)
    stability = {int(c): 0.0 for c in nodes}
    for p, v, s_ in zip(parent, value, size):
        stability[int(p)] += (float(v) - birth[int(p)]) * float(s_)
    children = {int(c): [] for c in nodes}
    for p, c in zip(parent, child):
        if c >= n_points:
            children[int(p)].append(int(c))
    selected = {}
    for c in sorted(int(c) for c in nodes)[::-1]:
        if c == n_points:
            selected[c] = False
            continue
        if not children[c]:
            selected[c] = True
            continue
        sub = sum(stability[k] for k in children[c])
        if sub > stability[c]:
            stability[c] = sub
            selected[c] = False
        else:
            selected[c] = True
            stack = list(children[c])
            while stack:
                k = stack.pop()
                selected[k] = False
                stack.extend(children[k])
    out = np.zeros(int(parent.max()) + 1, dtype=np.uint8)
    for k, v in selected.items():
        if v:
            out[k] = 1
    return out


def hdbscan_labels(x, min_cluster_size, k):
    """core distances -> mutual-reachability MST -> (low, high, weight) in the
    total order the linkage wants -> condensed tree -> EOM -> labels."""
    n = x.shape[0]
    mst = np.asarray(tttrlib.mutual_reachability_mst(x, k, 1.0))
    lo = np.minimum(mst[:, 0], mst[:, 1])
    hi = np.maximum(mst[:, 0], mst[:, 1])
    order = np.lexsort((hi, lo, mst[:, 2]))
    src = np.ascontiguousarray(lo[order].astype(np.int64))
    tgt = np.ascontiguousarray(hi[order].astype(np.int64))
    w = np.ascontiguousarray(mst[order, 2].astype(np.float64))
    parent, child, value, size = tttrlib.hdbscan_condensed_tree(src, tgt, w, min_cluster_size)
    selected = eom_select(parent, child, value, size, n)
    roots = np.asarray(tttrlib.hdbscan_label_points(parent, child, selected, n))
    labels = np.full(n, -1, dtype=np.int64)
    for i, r in enumerate(np.unique(roots[roots != n])):
        labels[roots == r] = i
    return labels


def main():
    # watershed + marching squares
    image, markers, mask = make_watershed()
    np.savez(os.path.join(SHARED, "watershed.npz"), image=image, markers=markers, mask=mask, level=-0.5)
    n = image.size
    bench("watershed", "tttrlib", "watershed 1024x1024, 200 markers, connectivity 1",
          lambda: tttrlib.watershed(image, markers, mask, 1), repeat=5, warmup=1,
          n_items=n, unit="pixel", dataset="simulated")
    bench("marching_squares", "tttrlib", "marching squares 1024x1024, one level",
          lambda: tttrlib.marching_squares(image, -0.5, False), repeat=5, warmup=1,
          n_items=n, unit="pixel", dataset="simulated")

    # Richardson-Lucy
    blurred, psf = make_rl()
    n_iter = 30
    np.savez(os.path.join(SHARED, "richardson_lucy.npz"), blurred=blurred, psf=psf, n_iter=n_iter)
    bench("richardson_lucy", "tttrlib", f"Richardson-Lucy 512x512, 15x15 PSF, {n_iter} iterations",
          lambda: tttrlib.richardson_lucy_2d(blurred, psf, n_iter, False, 0.0, False), repeat=5, warmup=1,
          n_items=blurred.size, unit="pixel", dataset="simulated", extra={"iterations": n_iter})

    # k-means
    x, uniforms = make_kmeans()
    k = 10
    np.savez(os.path.join(SHARED, "kmeans.npz"), x=x, k=k, uniforms=uniforms, init_centres=kmeanspp_seed(x, k, uniforms))
    seed_only = bench("kmeans", "tttrlib (k-means++ seed only)", f"k-means n={x.shape[0]} d={x.shape[1]} k={k}, greedy k-means++ seeding alone",
                      lambda: tttrlib.kmeans(x, k, uniforms, 1, 0, 0.0), repeat=5, warmup=1,
                      n_items=x.shape[0], unit="sample", dataset="simulated")
    bench("kmeans", "tttrlib", f"k-means n={x.shape[0]} d={x.shape[1]} k={k}, one init to convergence",
          lambda: tttrlib.kmeans(x, k, uniforms, 1, 300, 0.0), repeat=5, warmup=1,
          n_items=x.shape[0], unit="sample", dataset="simulated",
          extra={"note": "k-means++ (2+ln k trials) + Lloyd; the seed-only row isolates the seeding"})

    # HDBSCAN
    xh = make_hdbscan()
    mcs, kk = 25, 25
    np.savez(os.path.join(SHARED, "hdbscan.npz"), x=xh, min_cluster_size=mcs, min_samples=kk)
    bench("hdbscan", "tttrlib", f"HDBSCAN n={xh.shape[0]} d={xh.shape[1]} (core distances, MST, condense, EOM, labels)",
          lambda: hdbscan_labels(xh, mcs, kk), repeat=3, warmup=1,
          n_items=xh.shape[0], unit="sample", dataset="simulated")

    # Kalman
    y, x0, P0, Q, dt, r_scale = make_kalman()
    np.savez(os.path.join(SHARED, "kalman.npz"), y=y, x0=x0, P0=P0, Q=Q, dt=dt, r_scale=r_scale)
    bench("kalman", "tttrlib", f"Kalman filter, {y.shape[0]} steps x {y.shape[1]} channels",
          lambda: tttrlib.kalman_filter(y, x0, P0, Q, dt, r_scale), repeat=5, warmup=1,
          n_items=y.shape[0], unit="step", dataset="simulated")

    # HMM lattice
    log_start, log_trans, log_frame = make_hmm()
    np.savez(os.path.join(SHARED, "hmm_lattice.npz"), log_start=log_start, log_trans=log_trans, log_frame=log_frame)
    T, K = log_frame.shape

    def run_hmm():
        fwd = np.empty_like(log_frame)
        lp = tttrlib.hmm_forward_log(log_start, log_trans, log_frame, fwd)
        post = np.empty_like(log_frame)
        xi = np.zeros((K, K))
        tttrlib.hmm_backward_posteriors_xi(log_trans, log_frame, fwd, lp, post, xi)
        states = np.empty(T, dtype=np.int64)
        vs = tttrlib.hmm_viterbi_log(log_start, log_trans, log_frame, states)
        return lp, post, xi, vs, states

    bench("hmm_lattice", "tttrlib", f"HMM lattice T={T} K={K}: forward + posteriors/xi + Viterbi",
          run_hmm, repeat=5, warmup=1, n_items=T, unit="step", dataset="simulated")

    # HMM VB (dense stream) -- posterior + elbo saved so the competitor can evaluate its bound at our posterior
    X, lengths, seed_pi, seed_A, seed_B = make_hmm_vb()
    off = np.concatenate([[0], np.cumsum(lengths)])
    streams = [X[off[i]:off[i + 1]].tolist() for i in range(len(lengths))]
    times = [list(range(int(L))) for L in lengths]
    Kv, Pv = seed_B.shape
    eng_vb = tttrlib.HMM(); eng_vb.set_bursts(times, streams, Pv)
    init_vb = tttrlib.HmmModel(list(seed_pi), list(seed_A.ravel()), list(seed_B.ravel()))

    def run_vb():
        return tttrlib.fit_vb(eng_vb, init_vb, None, 5000, 1e-12)

    vb = run_vb()
    np.savez(os.path.join(SHARED, "hmm_vb.npz"), X=X, lengths=lengths, seed_pi=seed_pi, seed_A=seed_A, seed_B=seed_B,
             alpha_prior=np.asarray(vb.alpha_prior), alpha_trans=np.asarray(vb.alpha_trans).reshape(Kv, Kv),
             alpha_obs=np.asarray(vb.alpha_obs).reshape(Kv, Pv), elbo=vb.elbo, n_iter=vb.n_iter)
    bench("hmm_vb", "tttrlib", f"VB-HMM (Dirichlet mean-field) on {len(lengths)} dense chains, {int(lengths.sum())} ticks, K={Kv}, to convergence",
          run_vb, repeat=3, warmup=1, n_items=int(lengths.sum()), unit="tick", dataset="simulated")

    # phasor
    counts = make_phasor()
    freq = 1.0 / counts.shape[1]
    np.savez(os.path.join(SHARED, "phasor.npz"), counts=counts, frequency=freq)
    bench("phasor", "tttrlib", f"phasor of {counts.shape[0]} decays x {counts.shape[1]} bins (first harmonic)",
          lambda: tttrlib.DecayPhasor.compute_phasor_bincounts_batch(counts, freq, 1, 1.0, 0.0), repeat=5, warmup=1,
          n_items=counts.shape[0], unit="decay", dataset="simulated")

    with open(os.path.join(SHARED, "meta.json"), "w") as fh:
        json.dump({"tttrlib": tttrlib.__version__}, fh)


if __name__ == "__main__":
    main()
