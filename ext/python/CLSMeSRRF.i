/*
 * CLSMeSRRF.i
 *
 * SWIG interface for CLSMeSRRF (photon-level eSRRF)
 */

%{
#include "CLSMeSRRF.h"
%}

%include "CLSMeSRRF.h"

// Numpy typemaps for array outputs (already defined in misc_types.i)
// The ARGOUTVIEWM_ARRAY typemaps will handle the output arrays

%apply (double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
    (double** output, int* out_ny, int* out_nx)
}

%apply (int** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (int** out_frame, int* n_photons)
    (int** out_line, int* n_photons)
    (int** out_event_idx, int* n_photons)
}

%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (double** out_x_exact, int* n_photons)
    (double** out_y_line, int* n_photons)
}

%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (double** output, int* dummy)
}

// Experimental API marking
%pythoncode %{
import tttrlib

tttrlib.mark_experimental(
    CLSMeSRRF,
    "Photon-level eSRRF is experimental. API may change."
)
%}
