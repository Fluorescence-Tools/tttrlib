%{
#include "../include/decay.h"
#include "../include/statistics.h"
%}

// Input arrays
%apply(double* IN_ARRAY1, int DIM1) {
    (double* time_axis, int n_time_axis),
    (double* squared_weights, int n_weights),
    (double* instrument_response_function, int n_instrument_response_function),
    (double* lifetime_spectrum, int n_lifetime_spectrum),
    (double* data, int n_data),
    (double* curve1, int n_curve1),
    (double* curve2, int n_curve2),
    (double *lifetime_spectrum, int n_lifetime_spectrum)
}

%apply(short* IN_ARRAY1, int DIM1){
    (short* fixed, int n_fixed)
}

%apply(double** ARGOUTVIEWM_ARRAY1, int* DIM1){
    (double** histogram, int* n_histogram),
    (double** time, int* n_time)
};

// Input output arrays
%apply(double* INPLACE_ARRAY1, int DIM1) {
    (double* model_function, int n_model_function),
    (double* x, int n_x)
}

%include "../include/decay.h"

%extend Decay{
    %pythoncode "../ext/python/decay/decay_extension.py"
}

