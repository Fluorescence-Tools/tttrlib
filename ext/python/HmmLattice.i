// SPDX-License-Identifier: BSD-3-Clause
%{
#include "HmmLattice.h"
%}

// Every array here crosses as a NumPy buffer, never as `VectorDouble`.
//
// The default `std::vector<double>` typemaps convert through the Python
// sequence protocol -- one boxed float per element, ~50 ns each. A binned-trace
// E-step hands over a (100000 x 6) frame matrix, which is ~30 ms of conversion
// against a ~27 ms kernel: the wrapper would cost more than the algorithm and
// the delegation would be a loss. See okf/bindings/marshalling-cost.md.
//
// The output buffers are INPLACE, not ARGOUTVIEWM, and that is deliberate:
//   * `xi_sum` is an *accumulator* summed across sequences and across calls, so
//     a function that allocated and returned it could not express what the
//     caller does with it;
//   * an EM fit runs these thousands of times over identical shapes, and
//     allocating a (T x K) lattice per call would put the allocator in the hot
//     path of the thing being optimised.
%apply (double* IN_ARRAY1, int DIM1) {(double* log_startprob, int n_startprob)}
%apply (double* IN_ARRAY1, int DIM1) {(double* values, int n_values)}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(double* log_transmat, int n_trans1, int n_trans2)}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(double* log_frameprob, int n_samples, int n_states)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* lengths, int n_lengths)}

%apply (double* INPLACE_ARRAY2, int DIM1, int DIM2) {(double* fwd, int n_fwd1, int n_fwd2)}
%apply (double* INPLACE_ARRAY2, int DIM1, int DIM2) {(double* bwd, int n_bwd1, int n_bwd2)}
%apply (double* INPLACE_ARRAY2, int DIM1, int DIM2) {(double* posteriors, int n_post1, int n_post2)}
%apply (double* INPLACE_ARRAY2, int DIM1, int DIM2) {(double* xi_sum, int n_xi1, int n_xi2)}
%apply (double* INPLACE_ARRAY1, int DIM1) {(double* log_prob_per_seq, int n_log_prob_per_seq)}
%apply (long long* INPLACE_ARRAY1, int DIM1) {(long long* state_sequence, int n_state_sequence)}

// Serial recursions over the whole lattice that touch no Python object. An EM
// fit over several sequences is the case that wants the GIL released.
TTTRLIB_NOGIL(tttrlib::hmm_forward_log)
TTTRLIB_NOGIL(tttrlib::hmm_backward_log)
TTTRLIB_NOGIL(tttrlib::hmm_backward_posteriors_xi)
TTTRLIB_NOGIL(tttrlib::hmm_viterbi_log)
TTTRLIB_NOGIL(tttrlib::hmm_estep_log)

// The shape checks throw std::invalid_argument. Without this they would cross
// as a C++ exception through the wrapper and abort the interpreter.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "HmmLattice.h"

// Restore the global handler rather than clearing it. A bare `%exception;`
// resets to NOTHING -- not to whatever was in force before, which is what the
// old comment here claimed -- so it disarms every interface included after
// this one, and a C++ throw from any of them terminates the interpreter
// instead of raising (BUGS 2026-08-11: a bare clear in Fdc2D.i aborted the
// whole test suite at CLSMSuperRes.temporal_combine). These files are safe
// today only because they happen to precede MicrotimeLinearization.i, which
// reinstalls it; that is include order, not design. Keep this body identical
// to MicrotimeLinearization.i's.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "Unknown exception");
    }
}

// Clear the maps, so a later interface declaring a `double* fwd` of its own
// does not silently inherit a NumPy typemap from here.
%clear (double* log_startprob, int n_startprob);
%clear (double* values, int n_values);
%clear (double* log_transmat, int n_trans1, int n_trans2);
%clear (double* log_frameprob, int n_samples, int n_states);
%clear (long long* lengths, int n_lengths);
%clear (double* fwd, int n_fwd1, int n_fwd2);
%clear (double* bwd, int n_bwd1, int n_bwd2);
%clear (double* posteriors, int n_post1, int n_post2);
%clear (double* xi_sum, int n_xi1, int n_xi2);
%clear (double* log_prob_per_seq, int n_log_prob_per_seq);
%clear (long long* state_sequence, int n_state_sequence);
