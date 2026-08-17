"""Numerics behind okf/design/hmmvb-elbo-decision.md.

For simulated photon-stream HMMs (tick-marginalised transitions, geometric
inter-photon gaps) and K = 1..4 it reports:
  E     engine ELBO (fit_vb; A~ rows re-normalised before the tick power)
  H     header ELBO (same q(theta), sub-stochastic A~^dt data term = Beal's bound)
  logZ  importance-sampling estimate of the exact log marginal likelihood
        (proposal = a widened copy of the VB posterior; the engine's exact
        forward pass gives p(y|theta)); ESS is printed. Single-mode: add ln K!
        for the label-permutation copies if you want the full evidence.
  BIC/ICL of the ML fit (HMM.optimize / viterbi_path)
Fits are started from a near-diagonal transition matrix (what a user does) with
several restarts, best objective kept.
Run: PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python okf/design/scripts/hmmvb_elbo_decision.py [n_seeds]
"""
import sys, time
import numpy as np
from scipy.special import digamma, gammaln, logsumexp
import tttrlib


def simulate(K_true, n_bursts, seed, n_det=2, mean_gap=8, mean_len=60):
    """Bursts of a K-state chain observed only at photon ticks."""
    rng = np.random.default_rng(seed)
    if K_true == 1:
        A = np.ones((1, 1)); B = np.array([[0.7, 0.3]]); pi = np.ones(1)
    elif K_true == 2:
        A = np.array([[0.995, 0.005], [0.008, 0.992]]); B = np.array([[0.85, 0.15], [0.25, 0.75]]); pi = np.array([0.6, 0.4])
    else:
        A = np.array([[0.994, 0.003, 0.003], [0.004, 0.992, 0.004], [0.003, 0.003, 0.994]])
        B = np.array([[0.9, 0.1], [0.5, 0.5], [0.15, 0.85]]); pi = np.ones(3) / 3
    times, streams = [], []
    for _ in range(n_bursts):
        n = max(5, rng.poisson(mean_len))
        gaps = 1 + rng.geometric(1.0 / mean_gap, n - 1)
        t = np.concatenate([[0], np.cumsum(gaps)]).astype(np.int64)
        z = rng.choice(K_true, p=pi); s = [rng.choice(n_det, p=B[z])]
        for g in gaps:
            Ag = np.linalg.matrix_power(A, int(g))
            z = rng.choice(K_true, p=Ag[z]); s.append(rng.choice(n_det, p=B[z]))
        times.append(t.tolist()); streams.append(list(map(int, s)))
    return times, streams, n_det


def forward_ll(prior, A, B, times, streams):
    """Exact log-likelihood of the tick-marginalised chain (matrix powers);
    A may be sub-stochastic."""
    tot = 0.0
    for t, s in zip(times, streams):
        a = prior * B[:, s[0]]; c = a.sum()
        if c <= 0: return -np.inf
        tot += np.log(c); a = a / c
        for k in range(1, len(t)):
            a = (a @ np.linalg.matrix_power(A, int(t[k] - t[k - 1]))) * B[:, s[k]]; c = a.sum()
            if c <= 0: return -np.inf
            tot += np.log(c); a = a / c
    return tot


def kl_dir(a, b):
    return (gammaln(a.sum()) - gammaln(a).sum() - gammaln(b.sum()) + gammaln(b).sum()
            + ((a - b) * (digamma(a) - digamma(a.sum()))).sum())


def log_dir(x, a):
    return gammaln(a.sum()) - gammaln(a).sum() + ((a - 1) * np.log(x)).sum()


def inits(K, n_det, rng, n):
    for _ in range(n):
        pi0 = rng.dirichlet(np.ones(K))
        A0 = np.full((K, K), 0.01 / max(K - 1, 1)) + np.eye(K) * (0.99 - 0.01 / max(K - 1, 1)) if K > 1 else np.ones((1, 1))
        B0 = rng.dirichlet(np.ones(n_det), K)
        yield tttrlib.HmmModel(list(pi0), list(A0.ravel()), list(B0.ravel()))


def vb_terms(eng, K, n_det, times, streams, seed, restarts=4):
    rng = np.random.default_rng(seed)
    best = None
    for init in inits(K, n_det, rng, restarts):
        vb = tttrlib.fit_vb(eng, init, None, 3000, 1e-9)
        if best is None or vb.elbo > best.elbo: best = vb
    vb = best
    ap = np.asarray(vb.alpha_prior); at = np.asarray(vb.alpha_trans).reshape(K, K); ao = np.asarray(vb.alpha_obs).reshape(K, n_det)
    tp = np.exp(digamma(ap) - digamma(ap.sum())); ta = np.exp(digamma(at) - digamma(at.sum(1, keepdims=True))); to = np.exp(digamma(ao) - digamma(ao.sum(1, keepdims=True)))
    total_kl = kl_dir(ap, np.ones(K)) + sum(kl_dir(at[i], np.ones(K)) for i in range(K)) + sum(kl_dir(ao[i], np.ones(n_det)) for i in range(K))
    H_data = forward_ll(tp, ta, to, times, streams)                    # sub-stochastic A~^dt (Beal)
    # predicted asymptotic gap: sum_i N_i (K-1)/(2 alpha_i) with N_i = ticks from state i ~ alpha_i - K
    pred_gap = sum((at[i].sum() - K) * (K - 1) / (2.0 * at[i].sum()) for i in range(K)) if K > 1 else 0.0
    return dict(E=vb.elbo, H=H_data - total_kl, kl=total_kl, pred_gap=pred_gap, vb=vb, ap=ap, at=at, ao=ao, n_iter=vb.n_iter)


def ml_fit(eng, K, n_det, seed, restarts=4):
    rng = np.random.default_rng(seed + 100)
    best = None
    for init in inits(K, n_det, rng, restarts):
        m = eng.optimize(init, 3000, 1e-9, 1e-12, False)
        if best is None or m.loglik > best.loglik: best = m
    path, icl = eng.viterbi_path(best)
    return best.loglik, best.bic(), icl


def log_evidence_is(eng, K, n_det, ap, at, ao, n_samples, seed, widen=0.7):
    """log p(y) = log E_{theta~q'}[p(y|theta) p(theta) / q'(theta)], q' = the VB
    posterior with (alpha-1) scaled by `widen` (heavier tails so the weights
    stay bounded); p(theta) = Dir(1) everywhere. Returns (logZ, ESS)."""
    rng = np.random.default_rng(seed + 7)
    wa = lambda a: 1.0 + (a - 1.0) * widen
    qp, qt, qo = wa(ap), wa(at), wa(ao)
    lw = np.empty(n_samples)
    for i in range(n_samples):
        pi = rng.dirichlet(qp); A = np.vstack([rng.dirichlet(qt[r]) for r in range(K)]); B = np.vstack([rng.dirichlet(qo[r]) for r in range(K)])
        ll = eng.evaluate(tttrlib.HmmModel(list(pi), list(A.ravel()), list(B.ravel()))).loglik
        lq = log_dir(pi, qp) + sum(log_dir(A[r], qt[r]) for r in range(K)) + sum(log_dir(B[r], qo[r]) for r in range(K))
        lp = log_dir(pi, np.ones(K)) + sum(log_dir(A[r], np.ones(K)) for r in range(K)) + sum(log_dir(B[r], np.ones(n_det)) for r in range(K))
        lw[i] = ll + lp - lq
    lz = logsumexp(lw) - np.log(n_samples)
    ess = np.exp(2 * logsumexp(lw) - logsumexp(2 * lw))
    return lz, ess


def main():
    t0 = time.time()
    n_seeds = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    n_is = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    Ks = [1, 2, 3, 4]
    print(f"{'case':5s}{'seed':>5s}{'K':>3s}{'E(engine)':>12s}{'H(header)':>12s}{'E-H':>7s}{'pred':>7s}{'logZ(IS)':>11s}{'ESS':>7s}{'lnK!':>6s}{'ML-loglik':>11s}{'BIC':>10s}{'ICL':>10s}{'it':>5s}")
    summary = {}
    for K_true, n_bursts in ((2, 40), (3, 80)):
        picks = {"E": [], "H": [], "logZ": [], "logZ+lnK!": [], "BIC": [], "ICL": []}
        stats = dict(H_over=0, E_over=0, total=0, gaps=[], gap_err=[], E_minus_lz=[], H_minus_lz=[])
        for seed in range(n_seeds):
            times, streams, n_det = simulate(K_true, n_bursts, seed)
            eng = tttrlib.HMM(); eng.set_bursts(times, streams, n_det)
            rows = {}
            for K in Ks:
                r = vb_terms(eng, K, n_det, times, streams, seed)
                ll, bic, icl = ml_fit(eng, K, n_det, seed)
                lz, ess = log_evidence_is(eng, K, n_det, r["ap"], r["at"], r["ao"], n_is, seed)
                lnkf = float(gammaln(K + 1))
                rows[K] = dict(E=r["E"], H=r["H"], lz=lz, lzk=lz + lnkf, bic=bic, icl=icl)
                stats["total"] += 1
                stats["H_over"] += r["H"] > lz + 0.05; stats["E_over"] += r["E"] > lz + 0.05
                stats["gaps"].append(r["E"] - r["H"]); stats["gap_err"].append(r["E"] - r["H"] - r["pred_gap"])
                stats["E_minus_lz"].append(r["E"] - lz); stats["H_minus_lz"].append(r["H"] - lz)
                print(f"K{K_true}   {seed:4d}{K:3d}{r['E']:12.2f}{r['H']:12.2f}{r['E']-r['H']:7.3f}{r['pred_gap']:7.3f}{lz:11.2f}{ess:7.0f}{lnkf:6.2f}{ll:11.2f}{bic:10.1f}{icl:10.1f}{r['n_iter']:5d}")
            picks["E"].append(max(Ks, key=lambda k: rows[k]["E"]))
            picks["H"].append(max(Ks, key=lambda k: rows[k]["H"]))
            picks["logZ"].append(max(Ks, key=lambda k: rows[k]["lz"]))
            picks["logZ+lnK!"].append(max(Ks, key=lambda k: rows[k]["lzk"]))
            picks["BIC"].append(min(Ks, key=lambda k: rows[k]["bic"]))
            picks["ICL"].append(min(Ks, key=lambda k: rows[k]["icl"]))
            # K -> K+1 differences of the selection numbers, this seed
            dE = [rows[k + 1]["E"] - rows[k]["E"] for k in Ks[:-1]]
            dH = [rows[k + 1]["H"] - rows[k]["H"] for k in Ks[:-1]]
            dZ = [rows[k + 1]["lz"] - rows[k]["lz"] for k in Ks[:-1]]
            print(f"      seed {seed}: dE(K->K+1) {np.round(dE, 2).tolist()}  dH {np.round(dH, 2).tolist()}  dlogZ {np.round(dZ, 2).tolist()}")
        summary[K_true] = (picks, stats)
    print()
    for K_true, (picks, s) in summary.items():
        print(f"true K={K_true} ({n_seeds} seeds): correct picks -- "
              + ", ".join(f"{k}: {int(sum(np.array(v) == K_true))}/{n_seeds}" for k, v in picks.items()))
        print(f"   picks per seed: " + "; ".join(f"{k}={v}" for k, v in picks.items()))
        print(f"   H > logZ+0.05 in {s['H_over']}/{s['total']} fits; E > logZ+0.05 in {s['E_over']}/{s['total']}")
        print(f"   E-H gap: mean {np.mean(s['gaps']):.3f} max {np.max(s['gaps']):.3f} nat; |gap - K(K-1)/2 prediction| max {np.max(np.abs(s['gap_err'])):.3f}")
        print(f"   E-logZ: mean {np.mean(s['E_minus_lz']):.2f} [{np.min(s['E_minus_lz']):.2f},{np.max(s['E_minus_lz']):.2f}]   "
              f"H-logZ: mean {np.mean(s['H_minus_lz']):.2f} [{np.min(s['H_minus_lz']):.2f},{np.max(s['H_minus_lz']):.2f}]")
    print(f"\n{time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
