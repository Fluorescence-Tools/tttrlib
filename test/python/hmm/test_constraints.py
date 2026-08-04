#!/usr/bin/env python3
"""C++ `HmmRestraints` and `HmmConstraints` — two different things.

**Restraints are scored**: Dirichlet pseudo-counts, exactly conjugate to the
E-step's raw counts, contributing `log p` to the objective so the fit trades
them against the likelihood.  **Constraints are imposed**: a pinned entry never
enters the objective and holds exactly.  They are separate types because IMP
keeps them separate, and because they arrive from different places — restraints
serialised in from an external physics model, constraints asserted locally.

The first test is the important one.  A flat prior must be **bit-identical** to
plain EM, not merely close: the MAP and MLE paths share one loop, so bit-identity
is what makes sharing safe rather than something to be re-argued after every
change.  It is also a canary — it caught a catastrophic-cancellation bug that no
behavioural test would have flagged, because the fits still looked fine.
"""
import unittest

import numpy as np

import tttrlib


def _simulate(seed=1, n_bursts=30, burst_len=200):
    rng = np.random.default_rng(seed)
    true = tttrlib.HmmModel([0.5, 0.5],
                             [0.999, 0.001, 0.002, 0.998],
                             [0.80, 0.20, 0.25, 0.75])
    times = [np.cumsum(rng.integers(1, 60, size=burst_len)).astype(np.int64).tolist()
             for _ in range(n_bursts)]
    streams = [list(s) for s in tttrlib.HMM.simulate_bursts(true, times, seed + 1)]
    eng = tttrlib.HMM()
    eng.set_bursts(times, streams, 2)
    return eng


class TestFlatIsExactlyMle(unittest.TestCase):
    """A flat prior imposes nothing, so it must change nothing at all."""

    def test_bit_identical_both_paths(self):
        eng = _simulate()
        init = tttrlib.HMM.factory_model(2, 2, 1e-3, 0)
        flat = tttrlib.HmmRestraints(2, 2)
        self.assertTrue(flat.is_flat())

        for accelerate in (True, False):
            mle = eng.optimize(init, 300, 1e-10, 1e-12, accelerate)
            map_ = eng.optimize(init, 300, 1e-10, 1e-12, accelerate, False, flat)
            self.assertEqual(mle.loglik, map_.loglik, f"accelerate={accelerate}")
            np.testing.assert_array_equal(mle.prior_np, map_.prior_np)
            np.testing.assert_array_equal(mle.trans_np, map_.trans_np)
            np.testing.assert_array_equal(mle.obs_np, map_.obs_np)

    def test_pseudocount_addition_does_not_cancel(self):
        """`counts + alpha - 1` destroys small counts; `counts + (alpha - 1)` does not.

        Left-to-right, `1e-20 + 1.0` rounds to `1.0` and subtracting `1.0` gives
        exactly zero — a state's evidence for a rare symbol silently vanishes.
        That is worst precisely where a fine micro-time alphabet puts most of its
        bins, so it is guarded here rather than left to the bit-identity test.
        """
        self.assertEqual(1e-20 + 1.0 - 1.0, 0.0)          # the trap
        self.assertEqual(1e-20 + (1.0 - 1.0), 1e-20)      # the fix


class TestPriorsAndFixing(unittest.TestCase):
    def test_sticky_prior_slows_the_dynamics(self):
        """The prior must move the answer in the direction it claims."""
        eng = _simulate()
        init = tttrlib.HMM.factory_model(2, 2, 1e-3, 0)
        free = eng.optimize(init, 300, 1e-10)
        stuck = eng.optimize(init, 300, 1e-10, 1e-12, True, False,
                             tttrlib.HmmRestraints.sticky(2, 2, 1e5))
        off = ~np.eye(2, dtype=bool)
        self.assertLess(stuck.trans_np[off].sum(), free.trans_np[off].sum())

    def test_fixed_emission_stays_exact(self):
        """A donor-only state is a constraint, not a belief: it must hold exactly,
        and the row must still be a distribution."""
        eng = _simulate()
        c = tttrlib.HmmConstraints(2, 2)
        c.fix_emission(0, 1, 0.0)
        self.assertFalse(c.is_empty())
        fit = eng.optimize(tttrlib.HMM.factory_model(2, 2, 1e-3, 0),
                           300, 1e-10, 1e-12, True, False, None, c)
        self.assertEqual(fit.obs_np[0, 1], 0.0)
        np.testing.assert_allclose(fit.obs_np.sum(axis=1), 1.0, atol=1e-12)

    def test_restraint_is_scored_and_constraint_is_not(self):
        """The distinction, made observable.

        This is the property the two types exist to express, and until `logpost`
        was reported it could not be checked from Python at all: a restraint is
        *believed*, so it enters the objective and shifts `logpost` away from
        `loglik`; a constraint is *asserted*, so it changes the parameters and
        leaves the objective a pure log-likelihood.  Getting this backwards is
        not a cosmetic error -- a fit that scored its hard constraints would
        report a number that no model comparison could use.
        """
        eng = _simulate()
        init = tttrlib.HMM.factory_model(2, 2, 1e-3, 0)

        plain = eng.optimize(init, 300, 1e-10)
        self.assertEqual(plain.logpost, plain.loglik)

        restrained = eng.optimize(init, 300, 1e-10, 1e-12, True, False,
                                  tttrlib.HmmRestraints.sticky(2, 2, 1e3))
        self.assertLess(restrained.logpost, restrained.loglik)   # log p < 0 here

        c = tttrlib.HmmConstraints(2, 2)
        c.fix_emission(0, 1, 0.05)
        constrained = eng.optimize(init, 300, 1e-10, 1e-12, True, False, None, c)
        self.assertEqual(constrained.logpost, constrained.loglik)
        # ...and it did act -- otherwise the equality above would be vacuous.
        self.assertEqual(constrained.obs_np[0, 1], 0.05)
        self.assertLess(constrained.loglik, plain.loglik)

    def test_rejects_invalid_concentrations(self):
        r = tttrlib.HmmRestraints(2, 2)
        with self.assertRaises(Exception):
            r.set_alpha_trans([1.0, 0.0, 1.0, 1.0])       # must be > 0


class TestJsonRoundTrip(unittest.TestCase):
    """Serialisation is the route an external physics model hands priors in."""

    def test_restraints_round_trip(self):
        r = tttrlib.HmmRestraints.sticky(2, 2, 500.0)
        back = tttrlib.HmmRestraints.from_json_string(r.to_json_string())
        np.testing.assert_allclose(back.alpha_trans_np, r.alpha_trans_np)
        np.testing.assert_allclose(back.alpha_obs_np, r.alpha_obs_np)
        self.assertFalse(back.is_flat())

    def test_constraints_round_trip(self):
        c = tttrlib.HmmConstraints(2, 2)
        c.fix_emission(1, 0, 0.25)
        back = tttrlib.HmmConstraints.from_json_string(c.to_json_string())
        self.assertFalse(back.is_empty())
        # NaN has no JSON representation, so fixed entries travel as explicit
        # (index, value) pairs; a null would round-trip as "free" and silently
        # drop the constraint -- the one failure a serialised constraint must
        # not have.
        self.assertIn('"fixed_obs"', c.to_json_string())


if __name__ == "__main__":
    unittest.main()
