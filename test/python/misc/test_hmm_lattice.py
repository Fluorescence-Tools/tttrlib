"""The log-domain HMM lattice (`HmmLattice.h`).

Three things are pinned here, and only the first is about arithmetic.

**The recursions are right.** A two-state lattice is checked against a
brute-force enumeration of every state path — feasible at small `T`, exact, and
independent of the recursion being tested. Viterbi is checked against the
arg-max of the same enumeration, not against itself.

**`-inf` is a value, not an error.** A structurally constrained model has whole
`-inf` columns, and a frame no state can explain is `-inf` across. Those must
propagate to an `-inf` log-likelihood with **no nan anywhere** — including
`xi_sum`, where an impossible sequence must contribute exactly zero counts. That
is not a hypothetical: the numba implementation this was ported from computed
`exp(-inf + -inf - -inf)` there, and because `xi_sum` is the accumulator shared
by every sequence in an E-step, one unexplainable frame turned the whole
transition matrix to nan for that EM iteration and every one after. The
posteriors survive it — their uniform fallback catches the nan total — so the
only symptom was a fit that stopped improving. Fixed there first
(chisurf `f6e960190`), and these tests are what stops it coming back here.

These are also the tests that catch `-ffast-math` reaching this translation
unit: `nnan`/`ninf` license the compiler to fold away the guards the recursion
depends on. `modules/math/CMakeLists.txt` pins the flag off; this notices if the
pin is ever lost.

**It agrees with the implementation it replaces.** `hmm_lattice_numba_parity.npz`
is recorded from ChiSurf's numba kernels, which this exists to delete. Ten
cases including the two degenerate ones. A live comparison would have been
better until the day numba leaves — after which it becomes a skip, and a skip
reads like a pass.

That fixture is not circular: it was cross-checked against **hmmlearn**, the
library ChiSurf's kernels replaced. Forward and backward lattices come out
bit-identical, log-likelihoods agree on every case including the two `-inf`
ones, and posteriors and `xi_sum` differ by ≤ 3.1e-14 and ≤ 6.8e-13 — the
accumulation-order noise of fusing three passes into one sweep. So passing here
means agreeing with an independent implementation, transitively.

The one place they legitimately diverge is the Viterbi *path* on an impossible
sequence, where every candidate scores `-inf` and only the tie-break decides.
Those cases assert the log-probability and not the path; see the comment there.
"""

import itertools
import unittest
from pathlib import Path

import numpy as np

import tttrlib

FIXTURE = (
    Path(__file__).resolve().parents[2] / "data" / "reference" / "hmm_lattice_numba_parity.npz"
)


def brute_force(log_startprob, log_transmat, log_frameprob):
    """Enumerate every state path. Returns (log_prob, best_path, best_logp)."""
    n_samples, n_states = log_frameprob.shape
    total, best, best_path = -np.inf, -np.inf, None
    for path in itertools.product(range(n_states), repeat=n_samples):
        logp = log_startprob[path[0]] + log_frameprob[0, path[0]]
        for t in range(1, n_samples):
            logp += log_transmat[path[t - 1], path[t]] + log_frameprob[t, path[t]]
        total = np.logaddexp(total, logp)
        if logp > best:
            best, best_path = logp, np.asarray(path, dtype=np.int64)
    return total, best_path, best


def posteriors_brute_force(log_startprob, log_transmat, log_frameprob):
    """Marginal state probabilities from the same enumeration."""
    n_samples, n_states = log_frameprob.shape
    acc = np.full((n_samples, n_states), -np.inf)
    for path in itertools.product(range(n_states), repeat=n_samples):
        logp = log_startprob[path[0]] + log_frameprob[0, path[0]]
        for t in range(1, n_samples):
            logp += log_transmat[path[t - 1], path[t]] + log_frameprob[t, path[t]]
        for t, state in enumerate(path):
            acc[t, state] = np.logaddexp(acc[t, state], logp)
    total = np.logaddexp.reduce(acc[0])
    return np.exp(acc - total)


def a_small_model(seed=7, n_samples=6, n_states=2):
    rng = np.random.default_rng(seed)
    log_startprob = np.log(rng.dirichlet(np.ones(n_states)))
    log_transmat = np.log(rng.dirichlet(np.ones(n_states), size=n_states))
    log_frameprob = np.log(rng.random((n_samples, n_states)))
    return log_startprob, log_transmat, np.ascontiguousarray(log_frameprob)


def run_forward(log_startprob, log_transmat, log_frameprob):
    fwd = np.empty_like(log_frameprob)
    log_prob = tttrlib.hmm_forward_log(log_startprob, log_transmat, log_frameprob, fwd)
    return log_prob, fwd


class TestAgainstAnEnumeration(unittest.TestCase):
    """Small enough to enumerate every path, so the reference is exact."""

    def test_the_forward_recursion_is_the_sum_over_all_paths(self):
        start, trans, frame = a_small_model()
        log_prob, _ = run_forward(start, trans, frame)
        expected, _, _ = brute_force(start, trans, frame)
        self.assertAlmostEqual(log_prob, expected, places=12)

    def test_the_posteriors_are_the_path_marginals(self):
        start, trans, frame = a_small_model()
        log_prob, fwd = run_forward(start, trans, frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((frame.shape[1], frame.shape[1]))
        tttrlib.hmm_backward_posteriors_xi(
            trans, frame, fwd, log_prob, posteriors, xi_sum)
        np.testing.assert_allclose(
            posteriors, posteriors_brute_force(start, trans, frame), rtol=1e-10, atol=1e-12)

    def test_viterbi_is_the_arg_max_of_the_enumeration(self):
        start, trans, frame = a_small_model()
        states = np.empty(frame.shape[0], dtype=np.int64)
        logp = tttrlib.hmm_viterbi_log(start, trans, frame, states)
        _, best_path, best = brute_force(start, trans, frame)
        np.testing.assert_array_equal(states, best_path)
        self.assertAlmostEqual(logp, best, places=12)

    def test_the_fused_sweep_and_the_standalone_backward_agree(self):
        """The E-step folds the backward recursion into the posteriors sweep."""
        start, trans, frame = a_small_model(seed=11, n_samples=8)
        log_prob, fwd = run_forward(start, trans, frame)
        bwd = np.empty_like(frame)
        tttrlib.hmm_backward_log(trans, frame, bwd)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((frame.shape[1], frame.shape[1]))
        tttrlib.hmm_backward_posteriors_xi(
            trans, frame, fwd, log_prob, posteriors, xi_sum)
        expected = np.exp(fwd + bwd - log_prob)
        expected /= expected.sum(axis=1, keepdims=True)
        np.testing.assert_allclose(posteriors, expected, rtol=1e-10, atol=1e-12)


class TestMinusInfinityIsAValue(unittest.TestCase):
    """The cases a fast-math flag or a missing guard turns into nan."""

    def test_an_all_inf_frame_gives_minus_inf_and_no_nan(self):
        start, trans, frame = a_small_model(n_samples=6)
        frame[3, :] = -np.inf                      # no state can explain frame 3
        log_prob, fwd = run_forward(start, trans, frame)
        self.assertEqual(log_prob, -np.inf)
        self.assertFalse(np.isnan(fwd).any())

        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((frame.shape[1], frame.shape[1]))
        tttrlib.hmm_backward_posteriors_xi(
            trans, frame, fwd, log_prob, posteriors, xi_sum)
        self.assertFalse(np.isnan(posteriors).any())
        self.assertFalse(np.isnan(xi_sum).any())

    def test_an_impossible_sequence_contributes_no_transition_counts(self):
        """Zero, not "some finite number" and not nan.

        `xi_sum` is shared by every sequence in an E-step, so a single nan here
        is not a local wrong answer -- it is the whole transition matrix, for
        this iteration and every one after.
        """
        start, trans, frame = a_small_model(n_samples=6)
        frame[3, :] = -np.inf
        log_prob, fwd = run_forward(start, trans, frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((frame.shape[1], frame.shape[1]))
        tttrlib.hmm_backward_posteriors_xi(
            trans, frame, fwd, log_prob, posteriors, xi_sum)
        np.testing.assert_array_equal(xi_sum, np.zeros_like(xi_sum))

    def test_a_frame_no_state_explains_gets_a_uniform_posterior(self):
        start, trans, frame = a_small_model(n_samples=6, n_states=4)
        frame[2, :] = -np.inf
        log_prob, fwd = run_forward(start, trans, frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((frame.shape[1], frame.shape[1]))
        tttrlib.hmm_backward_posteriors_xi(
            trans, frame, fwd, log_prob, posteriors, xi_sum)
        np.testing.assert_allclose(posteriors[2], np.full(4, 0.25), rtol=0, atol=1e-15)

    def test_a_dead_state_stays_dead(self):
        """A state nothing can emit keeps a zero posterior, not a nan one."""
        start, trans, frame = a_small_model(n_samples=6, n_states=3)
        frame[:, 1] = -np.inf
        log_prob, fwd = run_forward(start, trans, frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((3, 3))
        tttrlib.hmm_backward_posteriors_xi(
            trans, frame, fwd, log_prob, posteriors, xi_sum)
        self.assertFalse(np.isnan(posteriors).any())
        np.testing.assert_array_equal(posteriors[:, 1], np.zeros(6))

    def test_logsumexp_returns_minus_inf_rather_than_nan(self):
        self.assertEqual(tttrlib.hmm_logsumexp(np.full(4, -np.inf)), -np.inf)


class TestParityWithTheImplementationItReplaces(unittest.TestCase):
    """Recorded from ChiSurf's numba kernels before they are deleted."""

    @classmethod
    def setUpClass(cls):
        if not FIXTURE.exists():
            raise AssertionError(f"missing parity fixture: {FIXTURE}")
        cls.d = np.load(FIXTURE)
        cls.names = [str(n) for n in cls.d["names"]]

    def test_every_recorded_case_matches(self):
        for i, name in enumerate(self.names):
            with self.subTest(case=name):
                d = self.d
                start = d["log_startprob_%d" % i]
                trans = d["log_transmat_%d" % i]
                frame = d["log_frameprob_%d" % i]

                fwd = np.empty_like(frame)
                log_prob = tttrlib.hmm_forward_log(start, trans, frame, fwd)
                np.testing.assert_allclose(fwd, d["fwd_%d" % i], rtol=1e-12, atol=0)
                self.assertEqual(np.isneginf(log_prob), bool(np.isneginf(d["log_prob_%d" % i])))
                if not np.isneginf(log_prob):
                    self.assertAlmostEqual(log_prob, float(d["log_prob_%d" % i]), places=10)

                bwd = np.empty_like(frame)
                tttrlib.hmm_backward_log(trans, frame, bwd)
                np.testing.assert_allclose(bwd, d["bwd_%d" % i], rtol=1e-12, atol=0)

                posteriors = np.empty_like(frame)
                xi_sum = np.zeros_like(d["xi_sum_%d" % i])
                tttrlib.hmm_backward_posteriors_xi(
                    trans, frame, fwd, log_prob, posteriors, xi_sum)
                np.testing.assert_allclose(
                    posteriors, d["posteriors_%d" % i], rtol=1e-10, atol=1e-14)
                np.testing.assert_allclose(
                    xi_sum, d["xi_sum_%d" % i], rtol=1e-10, atol=1e-14)
                self.assertFalse(np.isnan(posteriors).any())
                self.assertFalse(np.isnan(xi_sum).any())

                states = np.empty(frame.shape[0], dtype=np.int64)
                logp = tttrlib.hmm_viterbi_log(start, trans, frame, states)
                recorded = float(d["viterbi_logprob_%d" % i])
                if np.isneginf(recorded):
                    # Once the sequence is impossible every candidate path
                    # scores -inf, so the arg-max is decided entirely by the
                    # tie-break and the path carries no information. Asserting
                    # it would pin a convention, not a result -- hmmlearn picks
                    # a different path here and is not wrong. The
                    # log-probability is the part both agree on.
                    self.assertTrue(np.isneginf(logp))
                    self.assertTrue(((states >= 0) & (states < frame.shape[1])).all())
                else:
                    np.testing.assert_array_equal(states, d["states_%d" % i])
                    self.assertAlmostEqual(logp, recorded, places=10)


class TestTheMultiSequenceEntryPoint(unittest.TestCase):
    """One call per EM sweep, not one per sequence."""

    def test_it_equals_looping_over_the_sequences(self):
        parts = [a_small_model(seed=s, n_samples=n)[2] for s, n in ((1, 5), (2, 9), (3, 4))]
        start, trans, _ = a_small_model()
        frame = np.ascontiguousarray(np.vstack(parts))
        lengths = np.asarray([p.shape[0] for p in parts], dtype=np.int64)

        fwd = np.empty_like(frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((2, 2))
        per_seq = np.empty(len(parts))
        total = tttrlib.hmm_estep_log(
            start, trans, frame, lengths, fwd, posteriors, xi_sum, per_seq)

        expected_total = 0.0
        expected_xi = np.zeros((2, 2))
        for s, part in enumerate(parts):
            f = np.empty_like(part)
            lp = tttrlib.hmm_forward_log(start, trans, part, f)
            p = np.empty_like(part)
            tttrlib.hmm_backward_posteriors_xi(trans, part, f, lp, p, expected_xi)
            expected_total += lp
            self.assertAlmostEqual(per_seq[s], lp, places=12)
        self.assertAlmostEqual(total, expected_total, places=10)
        np.testing.assert_allclose(xi_sum, expected_xi, rtol=1e-12, atol=0)

    def test_a_single_sample_sequence_adds_no_transition_counts(self):
        start, trans, frame = a_small_model(n_samples=1)
        fwd = np.empty_like(frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((2, 2))
        no_lengths = np.empty(0, dtype=np.int64)     # "one sequence"
        no_report = np.empty(0)                      # "do not report per-sequence"
        tttrlib.hmm_estep_log(
            start, trans, frame, no_lengths, fwd, posteriors, xi_sum, no_report)
        np.testing.assert_array_equal(xi_sum, np.zeros((2, 2)))

    def test_the_lengths_must_account_for_every_row(self):
        start, trans, frame = a_small_model(n_samples=6)
        fwd = np.empty_like(frame)
        posteriors = np.empty_like(frame)
        xi_sum = np.zeros((2, 2))
        # ValueError, not RuntimeError: the shape checks throw
        # std::invalid_argument, which SWIG's std_except maps to ValueError
        # before this module's catch-all sees it.
        with self.assertRaises(ValueError):
            tttrlib.hmm_estep_log(
                start, trans, frame, np.asarray([2, 2], dtype=np.int64),
                fwd, posteriors, xi_sum, np.empty(0))


class TestTheShapeChecks(unittest.TestCase):
    """A transposed frame matrix reads as garbage numbers if nothing catches it."""

    def test_a_transposed_frame_matrix_is_rejected(self):
        start, trans, frame = a_small_model(n_samples=6, n_states=2)
        transposed = np.ascontiguousarray(frame.T)          # (K, T) instead of (T, K)
        fwd = np.empty_like(transposed)
        with self.assertRaises(ValueError):
            tttrlib.hmm_forward_log(start, trans, transposed, fwd)


if __name__ == "__main__":
    unittest.main()
