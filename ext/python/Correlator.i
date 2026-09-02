// SPDX-License-Identifier: BSD-3-Clause
%{
#include "TTTR.h"
#include "CorrelatorPhotonStream.h"
#include "CorrelatorCurve.h"
#include "Correlator.h"
%}

// CorrelatorCurve
%attribute(CorrelatorCurve, int, n_bins, get_n_bins, set_n_bins);
%attribute(CorrelatorCurve, int, n_casc, get_n_casc, set_n_casc);

// Correlator
%attribute(Correlator, int, n_bins, get_n_bins, set_n_bins);
%attribute(Correlator, int, n_casc, get_n_casc, set_n_casc);
%attributestring(Correlator, std::string, method, get_correlation_method, set_correlation_method);
%template(TTTRPair) std::pair<std::shared_ptr<TTTR>, std::shared_ptr<TTTR>>;

// CorrelatorPhotonStream
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *t1, int n_t1),(unsigned long long *t2, int n_t2)}
%apply (double* IN_ARRAY1, int DIM1) {(double* weight_ch1, int n_weights_ch1)}
%apply (double* IN_ARRAY1, int DIM1) {(double* weight_ch2, int n_weights_ch2)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short* tac_1, int n_tac_1)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short* tac_2, int n_tac_2)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short *tac, int n_tac)}

%include "CorrelatorPhotonStream.h"
%include "CorrelatorCurve.h"

// Release the Python GIL around the heavy correlation compute (internal C++
// vectors only; no Python objects touched).
TTTRLIB_NOGIL(Correlator::run)      // src/Correlator.cpp:84

// species_matrix_correlation: the shared macro-time stream and the full
// (n_species, n_photons) weight matrix go in once; the (n_pairs, n_lags)
// normalized-correlation matrix and its shared lag axis come back once.
%apply (unsigned long long* IN_ARRAY1, int DIM1) {(const unsigned long long *macro_times, int n_photons)}
%apply (double* IN_ARRAY2, int DIM1, int DIM2) {(const double *weights, int n_species, int n_weights_per_species)}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double **out_x_axis, int *out_n_lags)}
%apply (double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double **out_matrix, int *out_n_pairs, int *out_n_matrix_lags)}
TTTRLIB_NOGIL(Correlator::species_matrix_correlation)

%include "Correlator.h"

#ifdef SWIGPYTHON
%extend Correlator{
    %pythoncode "./ext/python/Correlator.py"
}
%extend CorrelatorCurve{
    %pythoncode "./ext/python/CorrelatorCurve.py"
}
#endif
