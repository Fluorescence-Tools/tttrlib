"""A/B of the photon-stream HMM engine against H2MM_C, and of the VB machinery
against scipy transcriptions.

Reference: **H2MM_C** (Harris et al., the reference implementation of the
photon-by-photon hidden Markov model of Pirchi et al. 2016), recorded from the
competitor venv by ``gen_ab_hmm_h2mm_c_reference.py`` into
``test/data/reference/hmm_h2mm_c_reference.npz``. Bursts are simulated there
with a plain NumPy Markov chain and stored beside the outputs, so nothing here
depends on any RNG being stable. What is compared, for the *same* model on the
*same* bursts:

* the total and per-burst log-likelihood (H2MM_C ``H2MM_arr``);
* the per-photon posteriors gamma;
* the Viterbi path (H2MM_C ``viterbi_path``);
* the model after exactly one Baum-Welch step (H2MM_C ``EM_H2MM_C(max_iter=1)``
  against ``HMM.optimize(init, 1, ..., accelerate=False)``).

Two things are deliberately *not* compared bit for bit, and the reasons are
pinned as tests of their own:

* H2MM_C's per-burst "path log-likelihood" from ``viterbi_path`` is not the
  complete-data log-likelihood of the decoded path (the tttrlib ICL term is —
  checked here against an exact NumPy evaluation of log π + Σ log A^Δt + Σ log B
  along the identical path), so the two ICL numbers differ while the paths agree.
* H2MM_C's returned ``loglik`` after one step is the log-likelihood of the
  *updated* model; tttrlib reports the E-step value of the *input* model.
  Both are checked for what they claim to be.

The digamma is compared to ``scipy.special.digamma``; the VB posterior is
checked for self-consistency against a scipy transcription of the mean-field
fixed point (posterior concentrations = prior + expected counts under the
geometric-mean parameters, KL(Dir‖Dir) closed form). The ELBO's data term is
compared with a NumPy forward pass under the geometric-mean parameters — see
``TestVariationalBayes.test_elbo_data_term_against_the_header_derivation`` for
what that showed.
"""
import os
import unittest

import numpy as np

import tttrlib

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "..", "..", "data", "reference", "hmm_h2mm_c_reference.npz")


def _load():
    if not os.path.exists(FIXTURE):
        raise unittest.SkipTest("hmm_h2mm_c_reference.npz not present (see gen_ab_hmm_h2mm_c_reference.py)")
    return np.load(FIXTURE)


def _case(d, name):
    g = lambda k: d[f"{name}/{k}"]
    off = g("offsets")
    T, S = g("times"), g("streams")
    times = [T[off[i]:off[i + 1]].tolist() for i in range(len(off) - 1)]
    streams = [S[off[i]:off[i + 1]].tolist() for i in range(len(off) - 1)]
    prior, A, B = g("prior"), g("trans"), g("obs")
    eng = tttrlib.HMM()
    eng.set_bursts(times, streams, int(B.shape[1]))
    model = tttrlib.HmmModel(list(prior), list(A.ravel()), list(B.ravel()))
    return g, eng, model, times, streams, prior, A, B


def _path_loglik(prior, A, B, times, streams, path):
    """Exact complete-data log-likelihood along ``path`` (one burst)."""
    ll = np.log(prior[path[0]]) + np.log(B[path[0], streams[0]])
    for k in range(1, len(times)):
        Adt = np.linalg.matrix_power(A, int(times[k] - times[k - 1]))
        ll += np.log(Adt[path[k - 1], path[k]]) + np.log(B[path[k], streams[k]])
    return ll


class TestAgainstH2mmC(unittest.TestCase):
    """Same model, same bursts: tttrlib.HMM against H2MM_C's recorded numbers."""

    def test_loglik_total_and_per_burst(self):
        d = _load()
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, eng, model, *_ = _case(d, name)
                ev = eng.evaluate(model)
                self.assertAlmostEqual(ev.loglik, float(g("loglik")), delta=1e-9 * abs(float(g("loglik"))))
                # per burst through the toy-free route: one engine per burst
                per = []
                off = g("offsets")
                for b in range(len(off) - 1):
                    e1 = tttrlib.HMM()
                    e1.set_bursts([g("times")[off[b]:off[b + 1]].tolist()],
                                  [g("streams")[off[b]:off[b + 1]].tolist()], int(g("obs").shape[1]))
                    per.append(e1.evaluate(model).loglik)
                np.testing.assert_allclose(per, g("loglik_burst"), rtol=1e-9, atol=1e-9)

    def test_gamma(self):
        d = _load()
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, eng, model, *_ = _case(d, name)
                gamma, n_underflow = eng.gamma(model)
                self.assertEqual(n_underflow, 0)
                # the engine returns float32 posteriors
                np.testing.assert_allclose(np.asarray(gamma, float), g("gamma"), atol=2e-7, rtol=0)

    def test_viterbi_path_identical(self):
        d = _load()
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, eng, model, *_ = _case(d, name)
                path, _ = eng.viterbi_path(model)
                np.testing.assert_array_equal(np.asarray(path), g("viterbi"))

    def test_icl_is_the_exact_path_loglik_where_h2mm_c_reports_something_else(self):
        """tttrlib's ICL is -2 * (complete-data log-likelihood along the Viterbi
        path) + n_free * ln N. Check that against an exact NumPy evaluation on
        the identical path, and record that H2MM_C's per-burst path "loglik"
        is not that quantity (so its ICL is not comparable)."""
        d = _load()
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, eng, model, times, streams, prior, A, B = _case(d, name)
                path, icl = eng.viterbi_path(model)
                off = g("offsets")
                ll = sum(_path_loglik(prior, A, B, np.asarray(times[b]), np.asarray(streams[b]),
                                      np.asarray(path)[off[b]:off[b + 1]])
                         for b in range(len(off) - 1))
                N = int(off[-1])
                self.assertAlmostEqual(icl, -2.0 * ll + model.n_free() * np.log(N), delta=1e-8 * abs(icl))
                # H2MM_C's number: same paths, different quantity
                h2_ll = float(g("viterbi_ll_burst").sum())
                self.assertNotAlmostEqual(h2_ll, ll, delta=1e-6)

    def test_one_baum_welch_step_matches(self):
        d = _load()
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, eng, model, *_ = _case(d, name)
                n, p = g("trans").shape[0], g("obs").shape[1]
                step = eng.optimize(model, 1, 1e30, 1e-12, False)
                np.testing.assert_allclose(np.asarray(step.prior), g("step_prior"), atol=1e-13, rtol=0)
                np.testing.assert_allclose(np.asarray(step.trans).reshape(n, n), g("step_trans"), atol=1e-13, rtol=0)
                np.testing.assert_allclose(np.asarray(step.obs).reshape(n, p), g("step_obs"), atol=1e-13, rtol=0)
                # tttrlib reports the E-step value of the input model; H2MM_C the
                # log-likelihood of the updated one -- both are what they claim.
                self.assertAlmostEqual(step.loglik, float(g("loglik")), delta=1e-8)
                stepped = tttrlib.HmmModel(list(step.prior), list(step.trans), list(step.obs))
                self.assertAlmostEqual(eng.evaluate(stepped).loglik, float(g("step_loglik")), delta=1e-8)


class TestVariationalBayes(unittest.TestCase):

    def test_dirichlet_kl_matches_the_closed_form(self):
        """KL(Dir(a) || Dir(b)) closed form with scipy.special (gammaln,
        digamma); the array binding landed 2026-08-17 (the raw-pointer form
        was uncallable from Python)."""
        from scipy.special import gammaln, digamma
        rng = np.random.default_rng(3)
        for n in (2, 3, 6):
            a = rng.uniform(0.5, 5.0, n)
            b = rng.uniform(0.5, 5.0, n)
            ref = (gammaln(a.sum()) - gammaln(b.sum()) - (gammaln(a) - gammaln(b)).sum()
                   + ((a - b) * (digamma(a) - digamma(a.sum()))).sum())
            self.assertAlmostEqual(tttrlib.dirichlet_kl(a, b), float(ref), places=10)
            self.assertAlmostEqual(tttrlib.dirichlet_kl(a, a), 0.0, places=12)
        with self.assertRaises(RuntimeError):
            tttrlib.dirichlet_kl(np.ones(2), np.ones(3))

    def test_digamma_matches_scipy(self):
        from scipy.special import digamma
        xs = np.concatenate([np.logspace(-3, 4, 60), [0.5, 1.0, 2.0, 5.5, 6.0, 7.0]])
        for x in xs:
            self.assertAlmostEqual(tttrlib.digamma(float(x)), float(digamma(x)), delta=1e-11 * max(1.0, abs(digamma(x))))

    def _vb(self):
        from scipy.special import digamma
        d = _load()
        g, eng, model, times, streams, prior, A, B = _case(d, "two_state_2det")
        vb = tttrlib.fit_vb(eng, model, None, 400, 1e-10)
        n, p = A.shape[0], B.shape[1]
        ap = np.asarray(vb.alpha_prior)
        at = np.asarray(vb.alpha_trans).reshape(n, n)
        ao = np.asarray(vb.alpha_obs).reshape(n, p)
        tp = np.exp(digamma(ap) - digamma(ap.sum()))
        ta = np.exp(digamma(at) - digamma(at.sum(1, keepdims=True)))
        to = np.exp(digamma(ao) - digamma(ao.sum(1, keepdims=True)))
        return vb, eng, times, streams, (n, p), (ap, at, ao), (tp, ta, to)

    def test_fixed_point_and_kl_against_scipy(self):
        """At convergence alpha = 1 (flat prior) + expected counts under the
        geometric-mean parameters, and the ELBO's KL terms are the closed-form
        Dirichlet KL (scipy gammaln/digamma)."""
        from scipy.special import digamma, gammaln
        vb, eng, times, streams, (n, p), (ap, at, ao), (tp, ta, to) = self._vb()
        self.assertTrue(vb.converged)
        ev = eng.evaluate(tttrlib.HmmModel(list(tp), list(ta.ravel()), list(to.ravel())))
        np.testing.assert_allclose(at - 1.0, np.asarray(ev.xi).reshape(n, n), atol=2e-4, rtol=1e-4)
        np.testing.assert_allclose(ao - 1.0, np.asarray(ev.gamma_obs).reshape(n, p), atol=2e-5, rtol=1e-6)
        np.testing.assert_allclose(ap - 1.0, np.asarray(ev.prior_counts), atol=1e-6, rtol=1e-6)

        def kl(a, b):
            return (gammaln(a.sum()) - gammaln(a).sum() - gammaln(b.sum()) + gammaln(b).sum()
                    + ((a - b) * (digamma(a) - digamma(a.sum()))).sum())
        total_kl = kl(ap, np.ones(n)) + sum(kl(at[i], np.ones(n)) for i in range(n)) \
            + sum(kl(ao[i], np.ones(p)) for i in range(n))
        # elbo_normalised = loglik(engine data term) - KL; elbo = loglik_beal - KL:
        # the KL side of both identities is exact
        self.assertAlmostEqual(vb.elbo_normalised, vb.loglik - total_kl, delta=1e-8)
        self.assertAlmostEqual(vb.elbo, vb.loglik_beal - total_kl, delta=1e-8)

    def test_elbo_data_term_against_the_header_derivation(self):
        """HMMVB.h derives the E-step as forward-backward with the geometric-mean
        parameters substituted, so that marginalising the unobserved ticks gives
        exactly A~^dt (A~ is *sub*-stochastic). The engine's A^dt cache
        row-normalises after every composition, so the iteration's data term
        (``loglik``) is the forward pass with A~'s rows normalised first, and
        the reported bound (``loglik_beal`` / ``elbo``) is one extra
        sub-stochastic pass at the returned posterior. Both are pinned to their
        NumPy transcriptions here; the difference is K(K-1)/2 = 1 nat on this
        two-state case (okf/design/hmmvb-elbo-decision.md; upstream reference:
        TestVariationalBayesAgainstHmmlearn)."""
        vb, eng, times, streams, (n, p), _, (tp, ta, to) = self._vb()

        def fwd(prior, A, B):
            tot = 0.0
            for t, s in zip(times, streams):
                a = prior * B[:, s[0]]
                c = a.sum(); tot += np.log(c); a = a / c
                for k in range(1, len(t)):
                    a = (a @ np.linalg.matrix_power(A, int(t[k] - t[k - 1]))) * B[:, s[k]]
                    c = a.sum(); tot += np.log(c); a = a / c
            return tot
        normalised_rows = fwd(tp, ta / ta.sum(1, keepdims=True), to)
        sub_stochastic = fwd(tp, ta, to)
        self.assertAlmostEqual(vb.loglik, normalised_rows, delta=1e-5)
        self.assertAlmostEqual(vb.loglik_beal, sub_stochastic, delta=1e-8)
        self.assertAlmostEqual(vb.loglik - vb.loglik_beal, n * (n - 1) / 2, delta=0.05)


class TestVariationalBayesAgainstHmmlearn(unittest.TestCase):
    """Independent reference for the VB machinery: ``hmmlearn.vhmm.
    VariationalCategoricalHMM`` (fixture recorded by
    ``gen_ab_hmm_vb_hmmlearn_reference.py`` under the sciref venv). On a stream
    with a photon at every tick (dt == 1) the photon-stream VB-HMM *is* a
    categorical VB-HMM with Dirichlet factors, so posterior parameters and the
    lower bound are directly comparable.

    What this pins: the engine's fixed point is hmmlearn's (posterior Dirichlet
    parameters to ~1e-3 relative -- the engine iterates on the row-normalised
    geometric-mean A, hmmlearn on the sub-stochastic one), the reported ``elbo``
    is hmmlearn's bound (the sub-stochastic forward pass at the engine's
    posterior minus the KL terms), and the iteration's ``elbo_normalised`` sits
    K(K-1)/2 nat above it (okf/design/hmmvb-elbo-decision.md)."""

    FIX = os.path.join(HERE, "..", "..", "data", "reference", "hmm_vb_hmmlearn_reference.npz")

    def _case(self, d, name):
        from scipy.special import digamma, gammaln
        g = lambda k: d[f"{name}/{k}"]
        X, lengths = g("X"), g("lengths")
        off = np.concatenate([[0], np.cumsum(lengths)])
        streams = [X[off[i]:off[i + 1]].tolist() for i in range(len(lengths))]
        times = [list(range(int(L))) for L in lengths]                # dense: dt == 1
        K, P = g("B").shape
        eng = tttrlib.HMM()
        eng.set_bursts(times, streams, P)
        init = tttrlib.HmmModel(list(g("seed_pi")), list(g("seed_A").ravel()), list(g("seed_B").ravel()))
        vb = tttrlib.fit_vb(eng, init, None, 5000, 1e-12)
        self.assertTrue(vb.converged)
        ap = np.asarray(vb.alpha_prior); at = np.asarray(vb.alpha_trans).reshape(K, K); ao = np.asarray(vb.alpha_obs).reshape(K, P)
        # sub-stochastic forward pass at the engine's posterior (Beal's bound at q)
        tp = np.exp(digamma(ap) - digamma(ap.sum()))
        ta = np.exp(digamma(at) - digamma(at.sum(1, keepdims=True)))
        to = np.exp(digamma(ao) - digamma(ao.sum(1, keepdims=True)))
        tot = 0.0
        for s in streams:
            a = tp * to[:, s[0]]; c = a.sum(); tot += np.log(c); a = a / c
            for k in range(1, len(s)):
                a = (a @ ta) * to[:, s[k]]; c = a.sum(); tot += np.log(c); a = a / c

        def kl(a, b):
            return (gammaln(a.sum()) - gammaln(a).sum() - gammaln(b.sum()) + gammaln(b).sum()
                    + ((a - b) * (digamma(a) - digamma(a.sum()))).sum())
        H = tot - kl(ap, np.ones(K)) - sum(kl(at[i], np.ones(K)) for i in range(K)) - sum(kl(ao[i], np.ones(P)) for i in range(K))
        return g, vb, (ap, at, ao), H, K

    def test_posterior_matches_hmmlearn(self):
        if not os.path.exists(self.FIX):
            raise unittest.SkipTest("hmm_vb_hmmlearn_reference.npz not present")
        d = np.load(self.FIX)
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, vb, (ap, at, ao), H, K = self._case(d, name)
                np.testing.assert_allclose(ap, g("alpha_prior"), rtol=2e-3, atol=5e-3)
                np.testing.assert_allclose(at, g("alpha_trans"), rtol=2e-3, atol=2e-2)
                np.testing.assert_allclose(ao, g("alpha_obs"), rtol=2e-3, atol=2e-2)

    def test_elbo_is_hmmlearns_bound_and_elbo_normalised_is_K_choose_2_above(self):
        if not os.path.exists(self.FIX):
            raise unittest.SkipTest("hmm_vb_hmmlearn_reference.npz not present")
        d = np.load(self.FIX)
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                g, vb, _, H, K = self._case(d, name)
                lb = float(g("lower_bound"))
                # the reported elbo is the sub-stochastic bound at the returned posterior
                self.assertAlmostEqual(vb.elbo, H, delta=1e-8)
                # ... which is hmmlearn's bound (theirs at their own optimum: >= ours, ~1e-5 nat)
                self.assertAlmostEqual(vb.elbo, lb, delta=1e-3)
                self.assertLessEqual(vb.elbo, lb + 1e-6)
                # the iteration variable is K(K-1)/2 nat above the bound
                self.assertAlmostEqual(vb.elbo_normalised - vb.elbo, K * (K - 1) / 2, delta=0.05)


class TestPosteriorDiagnosticsAgainstArviz(unittest.TestCase):
    """``HmmPosterior.rhat`` / ``ess`` vs ArviZ ``rhat(method="split")`` /
    ``ess(method="mean")`` on recorded synthetic chains
    (``gen_ab_hmm_diagnostics_arviz_reference.py``, sciref venv). Same
    estimators to rounding, including the case where two chains disagree in
    mean -- there the previous ESS (within-chain autocorrelation only) reported
    ~N against ArviZ's ~40; the split-chain Vehtari 2021 form is used since
    2026-08-17."""

    FIX = os.path.join(HERE, "..", "..", "data", "reference", "hmm_diagnostics_arviz_reference.npz")

    @staticmethod
    def _posterior(x):
        post = tttrlib.HmmPosterior()
        post.n_states, post.n_symbols = 2, 2
        post.n_chains, post.n_par = int(x.shape[0]), int(x.shape[2])
        post.draws = tttrlib.VectorDouble(np.ascontiguousarray(x).ravel().tolist())
        return post

    def test_rhat_and_ess_match_arviz(self):
        if not os.path.exists(self.FIX):
            raise unittest.SkipTest("hmm_diagnostics_arviz_reference.npz not present")
        d = np.load(self.FIX)
        for name in d["cases"]:
            with self.subTest(case=str(name)):
                x = d[f"{name}/draws"]
                post = self._posterior(x)
                self.assertEqual(post.n_draws(), x.shape[1])
                ess = np.asarray(post.ess())
                np.testing.assert_allclose(ess, d[f"{name}/ess_mean"], rtol=1e-9, atol=1e-6)
                if x.shape[0] > 1:
                    np.testing.assert_allclose(np.asarray(post.rhat()), d[f"{name}/rhat_split"], rtol=1e-10, atol=1e-12)

    def test_ess_sees_disagreeing_chains(self):
        if not os.path.exists(self.FIX):
            raise unittest.SkipTest("hmm_diagnostics_arviz_reference.npz not present")
        d = np.load(self.FIX)
        agree = np.asarray(self._posterior(d["agreeing/draws"]).ess())
        offset = np.asarray(self._posterior(d["offset/draws"]).ess())
        # phi = 0 parameter: ~N when chains agree, tens when they sit half an sd apart
        self.assertGreater(agree[0], 1500)
        self.assertLess(offset[0], 100)


class TestGibbsAgainstHmmlearnPosterior(unittest.TestCase):
    """The blocked-Gibbs sampler (``HMM.sample``) against hmmlearn's variational
    posterior on the dense-stream fixture: same data, same Dir(1) priors. Two
    approximations of one posterior -- MCMC and mean-field VB -- so the check
    is statistical: every posterior mean within 3 combined standard deviations,
    R-hat under 1.05, and the Gibbs sd never below 0.9x nor above 2x the VB sd
    -- measured 1.2-1.5x here, the known direction: mean-field VB ignores the
    correlation between the parameters and the hidden path and under-disperses."""

    FIX = TestVariationalBayesAgainstHmmlearn.FIX

    def test_posterior_mean_and_sd(self):
        if not os.path.exists(self.FIX):
            raise unittest.SkipTest("hmm_vb_hmmlearn_reference.npz not present")
        d = np.load(self.FIX)
        g = lambda k: d[f"two_state_2det/{k}"]
        X, lengths = g("X"), g("lengths")
        off = np.concatenate([[0], np.cumsum(lengths)])
        streams = [X[off[i]:off[i + 1]].tolist() for i in range(len(lengths))]
        times = [list(range(int(L))) for L in lengths]
        K, P = g("B").shape
        eng = tttrlib.HMM()
        eng.set_bursts(times, streams, P)
        init = tttrlib.HmmModel(list(g("seed_pi")), list(g("seed_A").ravel()), list(g("seed_B").ravel()))
        post = eng.sample(init, 1500, 300, 2, 12345)
        self.assertLess(float(np.max(post.rhat())), 1.05)
        mean = np.asarray(post.mean()); sd = np.asarray(post.sd())
        # hmmlearn's Dirichlet posterior: mean a_i / a0, sd sqrt(a_i (a0 - a_i) / (a0^2 (a0 + 1)))
        blocks = []
        for a in (g("alpha_prior")[None, :], g("alpha_trans"), g("alpha_obs")):
            a0 = a.sum(1, keepdims=True)
            blocks.append((a / a0, np.sqrt(a * (a0 - a) / (a0 ** 2 * (a0 + 1)))))
        ref_mean = np.concatenate([b[0].ravel() for b in blocks])
        ref_sd = np.concatenate([b[1].ravel() for b in blocks])
        # relabel the reference to tttrlib's canonical order (sorted by emission of symbol 0)
        order = np.argsort(-g("alpha_obs")[:, 0] / g("alpha_obs").sum(1))
        canon = np.argsort(-mean[K + K * K::P][:K])
        if not np.array_equal(order, canon):
            perm = np.empty(K, int); perm[canon] = order
            def relabel(v):
                pr, tr, ob = v[:K], v[K:K + K * K].reshape(K, K), v[K + K * K:].reshape(K, P)
                return np.concatenate([pr[perm], tr[perm][:, perm].ravel(), ob[perm].ravel()])
            ref_mean, ref_sd = relabel(ref_mean), relabel(ref_sd)
        z = np.abs(mean - ref_mean) / np.sqrt(sd ** 2 + ref_sd ** 2)
        self.assertLess(float(z.max()), 3.0, (mean, ref_mean, z))
        ratio = sd / ref_sd
        self.assertTrue(np.all((ratio > 0.9) & (ratio < 2.0)), ratio)
        self.assertGreater(float(np.median(ratio)), 1.0, ratio)      # MCMC wider than mean-field VB


class TestGibbsVariatesAgainstScipy(unittest.TestCase):
    """The sampler's Marsaglia-Tsang gamma and stick-free Dirichlet draws
    (``gamma_variates`` / ``dirichlet_variates``, batch bindings 2026-08-17 --
    the scalar forms take a counter by reference and were uncallable from
    Python) vs scipy.stats: Kolmogorov-Smirnov on 20 000 draws, shape below and
    above 1 (the two branches of Marsaglia-Tsang), Dirichlet marginals = Beta."""

    def test_gamma_ks(self):
        from scipy import stats
        for shape in (0.4, 1.0, 2.5, 30.0):
            with self.subTest(shape=shape):
                g = np.asarray(tttrlib.gamma_variates(shape, 7, 0, 20000))
                self.assertGreater(stats.kstest(g, stats.gamma(shape).cdf).pvalue, 1e-3)

    def test_dirichlet_marginals_are_beta(self):
        from scipy import stats
        alpha = np.array([1.0, 3.0, 0.5])
        d = np.asarray(tttrlib.dirichlet_variates(alpha, 7, 0, 20000))
        np.testing.assert_allclose(d.sum(1), 1.0, atol=1e-12)
        for i in range(3):
            with self.subTest(component=i):
                p = stats.kstest(d[:, i], stats.beta(alpha[i], alpha.sum() - alpha[i]).cdf).pvalue
                self.assertGreater(p, 1e-3)


if __name__ == "__main__":
    unittest.main()
