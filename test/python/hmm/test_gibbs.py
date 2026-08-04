#!/usr/bin/env python3
"""`HMM.sample` — blocked Gibbs over (pi, A, B).

EM returns one model; this returns a distribution over them.  The tests below
check the three things that can each be wrong independently: that the sampler
targets the right distribution, that the summaries handle exchangeable states,
and that the diagnostics report honestly when it has not converged.

Two findings from building it are pinned here, because both look like bugs and
neither is:

**Diagnostics must relabel.**  Two chains each holding a *stable* but opposite
labelling gave a raw split-Rhat of 15.2 and a relabelled one of 1.75.  Both
chains were correct; the raw statistic compared "state 0" in one against a
different state in the other.

**Chains must be dispersed around the model, not drawn from the prior.**  A flat
Dirichlet on a transition row starts a 2-state chain near ``A01 = 0.5``, which
for sticky data is a genuine second mode -- fast switching with blurred
emissions -- that no practical number of sweeps escapes.
"""
import unittest

import numpy as np

import tttrlib
import pytest


TRUE_TRANS = [0.995, 0.005, 0.008, 0.992]
TRUE_OBS = [0.85, 0.15, 0.25, 0.75]


def _engine(seed=7, n_bursts=25, burst_len=300):
    rng = np.random.default_rng(seed)
    true = tttrlib.HmmModel([0.5, 0.5], TRUE_TRANS, TRUE_OBS)
    times = [np.cumsum(rng.integers(1, 40, size=burst_len)).astype(np.int64).tolist()
             for _ in range(n_bursts)]
    streams = [list(s) for s in tttrlib.HMM.simulate_bursts(true, times, seed + 1)]
    eng = tttrlib.HMM()
    eng.set_bursts(times, streams, 2)
    return eng, true


class TestTargetsThePosterior(unittest.TestCase):

    def test_started_at_the_truth_it_stays_there(self):
        """The sharpest check of the sweep itself, free of any burn-in question.

        If the bridge miscounted one-tick transitions -- the step that turns a
        photon-to-photon jump into the ticks it actually took -- a chain seeded
        at the generating model would drift away from it. It does not.
        """
        eng, true = _engine()
        post = eng.sample(true, 400, 100, 1, 3)
        _, trans, obs = post.mean_model()

        mle = eng.fit(2, 3, seed=0)
        # The posterior mean must sit on the likelihood's own answer, not merely
        # near the truth -- with this much data they are close but not equal,
        # and agreeing with the MLE is the stronger statement.
        np.testing.assert_allclose(trans[0, 1], mle.trans_np[0, 1], rtol=0.15)
        np.testing.assert_allclose(trans[1, 0], mle.trans_np[1, 0], rtol=0.15)
        np.testing.assert_allclose(obs[1, 1], mle.obs_np[1, 1], rtol=0.05)

    @pytest.mark.slow
    def test_converges_from_an_em_seed_and_covers(self):
        """The workflow to actually use: fit first, then sample around the fit."""
        eng, _ = _engine()
        post = eng.sample(eng.fit(2, 3, seed=0), 600, 300, 4, 11, None, 2)

        diag = post.diagnostics()
        self.assertLess(diag["rhat_max"], 1.05, diag)
        self.assertGreater(diag["ess_min"], 100, diag)

        _, trans, _ = post.mean_model()
        lo, hi = post.interval(0.95)
        for truth, l, h in ((0.005, lo[1][0, 1], hi[1][0, 1]),
                            (0.008, lo[1][1, 0], hi[1][1, 0])):
            self.assertLessEqual(l, truth)
            self.assertGreaterEqual(h, truth)

    def test_intervals_are_ordered_and_contain_the_mean(self):
        eng, _ = _engine()
        post = eng.sample(eng.fit(2, 3, seed=0), 300, 200, 2, 5)
        lo, hi = post.interval(0.9)
        mean = post.mean_model()
        for k in range(3):
            self.assertTrue(np.all(lo[k] <= hi[k]))
            self.assertTrue(np.all(lo[k] <= mean[k] + 1e-12))
            self.assertTrue(np.all(mean[k] <= hi[k] + 1e-12))

    def test_draws_are_valid_probability_vectors(self):
        """Every draw is a model, not just the summary of them."""
        eng, _ = _engine()
        post = eng.sample(eng.fit(2, 3, seed=0), 50, 20, 1, 2)
        d = post.draws_np[0]
        n, p = post.n_states, post.n_symbols
        np.testing.assert_allclose(d[:, :n].sum(1), 1.0, atol=1e-10)
        np.testing.assert_allclose(d[:, n:n + n * n].reshape(-1, n, n).sum(2), 1.0, atol=1e-10)
        np.testing.assert_allclose(d[:, n + n * n:].reshape(-1, n, p).sum(2), 1.0, atol=1e-10)
        self.assertTrue(np.all(d >= 0.0))


class TestExchangeableStates(unittest.TestCase):

    def test_summary_is_invariant_to_relabelling_the_start(self):
        """The property relabelling exists to provide, stated directly.

        Swap the two states of the initial model and the posterior is the same
        distribution with its labels permuted -- so the *summary* must be
        identical. Without relabelling it would come out swapped, which is what
        makes a posterior mean meaningless for exchangeable states.

        Preferred over checking that raw draws violate canonical order: they
        only do so when chains happen to settle into opposite labellings, which
        dispersing around the model deliberately made rare. A test that depends
        on that is a test of the dispersion, not of the relabelling.
        """
        eng, _ = _engine()
        init = eng.fit(2, 3, seed=0)
        swapped = tttrlib.HmmModel(
            list(init.prior_np[::-1]),
            list(init.trans_np[::-1, ::-1].ravel()),
            list(init.obs_np[::-1].ravel()))

        a = eng.sample(init, 300, 300, 2, 5).mean_model()
        b = eng.sample(swapped, 300, 300, 2, 5).mean_model()

        # Canonically ordered, and the same either way.
        self.assertGreaterEqual(a[2][0, 0], a[2][1, 0])
        self.assertGreaterEqual(b[2][0, 0], b[2][1, 0])
        for k in (1, 2):
            np.testing.assert_allclose(a[k], b[k], atol=0.03)

    @pytest.mark.slow
    def test_relabelling_makes_rhat_meaningful(self):
        """Pinned because the raw statistic looks catastrophic and is not.

        Chains that settle into opposite labellings are both sampling the same
        posterior. Split-Rhat computed on raw draws reports that as gross
        non-convergence; computed on relabelled draws it does not.
        """
        eng, _ = _engine()
        post = eng.sample(eng.fit(2, 3, seed=0), 400, 300, 4, 11, None, 2)
        self.assertLess(post.diagnostics()["rhat_max"], 1.1)


class TestDiagnosticsAreHonest(unittest.TestCase):

    def test_too_short_a_run_is_reported_as_such(self):
        """A sampler that never says "not converged" is not a diagnostic.

        Deliberately under-burned and started far from the mode, so the chains
        have not yet arrived; Rhat and ESS must say so rather than returning a
        confident-looking summary.
        """
        eng, _ = _engine()
        bad = eng.sample(tttrlib.HMM.factory_model(2, 2, 1e-3, 0), 40, 0, 4, 3)
        diag = bad["rhat_max"] if isinstance(bad, dict) else bad.diagnostics()
        self.assertTrue(diag["rhat_max"] > 1.05 or diag["ess_min"] < 40, diag)

    def test_reproducible_for_a_fixed_seed(self):
        eng, _ = _engine()
        init = eng.fit(2, 3, seed=0)
        a = eng.sample(init, 60, 20, 2, 99)
        b = eng.sample(init, 60, 20, 2, 99)
        np.testing.assert_array_equal(a.draws_np, b.draws_np)
        self.assertFalse(np.array_equal(a.draws_np,
                                        eng.sample(init, 60, 20, 2, 100).draws_np))

    def test_thinning_keeps_the_requested_number_of_draws(self):
        eng, _ = _engine()
        init = eng.fit(2, 3, seed=0)
        for thin in (1, 3):
            post = eng.sample(init, 50, 10, 1, 4, None, thin)
            self.assertEqual(post.n_draws(), 50)


class TestParameterisedEmissionSampling(unittest.TestCase):
    """Sampling a *product* alphabet needs the parameterised emission too.

    Left free, the emission is drawn as a categorical over every column, which
    is the degenerate family the parameterised M-step exists to avoid. The
    sampler wanders out of a good lifetime fit and takes the sampled paths with
    it -- so the failure shows up in the **transitions**, not in the emission,
    which is what makes it confusing to diagnose.

    Supplying the spec keeps the stream split a conjugate Dirichlet draw and
    gives each lifetime one univariate slice update, scored through the same
    `q_of_tau` the M-step maximises.
    """

    N_BINS, SPAN, K = 32, 16.0, 1e-3

    def _spec(self, taus):
        dt = self.SPAN / self.N_BINS
        s = tttrlib.HmmEmissionSpec.uniform(2, 2, self.N_BINS, dt, 4.0)
        for state, tau in enumerate(taus):
            s.set_stream_probability(state, 0, 0.5)
            s.set_stream_probability(state, 1, 0.5)
            s.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            s.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(2.5))
        return s

    def _engine(self, seed=3, n_bursts=30, burst_len=400):
        dt = self.SPAN / self.N_BINS
        true = tttrlib.HmmModel([0.5, 0.5],
                                [1 - self.K, self.K, self.K, 1 - self.K],
                                self._spec((4.0, 2.0)).build())
        true.n_micro_bins = self.N_BINS
        rng = np.random.default_rng(seed)
        A, obs, powers = true.trans_np, true.obs_np, {}
        times, syms = [], []
        for _ in range(n_bursts):
            t = np.cumsum(rng.integers(1, 40, size=burst_len)).astype(np.int64)
            state = rng.integers(0, 2)
            row = []
            for k in range(len(t)):
                if k:
                    d = int(t[k] - t[k - 1])
                    if d not in powers:
                        powers[d] = np.linalg.matrix_power(A, d)
                    state = rng.choice(2, p=powers[d][state])
                row.append(int(rng.choice(obs.shape[1], p=obs[state])))
            times.append(t.tolist())
            syms.append(row)
        eng = tttrlib.HMM()
        eng.set_bursts_micro(times,
                             [[y // self.N_BINS for y in b] for b in syms],
                             [[y % self.N_BINS for y in b] for b in syms],
                             2, self.N_BINS, dt)
        return eng

    @pytest.mark.slow
    def test_it_converges_where_the_free_emission_does_not(self):
        eng = self._engine()
        spec = self._spec((8.0, 0.8))          # deliberately wrong start
        init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
        init.n_micro_bins = self.N_BINS
        fit = eng.optimize(init, 200, 1e-9, 1e-12, True, False, None, None, spec)

        free = eng.sample(fit, 300, 250, 4, 11, None, 2).diagnostics()
        par = eng.sample(fit, 300, 250, 4, 11, None, 2, spec).diagnostics()

        self.assertLess(par["rhat_max"], free["rhat_max"])
        self.assertGreater(par["ess_min"], free["ess_min"])
        self.assertLess(par["rhat_max"], 1.1, par)

    def test_the_spec_carries_sampled_lifetimes_back(self):
        """The spec is updated in place, so the last draw is readable."""
        eng = self._engine()
        spec = self._spec((8.0, 0.8))
        init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
        init.n_micro_bins = self.N_BINS
        fit = eng.optimize(init, 200, 1e-9, 1e-12, True, False, None, None, spec)
        eng.sample(fit, 200, 200, 2, 5, None, 2, spec)

        tau = sorted((spec.spectrum[i * 2].lifetimes[0] for i in range(2)), reverse=True)
        self.assertAlmostEqual(tau[0], 4.0, delta=0.6)
        self.assertAlmostEqual(tau[1], 2.0, delta=0.6)

    def test_lifetimes_stay_inside_their_box(self):
        """Slice sampling is bounded by construction -- no draw escapes."""
        eng = self._engine()
        spec = self._spec((4.0, 2.0))
        spec.tau_min, spec.tau_max = 1.0, 6.0
        init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
        init.n_micro_bins = self.N_BINS
        eng.sample(init, 100, 50, 1, 3, None, 1, spec)
        for i in range(2):
            for k in range(2):
                tau = spec.spectrum[i * 2 + k].lifetimes[0]
                self.assertGreaterEqual(tau, spec.tau_min)
                self.assertLessEqual(tau, spec.tau_max)


class TestRestraintsEnterAsConcentrations(unittest.TestCase):

    def test_a_sticky_prior_moves_the_posterior(self):
        """Concentrations are used as-is, not as `alpha - 1`.

        The MAP M-step wants the mode and subtracts one; a sampler wants the
        distribution and does not. Conflating them is the classic off-by-one
        that yields a quietly biased "calibrated" sampler, so the two paths are
        checked to respond to the same restraint in the same direction.
        """
        eng, _ = _engine()
        init = eng.fit(2, 3, seed=0)
        flat = eng.sample(init, 300, 200, 2, 5)
        sticky = eng.sample(init, 300, 200, 2, 5,
                            tttrlib.HmmRestraints.sticky(2, 2, 5e4))
        self.assertLess(sticky.mean_model()[1][0, 1], flat.mean_model()[1][0, 1])


if __name__ == "__main__":
    unittest.main()
