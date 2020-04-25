%{
#include "../include/decay.h"
%}

// Input arrays
%apply(double* IN_ARRAY1, int DIM1) {
    (double* time_axis, int n_time_axis),
    (double* weights, int n_weights),
    (double* instrument_response_function, int n_instrument_response_function),
    (double* lifetime_spectrum, int n_lifetime_spectrum),
    (double* data, int n_data),
    (double* curve1, int n_curve1),
    (double* curve2, int n_curve2)
}

// Input output arrays
%apply(double* INPLACE_ARRAY1, int DIM1) {
    (double* model_function, int n_model_function)
}

%include "../include/decay.h"
