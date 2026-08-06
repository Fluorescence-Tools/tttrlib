// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "Pda.h"
#include "PdaCallback.h"
#include "PdaBurstLikelihood.h"
%}

// Forward declare and expose the enum
enum PdaImplementation {
    PDA_DEFAULT,
    PDA_OPTIMIZED
};

//// internal
%attribute(Pda, bool, hist_sgsr_valid, is_valid_sgsr, set_valid_sgsr);

// Every buffer below is freshly malloc'd/calloc'd by the C++ side and handed
// over to Python, so the typemaps must be the MANAGED ones (ARGOUTVIEWM):
// plain ARGOUTVIEW wraps the pointer in a numpy array that never frees it,
// which leaks the whole buffer on every call -- and get_1dhistogram is called
// once per iteration of a fit.

// 1D histogram
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (double **histogram_x, int *n_histogram_x),
    (double **histogram_y, int *n_histogram_y)
}

// output of make_s1s2 //
// the 2d matrix
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** s1s2, int* dim1, int* dim2)}
%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** ps, int* dim_ps)}
%apply(int** ARGOUTVIEWM_ARRAY1, int* DIM1) {(int** tttr_indices, int* n_tttr_indices)}
%apply(double* IN_ARRAY1, int DIM1) {(double* pF, int n_pF)}

// Pda Model attributes
%attribute(Pda, double, background_ch2, get_ch2_background, set_ch2_background);
%attribute(Pda, double, background_ch1, get_ch1_background, set_ch1_background);

// 2d histogram attributes
%attribute(Pda, unsigned int, hist2d_nmin, get_min_number_of_photons, set_min_number_of_photons);
%attribute(Pda, unsigned int, hist2d_nmax, get_max_number_of_photons, set_max_number_of_photons);
%attribute(Pda, bool, hist2d_valid, is_valid_sgsr, set_valid_sgsr);
#ifdef SWIGPYTHON
%extend Pda{%pythoncode "./ext/python/Pda.py"}
#endif

// Used for PdaCallback
// see https://github.com/swig/swig/tree/master/Examples/python/callback
#ifdef SWIGPYTHON
%feature("director") PdaCallback;
#endif

// Release the GIL around the heavy model computations. NOT get_1dhistogram:
// it calls back into Python through the PdaCallback director.
TTTRLIB_NOGIL(Pda::evaluate)                          // src/Pda.cpp
TTTRLIB_NOGIL(Pda::compute_experimental_histograms)   // src/Pda.cpp

// PdaBurstLikelihood: the burst table in. Its (double* input, ...) arguments
// and every output already have their mapping from misc_types.i.
%apply (int* IN_ARRAY2, int DIM1, int DIM2) {(int* counts, int n_bursts, int n_channels)}

TTTRLIB_NOGIL(PdaBurstLikelihood::log_likelihood_grid)   // src/PdaBurstLikelihood.cpp
TTTRLIB_NOGIL(PdaBurstLikelihood::total_log_likelihood)  // src/PdaBurstLikelihood.cpp

%include "Pda.h"
%include "PdaCallback.h"
%include "PdaBurstLikelihood.h"
