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

%exception;
