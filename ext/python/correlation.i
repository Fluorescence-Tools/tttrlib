/****************************************************************************
 * Copyright (C) 2020 by Thomas-Otavio Peulen                               *
 *                                                                          *
 * This file is part of the library tttrlib.                                *
 *                                                                          *
 *   tttrlib is free software: you can redistribute it and/or modify it     *
 *   under the terms of the MIT License.                                    *
 *                                                                          *
 *   tttrlib is distributed in the hope that it will be useful,             *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                   *
 *                                                                          *
 ****************************************************************************/

%module tttrlib
%{
    #include "../include/correlation.h"
%}

%apply (unsigned long long* IN_ARRAY1, int DIM1) {
    (unsigned long long *t1, int n_t1),
    (unsigned long long *t2, int n_t2)
}
%apply (unsigned long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (unsigned long long** x_axis, int* n_out)
}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (double** corr, int* n_out)
}
%apply (double* IN_ARRAY1, int DIM1) {
    (double* weight_ch1, int n_weights_ch1),
    (double* weight_ch2, int n_weights_ch2)
}

%include attribute.i
%attribute(Correlator, int, n_bins, get_n_bins, set_n_bins);
%attribute(Correlator, int, n_casc, get_n_casc, set_n_casc);

%include "../include/correlation.h"

%extend Correlator{
    %pythoncode "../ext/python/correlator_extension.py"
}
