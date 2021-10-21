%{
#include "DecayFit26.h"
%}

%extend DecayFit26{

    static double my_targetf(double* x, int n_x, MParam* p){
        if (n_x != 8) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 8. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        return DecayFit26::targetf(x, p);
    }

    static double my_fit(double* x, int n_x, short* fixed, int n_fixed, MParam* p){
        if (n_x != 1) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 1. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        if (n_fixed < 1) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the vector fixed be at least of length 1. "
                         "Array of lengths (%d) given",
                         n_fixed);
            return 0.0;
        }
        return DecayFit26::fit(x, fixed, p);
    }
};

%include "DecayFit26.h"
%pythoncode "../ext/python/DecayFit26.py"
