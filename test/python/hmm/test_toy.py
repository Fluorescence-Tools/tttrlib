#!/usr/bin/env python3
"""The toy harness proves itself, then the engine is held to it.

Two tiers, deliberately in this order:

* ``TestReferenceAgainstEnumeration`` — the scaled recursions in ``toy`` must
  reproduce a brute-force enumeration over every tick-level state path.  This
  is where the reference earns the right to be called a reference; it assumes
  no HMM algebra at all.
* ``TestEngineAgainstReference`` — the C++ engine must reproduce the reference.

The expected one-tick transition counts (``xi``) are the interesting case: the
engine computes them with a deferred ρ contraction over sparse Δt powers, and
the enumeration computes them by literally walking every path and counting.
Agreement between those two is the strongest statement this suite makes.
"""
import unittest

import numpy as np

import tttrlib
import toy
import pytest


# A tiny model and tiny bursts: small enough to enumerate every tick-level path,
# asymmetric enough that a transposed or mis-indexed matrix would show up.
TINY_PRIOR = [0.6, 0.4]
TINY_A = [[0.90, 0.10], [0.25, 0.75]]
TINY_B = [[0.70, 0.30], [0.35, 0.65]]

TINY_BURSTS = [
    ([0, 1, 2], [0, 1, 0]),
    ([0, 2, 5], [1, 1, 0]),
    ([0, 3], [0, 1]),
]


def _engine(bursts, n_streams=2):
    eng = tttrlib.HMM()
    eng.set_bursts([list(map(int, t)) for t, _ in bursts],
                   [list(map(int, s)) for _, s in bursts], n_streams)
    return eng


def _model(prior, A, B):
    prior, A, B = toy.normalize_model(prior, A, B)
    return tttrlib.HmmModel(list(prior.ravel()), list(A.ravel()), list(B.ravel()))


class TestReferenceAgainstEnumeration(unittest.TestCase):
    """Layer 2 (recursions) against layer 1 (brute force)."""

    def test_loglik_matches_enumeration(self):
        for times, streams in TINY_BURSTS:
            ref = toy.burst_loglik(TINY_PRIOR, TINY_A, TINY_B, times, streams)
            brute, _, _, _ = toy.enumerate_burst(TINY_PRIOR, TINY_A, TINY_B, times, streams)
            self.assertAlmostEqual(ref, brute, places=12)

    def test_expected_counts_match_enumeration(self):
        """gamma, prior and the one-tick xi all agree with the path walk."""
        for times, streams in TINY_BURSTS:
            prior_acc, gamma_obs, xi, ll = toy.expected_counts(
                TINY_PRIOR, TINY_A, TINY_B, [(times, streams)])
            b_ll, b_prior, b_gamma, b_xi = toy.enumerate_burst(
                TINY_PRIOR, TINY_A, TINY_B, times, streams)
            self.assertAlmostEqual(ll, b_ll, places=12)
            np.testing.assert_allclose(prior_acc, b_prior, atol=1e-12)
            np.testing.assert_allclose(gamma_obs, b_gamma, atol=1e-12)
            np.testing.assert_allclose(xi, b_xi, atol=1e-12)

    def test_xi_counts_total_ticks(self):
        """Sanity: expected one-tick transitions sum to the number of ticks."""
        for times, streams in TINY_BURSTS:
            _, _, xi, _ = toy.expected_counts(
                TINY_PRIOR, TINY_A, TINY_B, [(times, streams)])
            self.assertAlmostEqual(xi.sum(), times[-1] - times[0], places=10)

    def test_gamma_is_a_distribution(self):
        fb = toy.forward_backward(TINY_PRIOR, TINY_A, TINY_B, *TINY_BURSTS[1])
        np.testing.assert_allclose(fb["gamma"].sum(axis=1), 1.0, atol=1e-12)


class TestEngineAgainstReference(unittest.TestCase):
    """The C++ engine against the proven reference."""

    def test_loglik_matches(self):
        eng = _engine(TINY_BURSTS)
        # One EM map: the reported loglik is that of the *input* model.
        fit = eng.optimize(_model(TINY_PRIOR, TINY_A, TINY_B), 1, 1e30)
        ref = toy.total_loglik(TINY_PRIOR, TINY_A, TINY_B, TINY_BURSTS)
        self.assertAlmostEqual(fit.loglik, ref, places=10)

    def test_gamma_matches(self):
        eng = _engine(TINY_BURSTS)
        gamma, n_underflow = eng.gamma(_model(TINY_PRIOR, TINY_A, TINY_B))
        self.assertEqual(n_underflow, 0)
        rows = [toy.forward_backward(TINY_PRIOR, TINY_A, TINY_B, t, s)["gamma"]
                for t, s in TINY_BURSTS]
        # gamma is float32, so compare at float32 precision
        np.testing.assert_allclose(gamma, np.vstack(rows), atol=1e-5)

    def test_m_step_matches(self):
        """One EM map's output model equals the reference M-step."""
        eng = _engine(TINY_BURSTS)
        fit = eng.optimize(_model(TINY_PRIOR, TINY_A, TINY_B), 1, 1e30)
        prior_acc, gamma_obs, xi, _ = toy.expected_counts(
            TINY_PRIOR, TINY_A, TINY_B, TINY_BURSTS)
        prior, A, B = toy.normalize_model(
            prior_acc / len(TINY_BURSTS), xi, gamma_obs)
        np.testing.assert_allclose(fit.prior_np, prior, atol=1e-9)
        np.testing.assert_allclose(fit.trans_np, A, atol=1e-9)
        np.testing.assert_allclose(fit.obs_np, B, atol=1e-9)

    def test_viterbi_matches(self):
        eng = _engine(TINY_BURSTS)
        path, _ = eng.viterbi_path(_model(TINY_PRIOR, TINY_A, TINY_B))
        ref = np.concatenate([
            toy.viterbi(TINY_PRIOR, TINY_A, TINY_B, t, s)[0] for t, s in TINY_BURSTS])
        np.testing.assert_array_equal(path, ref)

    @pytest.mark.slow
    @pytest.mark.smoke
    def test_em_reaches_same_fixed_point(self):
        """Full EM on simulated data: engine and reference agree on logL."""
        true = toy.two_state(e_low=0.2, e_high=0.8, k_switch=2e-3)
        times, streams = toy.simulate(*true, n_bursts=12, burst_len=60,
                                      mean_gap=20, seed=7)
        bursts = list(zip(times, streams))
        init = toy.two_state(e_low=0.35, e_high=0.65, k_switch=5e-3)

        eng = _engine(bursts)
        fit = eng.optimize(_model(*init), 500, 1e-11, 1e-12, False)
        *_, ref_ll, _, _ = toy.em(*init, bursts, max_iter=500, tol=1e-11)

        self.assertAlmostEqual(fit.loglik, ref_ll, delta=1e-6)


if __name__ == "__main__":
    unittest.main()
