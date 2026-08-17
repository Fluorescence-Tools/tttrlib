// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_HMMLATTICE_H
#define TTTRLIB_HMMLATTICE_H

// Validation: A/B-TESTED 2026-08-17 -- vs hmmlearn _hmmc (recorded; forward/backward/logprob/viterbi bit-identical,
//   posteriors 1e-14, xi 1e-13), scipy.special.logsumexp and a NumPy textbook
//   forward-backward. test/python/misc/test_math_ab_probabilistic.py.
//   Benchmarked vs hmmlearn _hmmc on T=200k K=4: 1.8x, identical (bench_sciref.py, check_sciref.py).
//   Register: okf/testing/math-kernel-validation.md

// HmmLattice.h -- the log-domain HMM recursions over a caller-supplied frame
// probability matrix: forward, a fused backward/posteriors/xi sweep, Viterbi,
// and a standalone backward kept for tests.
//
// Emissions are the caller's business. Everything here takes `log_frameprob`,
// the (n_samples x n_states) matrix of log b_j(x_t), and knows nothing about
// where it came from -- a Gaussian mixture, a Poisson rate, a lookup table.
// That is what makes it worth having once instead of once per model.
//
// ---------------------------------------------------------------------------
// This is not the photon-stream HMM, and merging them is not wanted
// ---------------------------------------------------------------------------
// `modules/spectroscopy/hmm` models a photon stream: per-burst, photon-indexed,
// with a transition matrix that depends on the inter-photon time, and discrete
// symbol emissions read from a table. Its recursion is *scaled* -- each row is
// normalised and the scale factors returned -- because alpha underflows within
// a few hundred photons and the sums get reused downstream.
//
// This one is a uniform-bin lattice in the log domain. Different algorithm,
// different numerics; the overlap is the name. The scaled per-burst recursion
// is right for photons and the log lattice is right for uniform bins.
//
// ---------------------------------------------------------------------------
// -inf is a value here, not an error -- do not compile this with fast math
// ---------------------------------------------------------------------------
// A structurally constrained model (a transition forbidden, a state that cannot
// emit a symbol) has whole -inf columns, and a frame no state can explain is
// -inf across. Those must propagate: an all--inf frame yields an -inf
// log-likelihood, never a nan.
//
// `-ffast-math` (LLVM's `nnan`/`ninf`) licenses the compiler to assume no
// operand is NaN or infinite, which folds away exactly the guards below --
// `vmax == -inf`, `maximum == -inf`, `total > 0`. The nan that then appears
// spreads through the M-step into every later EM iteration, *and* destroys the
// -inf log-likelihood that would have reported the problem. The consumer this
// was ported from picks its fast-math set by hand for that reason
// (`nsz, arcp, contract, afn, reassoc`, deliberately without `nnan`/`ninf`);
// the CMakeLists pins the same property on HmmLattice.cpp.
//
// ---------------------------------------------------------------------------
// Layout and ownership
// ---------------------------------------------------------------------------
// Everything is float64, row-major, and every output buffer is allocated by the
// caller. An EM fit runs these thousands of times over the same shapes, so
// allocating inside would be the dominant cost; `xi_sum` in particular is an
// *accumulator* the caller zeroes once per iteration and adds into across
// sequences, which an allocate-and-return signature cannot express.
//
// Multi-sequence: `log_frameprob` holds the sequences concatenated along time,
// and `lengths` gives their lengths. One call per EM sweep -- never one call
// per sequence, and certainly never one per time step.

#include <cstdint>
#include <vector>

namespace tttrlib {

/*!
 * \brief log(sum(exp(v))) computed without overflow; -inf in, -inf out.
 *
 * Exposed because it is the one place the -inf convention is decided, so it is
 * the one place a test can pin it.
 */
double hmm_logsumexp(double* values, int n_values);

/*!
 * \brief Fill the log forward lattice and return the log-likelihood.
 *
 * `fwd[t*K + i]` is log P(x_1..x_t, z_t = i). The return is
 * log P(x_1..x_T) = logsumexp over the last row.
 *
 * \param log_startprob  [K]
 * \param log_transmat   [K*K], row i to column j
 * \param log_frameprob  [T*K]
 * \param fwd            [T*K], written
 */
double hmm_forward_log(
        double* log_startprob, int n_startprob,
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        double* fwd, int n_fwd1, int n_fwd2);

/*!
 * \brief Fill the log backward lattice.
 *
 * `bwd[t*K + i]` is log P(x_{t+1}..x_T | z_t = i). The E-step does not call
 * this -- `hmm_backward_posteriors_xi` folds the same recursion into one sweep
 * and keeps two rows alive instead of the whole lattice. It exists so the fused
 * sweep can be checked against the textbook arrangement.
 */
void hmm_backward_log(
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        double* bwd, int n_bwd1, int n_bwd2);

/*!
 * \brief One backward sweep: state posteriors, and expected transition counts.
 *
 * The backward lattice, the posteriors and the transition counts all need the
 * same quantity log a_ij + log b_j(x_{t+1}) + beta_{t+1}(j). Computing and
 * exponentiating it once -- rather than three passes over the lattice -- is
 * what makes this E-step cheaper than the textbook arrangement.
 *
 * `xi_sum` is **accumulated into**, not overwritten: a fit sums the expected
 * counts over sequences. Zero it yourself before the first sequence.
 *
 * A frame no state can explain gets a *uniform* posterior. Normalising its -inf
 * entries would otherwise produce nan, and a dead state must stay dead rather
 * than become undefined.
 *
 * \param fwd        [T*K] from hmm_forward_log
 * \param log_prob   its return value
 * \param posteriors [T*K], written
 * \param xi_sum     [K*K], accumulated
 */
void hmm_backward_posteriors_xi(
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        double* fwd, int n_fwd1, int n_fwd2,
        double log_prob,
        double* posteriors, int n_post1, int n_post2,
        double* xi_sum, int n_xi1, int n_xi2);

/*!
 * \brief Most probable state path, and its log-probability.
 *
 * Ties go to the lowest state index, which is what a strict `>` comparison
 * against a running best gives. That is a contract, not an accident: a caller
 * comparing against another implementation sees a different path otherwise.
 */
double hmm_viterbi_log(
        double* log_startprob, int n_startprob,
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        long long* state_sequence, int n_state_sequence);

/*!
 * \brief A whole E-step over concatenated sequences, in one call.
 *
 * Runs forward and the fused backward sweep for each sequence in
 * `log_frameprob`, whose rows are the sequences laid end to end in the order
 * `lengths` gives. Returns the summed log-likelihood; if `log_prob_per_seq` is
 * non-null it also receives the per-sequence values.
 *
 * `lengths` may be null or empty, which means one sequence of `n_samples`; an
 * empty `log_prob_per_seq` means the per-sequence values are not wanted. A
 * zero-length array is how a binding whose array typemap cannot pass a null
 * pointer says either of those.
 *
 * This is the entry point a fit should use. Calling forward and backward
 * separately per sequence from Python is correct but pays the call overhead
 * once per sequence for no reason.
 */
double hmm_estep_log(
        double* log_startprob, int n_startprob,
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        long long* lengths, int n_lengths,
        double* fwd, int n_fwd1, int n_fwd2,
        double* posteriors, int n_post1, int n_post2,
        double* xi_sum, int n_xi1, int n_xi2,
        double* log_prob_per_seq, int n_log_prob_per_seq);

}  // namespace tttrlib

#endif  // TTTRLIB_HMMLATTICE_H
