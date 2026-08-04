#!/usr/bin/env python3
"""`HMM.evaluate` — the external contract: statistics and a gradient from one E-step.

Every quantity here is already computed inside an EM map and then discarded, so
`evaluate` costs exactly one forward-backward pass.  The point is that the
statistics summarise the dataset at the *model's* dimension rather than the
data's, which is what lets a host drive its own optimiser or sampler over an HMM
submodel without ever re-touching photons.

The tests check it against things that are already known to be right: the
log-likelihood against `optimize`, the counts against the M-step they must
reproduce, and the score against central finite differences.
"""
import unittest

import numpy as np

import tttrlib


def _engine(seed=4, n_bursts=12, burst_len=150):
    rng = np.random.default_rng(seed)
    true = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.02, 0.98], [0.8, 0.2, 0.3, 0.7])
    times = [np.cumsum(rng.integers(1, 50, size=burst_len)).astype(np.int64).tolist()
             for _ in range(n_bursts)]
    streams = [list(s) for s in tttrlib.HMM.simulate_bursts(true, times, seed + 1)]
    eng = tttrlib.HMM()
    eng.set_bursts(times, streams, 2)
    return eng


def _model():
    """Deliberately *not* the generating model -- a gradient at the optimum is 0."""
    return tttrlib.HmmModel([0.45, 0.55], [0.97, 0.03, 0.04, 0.96],
                            [0.75, 0.25, 0.35, 0.65])


class TestSufficientStatistics(unittest.TestCase):

    def test_loglik_matches_optimize(self):
        """One E-step is one E-step, whoever runs it."""
        eng, m = _engine(), _model()
        self.assertEqual(eng.evaluate(m).loglik, eng.optimize(m, 1, 1e30).loglik)

    def test_counts_reproduce_the_m_step(self):
        """The counts are the M-step's input, so normalising them must give it.

        This is the check that the exported statistics are the *same* objects EM
        uses, not a parallel recomputation that could drift from it.
        """
        eng, m = _engine(), _model()
        ev = eng.evaluate(m)
        one = eng.optimize(m, 1, 1e30)

        obs = ev.gamma_obs_np(2)
        np.testing.assert_allclose(obs / obs.sum(1, keepdims=True), one.obs_np, rtol=1e-12)
        xi = ev.xi_np
        np.testing.assert_allclose(xi / xi.sum(1, keepdims=True), one.trans_np, rtol=1e-12)
        # prior counts are summed over bursts, so the M-step divides by that
        np.testing.assert_allclose(
            np.asarray(ev.prior_counts) / eng.get_n_bursts(), one.prior_np, rtol=1e-12)

    def test_counts_are_raw_not_restrained(self):
        """Restraints must not leak into the exported statistics.

        A consumer supplying its own prior needs the likelihood's contribution
        alone; anything else double-counts. So `evaluate` is unaffected by
        restraints, which only ever enter the M-step.
        """
        eng, m = _engine(), _model()
        plain = eng.evaluate(m)
        # A sticky restraint changes the *fit* substantially...
        sticky = tttrlib.HmmRestraints.sticky(2, 2, 1e4)
        self.assertNotEqual(
            eng.optimize(m, 50, 1e-10, 1e-12, True, False, sticky).trans_np[0, 1],
            eng.optimize(m, 50, 1e-10).trans_np[0, 1])
        # ...but evaluate reports the same statistics regardless.
        np.testing.assert_array_equal(plain.xi_np, eng.evaluate(m).xi_np)


class TestAnalyticPosteriorWidth(unittest.TestCase):
    """`posterior_sd_analytic` — a closed-form width, and a lower bound.

    Conditional on the state path the posterior is Dirichlet, so the E-step's
    expected counts give a posterior with no sampling. What it cannot do is
    marginalise the *path*, and that is where the missing width lives::

        Var(theta|y) = E[Var(theta|y,path)] + Var(E[theta|y,path])

    Both halves of that are asserted here — that it is cheap and centred, and
    that it is narrower than the truth — because shipping only the first would
    invite it to be read as a credible interval.
    """

    def test_it_is_narrower_than_the_sampled_posterior(self):
        """The whole reason it carries a warning.

        Not a tolerance: the analytic width is *systematically* below the
        sampled one for every parameter, because a strictly positive variance
        term has been dropped. If this ever stopped holding, either the sampler
        or the formula would be wrong.
        """
        eng = _engine(n_bursts=25, burst_len=300)
        fit = eng.fit(2, 3, seed=0)
        _, sd_trans, sd_obs = eng.evaluate(fit).posterior_sd_analytic(2)

        post = eng.sample(fit, 400, 300, 2, 11, None, 2)
        _, g_trans, g_obs = post.sd_model()

        for analytic, sampled in ((sd_trans, g_trans), (sd_obs, g_obs)):
            self.assertTrue(np.all(analytic < sampled),
                            f"analytic {analytic} should be below sampled {sampled}")

    def test_the_dirichlet_arithmetic_is_right(self):
        """Checked against the closed form, so a refactor cannot drift."""
        eng = _engine()
        ev = eng.evaluate(_model())
        sd_prior, sd_trans, _ = ev.posterior_sd_analytic(2)

        a = ev.xi_np[0] + 1.0
        a0 = a.sum()
        np.testing.assert_allclose(
            sd_trans[0], np.sqrt(a * (a0 - a) / (a0 ** 2 * (a0 + 1))), rtol=1e-12)
        self.assertEqual(sd_prior.shape, (2,))

    def test_widths_are_positive_and_finite(self):
        eng = _engine()
        for block in eng.evaluate(_model()).posterior_sd_analytic(2):
            self.assertTrue(np.all(np.isfinite(block)))
            self.assertTrue(np.all(block > 0.0))


class TestScoreAgainstFiniteDifferences(unittest.TestCase):
    """Fisher's identity, checked the way it is actually meaningful."""

    H = 1e-5

    def _ll(self, eng, prior, trans, obs):
        return eng.evaluate(tttrlib.HmmModel(list(prior), list(trans), list(obs))).loglik

    def test_prior_and_obs_match_entrywise(self):
        eng, m = _engine(), _model()
        ev = eng.evaluate(m)
        s_prior, _, s_obs = ev.score_np(2)
        pr, tr, ob = m.prior_np, m.trans_np.ravel(), m.obs_np.ravel()

        for vec, analytic, f in ((pr, s_prior, lambda v: self._ll(eng, v, tr, ob)),
                                 (ob, s_obs.ravel(), lambda v: self._ll(eng, pr, tr, v))):
            for i in range(len(vec)):
                hi, lo = np.array(vec, float), np.array(vec, float)
                hi[i] += self.H; lo[i] -= self.H
                fd = (f(hi) - f(lo)) / (2 * self.H)
                self.assertAlmostEqual(fd / analytic[i], 1.0, places=6)

    def test_transitions_match_along_the_simplex(self):
        """Within-row *differences*, because only those stay on the simplex.

        The A^dt cache renormalises after every composition, so scaling a
        transition row changes nothing and the off-simplex component of the
        gradient is an artifact of that. Perturbing a single entry therefore
        appears to disagree by a constant per row -- perturbing
        `(A_ij + h, A_ik - h)`, which a transition row must do to keep summing
        to 1, agrees.
        """
        eng, m = _engine(), _model()
        _, s_trans, _ = eng.evaluate(m).score_np(2)
        pr, tr, ob = m.prior_np, m.trans_np.ravel(), m.obs_np.ravel()

        n = 2
        for i in range(n):
            for j in range(n):
                for k in range(n):
                    if j == k:
                        continue
                    hi, lo = np.array(tr, float), np.array(tr, float)
                    hi[i * n + j] += self.H; hi[i * n + k] -= self.H
                    lo[i * n + j] -= self.H; lo[i * n + k] += self.H
                    fd = (self._ll(eng, pr, hi, ob) - self._ll(eng, pr, lo, ob)) / (2 * self.H)
                    analytic = s_trans[i, j] - s_trans[i, k]
                    self.assertAlmostEqual(fd / analytic, 1.0, places=5,
                                           msg=f"row {i}, {j} vs {k}")

    def test_a_single_entry_disagrees_by_a_constant_per_row(self):
        """The artifact itself, pinned -- so it reads as known rather than as a bug.

        If this ever stops holding, the propagator's normalisation changed, and
        whoever sees it should know that is the cause rather than re-deriving it.
        """
        eng, m = _engine(), _model()
        _, s_trans, _ = eng.evaluate(m).score_np(2)
        pr, tr, ob = m.prior_np, m.trans_np.ravel(), m.obs_np.ravel()

        n = 2
        for i in range(n):
            offsets = []
            for j in range(n):
                hi, lo = np.array(tr, float), np.array(tr, float)
                hi[i * n + j] += self.H; lo[i * n + j] -= self.H
                fd = (self._ll(eng, pr, hi, ob) - self._ll(eng, pr, lo, ob)) / (2 * self.H)
                offsets.append(s_trans[i, j] - fd)
            self.assertAlmostEqual(offsets[0] / offsets[1], 1.0, places=4,
                                   msg=f"row {i}: offset should be constant, got {offsets}")


if __name__ == "__main__":
    unittest.main()
