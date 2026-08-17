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
        # elbo = loglik(data term) - KL: the KL side of the identity is exact
        self.assertAlmostEqual(vb.elbo, vb.loglik - total_kl, delta=1e-8)

    def test_elbo_data_term_against_the_header_derivation(self):
        """HMMVB.h derives the E-step as forward-backward with the geometric-mean
        parameters substituted, so that marginalising the unobserved ticks gives
        exactly A~^dt (A~ is *sub*-stochastic). The engine's A^dt cache
        row-normalises after every composition, so what it actually evaluates is
        the forward pass with A~'s rows normalised first. On this case the two
        differ by ~1 nat (~90k ticks x log(row mass of A~) ~ -1e-5 each). This
        test pins the engine's number to the normalised-rows transcription and
        records the gap to the sub-stochastic derivation, so the deviation is
        visible rather than silent; which one the ELBO should use is a design
        question for the header, not a numerical one."""
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
        # the header's derivation gives a lower data term on this case
        self.assertLess(sub_stochastic, vb.loglik - 0.5)


if __name__ == "__main__":
    unittest.main()
