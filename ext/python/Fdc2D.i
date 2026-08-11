// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Fdc2D.h"
%}

// A photon stream is millions of elements, so every array crosses as a buffer.
// These typemap names are not Python-only: ext/r/rarrays.i, ext/java/jarrays.i
// and ext/js/jsarrays.i implement the same IN_ARRAY / INPLACE_ARRAY names
// against R vectors, Java arrays and JS TypedArrays, so one %apply serves all
// four bindings.
//
// The matrices are INPLACE rather than ARGOUTVIEWM on purpose: a lag scan is
// run repeatedly over the same shapes while a caller sweeps parameters, and
// (n_lags x L x L) allocated per call would put the allocator in the hot path
// of the thing being measured.
%apply (long long* IN_ARRAY1, int DIM1) {(long long* macro_times, int n_macro)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* micro_times, int n_micro)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* lags, int n_lags)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* logt_ticks, int n_ticks)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* ticks, int n_ticks)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* ticks_a, int n_ticks_a)}
%apply (long long* IN_ARRAY1, int DIM1) {(long long* ticks_b, int n_ticks_b)}
%apply (long long* INPLACE_ARRAY1, int DIM1) {(long long* out_a, int n_out_a)}
%apply (long long* INPLACE_ARRAY1, int DIM1) {(long long* out_b, int n_out_b)}
%apply (long long* INPLACE_ARRAY1, int DIM1) {(long long* out, int n_out)}

// One pass over the photons with a binary search per lag, touching no Python
// object -- the case that wants the GIL released.
TTTRLIB_NOGIL(tttrlib::fdc_scan_log)
TTTRLIB_NOGIL(tttrlib::fdc_scan_axis)
TTTRLIB_NOGIL(tttrlib::fdc_scan_two_axes)
TTTRLIB_NOGIL(tttrlib::fdc_log)

// Identical to the global handler this file restores at the bottom, and to
// TTTRLIB_NOGIL's. Without the `invalid_argument` branch the same class of
// mistake raised ValueError from `fdc_scan_log` (which is NOGIL-wrapped, so it
// picks up that mapping) and RuntimeError from `fdc_t_imax` (which is not) --
// one file, one kind of error, two Python types depending on which function the
// caller reached for.
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

%include "Fdc2D.h"

// NOT `%exception;`. A bare clear ends the handler installed above AND the
// global one from MicrotimeLinearization.i, which is included earlier -- and
// this file is included later still, so everything after it (CLSM.i,
// CLSMSuperRes.i, Localization.i, Tiff.i, Pda.i, the decay fits, Sim.i,
// Streaming.i ...) was left with no handler at all. A C++ throw there does not
// raise, it TERMINATES the interpreter:
//
//   libc++abi: terminating due to uncaught exception of type
//   std::invalid_argument: TAC2 needs at least two frames
//
// which aborted the whole test suite at CLSMSuperRes.temporal_combine. So the
// scoped handler is ended by restoring the global one rather than by clearing
// to nothing. Keep these two bodies identical.
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

%clear (long long* macro_times, int n_macro);
%clear (long long* micro_times, int n_micro);
%clear (long long* lags, int n_lags);
%clear (long long* logt_ticks, int n_ticks);
%clear (long long* ticks, int n_ticks);
%clear (long long* ticks_a, int n_ticks_a);
%clear (long long* ticks_b, int n_ticks_b);
%clear (long long* out_a, int n_out_a);
%clear (long long* out_b, int n_out_b);
%clear (long long* out, int n_out);
