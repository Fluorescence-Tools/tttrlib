// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Jitter.h"
%}

%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(double* input, int n_input1, int n_input2)}
%apply(double* IN_ARRAY1, int DIM1) {(double* widths, int n_widths)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_output1, int* n_output2)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_out1, int* n_out2)}

TTTRLIB_NOGIL(tttrlib::jitter_coordinates_2d)
TTTRLIB_NOGIL(tttrlib::events_from_counts_2d)
TTTRLIB_NOGIL(tttrlib::counts_from_events_2d)

%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// The std::vector overloads are the C++ surface; the bindings expose the flat
// entry points, which carry the NumPy typemaps.
%ignore tttrlib::jitter_coordinates;
%ignore tttrlib::events_from_counts;
%ignore tttrlib::counts_from_events;

%include "Jitter.h"

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
