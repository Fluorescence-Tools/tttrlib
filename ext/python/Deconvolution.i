// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Deconvolution.h"
%}

// Images and point spread functions arrive as NumPy arrays; the 2-D and 3-D
// entry points are separate because a typemap cannot be rank-polymorphic.
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(double* input, int n_input1, int n_input2)}
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(double* psf, int n_psf1, int n_psf2)}
%apply(double* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(double* input, int n_input1, int n_input2, int n_input3)}
%apply(double* IN_ARRAY3, int DIM1, int DIM2, int DIM3) {(double* psf, int n_psf1, int n_psf2, int n_psf3)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_output1, int* n_output2)}
%apply(double** ARGOUTVIEWM_ARRAY3, int* DIM1, int* DIM2, int* DIM3) {(double** output, int* n_output1, int* n_output2, int* n_output3)}
// The event-mode entry point returns onto a grid the caller sizes, so its
// output dimensions are inputs and cannot reuse the names above.
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** output, int* n_out1, int* n_out2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** output, int* n_output)}

// Every one of these is a long FFT loop that touches no Python object.
TTTRLIB_NOGIL(tttrlib::richardson_lucy_2d)
TTTRLIB_NOGIL(tttrlib::richardson_lucy_3d)
TTTRLIB_NOGIL(tttrlib::wiener_deconvolve_2d)
TTTRLIB_NOGIL(tttrlib::richardson_lucy_events_2d)

%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// The std::vector overloads are the C++ surface; the bindings expose the flat
// entry points, which carry the NumPy typemaps.
%ignore tttrlib::richardson_lucy;
%ignore tttrlib::wiener_deconvolve;
%ignore tttrlib::richardson_lucy_events;
%ignore tttrlib::scan_blur_kernel;

%include "Deconvolution.h"

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
