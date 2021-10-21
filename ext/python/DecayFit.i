%{
#include "DecayFit.h"
%}

%include "DecayFit.h"

/* Convolution and LabView interface*/
%include "DecayLvArrays.i"

%rename (modelf) my_modelf;
%rename (targetf) my_targetf;
%rename (fit) my_fit;

// for fit23, fit24, etc.
%apply (double* INPLACE_ARRAY1, int DIM1) {
    (double* x, int n_x),
    (double* param, int n_param),
    (double* irf,int n_irf),
    (double* bg,int n_bg),
    (double* corrections,int n_corrections),
    (double* mfunction, int n_mfunction)
}
%apply (short* IN_ARRAY1, int DIM1) {
    (short* fixed, int n_fixed)
}

/* Fits */
%pythoncode "../ext/python/fit2x.py"
%include "DecayFit23.i"
%include "DecayFit24.i"
%include "DecayFit25.i"
%include "DecayFit26.i"

