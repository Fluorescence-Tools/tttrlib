"""General N-pattern fit: non-negative amplitudes of arbitrary fixed patterns.

``decay_pattern_fit`` is the first consumer of ``DecayFitProblem::patterns``:
given N fixed reference shapes and data, find non-negative amplitudes. Three
modes share one design matrix (see DecayPatternFit.h): plain NNLS (``kNone``),
L2-regularised NNLS (``kTikhonov``), and Skilling-Bryan maximum-entropy
(``kMaxEnt``). Every check here is either an independent reference
(``scipy.optimize.nnls`` for the unregularised case) or a property the
algorithm must have by construction (non-negativity, monotonic shrinkage with
regularisation strength, exact recovery of a noiseless mixture).
"""
import unittest

import numpy as np
import scipy.optimize
import tttrlib


def _synth_patterns(n_bins=64, taus=(1.0, 2.0, 3.0)):
    x = np.linspace(0.0, 5.0, n_bins)
    return [np.exp(-x / tau) for tau in taus]


class TestPatternFitNnls(unittest.TestCase):
    """kNone: plain NNLS, checked against scipy.optimize.nnls."""

    def test_matches_scipy_on_a_clean_mixture(self):
        patterns = _synth_patterns()
        true_amps = np.array([2.0, 0.5, 3.0])
        data = sum(a * p for a, p in zip(true_amps, patterns))

        A = np.column_stack(patterns)
        ref_amps, _ = scipy.optimize.nnls(A, data)

        r = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kNone, 0.0, [], 500, 1e-10)
        np.testing.assert_allclose(r.amplitudes, ref_amps, atol=1e-6)

    def test_matches_scipy_with_noise_and_more_patterns(self):
        rng = np.random.default_rng(7)
        patterns = _synth_patterns(n_bins=128, taus=np.linspace(1.0, 4.0, 6))
        true_amps = rng.uniform(0.2, 5.0, len(patterns))
        data = sum(a * p for a, p in zip(true_amps, patterns))
        data = data + 0.02 * rng.standard_normal(len(data))

        A = np.column_stack(patterns)
        ref_amps, _ = scipy.optimize.nnls(A, data)

        r = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kNone, 0.0, [], 500, 1e-10)
        np.testing.assert_allclose(r.amplitudes, ref_amps, atol=1e-4)

    def test_recovers_a_noiseless_mixture_exactly(self):
        patterns = _synth_patterns()
        true_amps = np.array([1.5, 0.0, 4.2])  # one pattern genuinely absent
        data = sum(a * p for a, p in zip(true_amps, patterns))

        r = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kNone, 0.0, [], 500, 1e-10)
        np.testing.assert_allclose(r.amplitudes, true_amps, atol=1e-6)
        self.assertLess(r.chisq, 1e-12)

    def test_amplitudes_are_never_negative(self):
        rng = np.random.default_rng(3)
        patterns = _synth_patterns(taus=(1.0, 1.05, 1.1))  # near-collinear
        data = rng.uniform(0.0, 1.0, len(patterns[0]))

        r = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kNone, 0.0, [], 500, 1e-10)
        self.assertTrue(all(v >= 0.0 for v in r.amplitudes))


class TestPatternFitTikhonov(unittest.TestCase):
    """kTikhonov: L2-regularised, non-negative; shrinks toward zero."""

    def test_zero_lambda_matches_plain_nnls(self):
        patterns = _synth_patterns()
        true_amps = np.array([2.0, 0.5, 3.0])
        data = sum(a * p for a, p in zip(true_amps, patterns))

        r_none = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kNone, 0.0, [], 500, 1e-10)
        r_tik = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kTikhonov, 0.0, [], 500, 1e-10)
        np.testing.assert_allclose(r_tik.amplitudes, r_none.amplitudes, atol=1e-6)

    def test_amplitude_norm_shrinks_monotonically_with_lambda(self):
        patterns = _synth_patterns()
        true_amps = np.array([2.0, 0.5, 3.0])
        data = sum(a * p for a, p in zip(true_amps, patterns))

        norms = []
        for lam in (0.0, 1.0, 10.0, 100.0):
            r = tttrlib.decay_pattern_fit(
                list(data), [list(p) for p in patterns],
                tttrlib.PatternFitMode_kTikhonov, lam, [], 500, 1e-10)
            self.assertTrue(all(v >= -1e-9 for v in r.amplitudes))
            norms.append(np.linalg.norm(r.amplitudes))
        self.assertEqual(norms, sorted(norms, reverse=True))


class TestPatternFitMaxEnt(unittest.TestCase):
    """kMaxEnt: Skilling-Bryan, non-negative; shrinks toward the prior."""

    def test_zero_nu_matches_plain_nnls(self):
        patterns = _synth_patterns()
        true_amps = np.array([2.0, 0.5, 3.0])
        data = sum(a * p for a, p in zip(true_amps, patterns))

        r_none = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kNone, 0.0, [], 500, 1e-10)
        r_mem = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kMaxEnt, 0.0, [], 500, 1e-6)
        np.testing.assert_allclose(r_mem.amplitudes, r_none.amplitudes, atol=1e-3)

    def test_large_nu_pulls_toward_the_uniform_prior(self):
        patterns = _synth_patterns()
        true_amps = np.array([2.0, 0.5, 3.0])
        data = sum(a * p for a, p in zip(true_amps, patterns))

        r = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kMaxEnt, 1e5, [], 500, 1e-6)
        amps = np.asarray(r.amplitudes)
        # every amplitude collapses toward the same (uniform-prior) value
        self.assertLess(np.std(amps), 0.05)
        self.assertTrue(np.all(amps >= 0.0))

    def test_a_non_uniform_prior_is_honoured(self):
        patterns = _synth_patterns()
        true_amps = np.array([2.0, 0.5, 3.0])
        data = sum(a * p for a, p in zip(true_amps, patterns))
        prior = [1.0, 1.0, 5.0]  # strong prior belief that pattern 3 dominates

        r = tttrlib.decay_pattern_fit(
            list(data), [list(p) for p in patterns],
            tttrlib.PatternFitMode_kMaxEnt, 1e5, prior, 500, 1e-6)
        amps = np.asarray(r.amplitudes)
        ratio = amps / np.asarray(prior)
        # at very high nu every amplitude is pulled to the same multiple of its prior
        self.assertLess(np.std(ratio), 0.05)


if __name__ == '__main__':
    unittest.main()
