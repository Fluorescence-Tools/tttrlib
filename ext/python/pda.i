%module tttrlib
%{
    #include "../include/pda.h"
%}

//// internal
%attribute(Pda, bool, hist_sgsr_valid, is_valid_sgsr, set_valid_sgsr);

// 1D histogram
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double **histogram_x, int *n_histogram_x)}
%apply (double** ARGOUTVIEW_ARRAY1, int* DIM1) {(double **histogram_y, int *n_histogram_y)}


// Pda Model attributes
%attribute(Pda, double, background_ch2, get_ch2_background, set_ch2_background);
%attribute(Pda, double, background_ch1, get_ch1_background, set_ch1_background);

// 2d histogram attributes
%attribute(Pda, unsigned int, hist2d_nmin, get_min_number_of_photons, set_min_number_of_photons);
%attribute(Pda, unsigned int, hist2d_nmax, get_max_number_of_photons, set_max_number_of_photons);
%attribute(Pda, bool, hist2d_valid, is_valid_sgsr, set_valid_sgsr);

// Used for PdaCallback
// see https://github.com/swig/swig/tree/master/Examples/python/callback
%feature("director") PdaCallback;
%extend Pda{%pythoncode "./ext/python/pda/pda_extension.py"}
%include "../include/pda.h"
