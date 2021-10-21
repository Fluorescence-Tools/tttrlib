%{
#include "DecayFit25.h"
%}

%extend DecayFit25{

    static double my_targetf(double* x, int n_x, MParam* p){
        if (n_x != 8) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 8. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        return DecayFit25::targetf(x, p);
    }

    static double my_fit(double* x, int n_x, short* fixed, int n_fixed, MParam* p){
        if (n_x != 9) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 9. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        if (n_fixed < 5) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the vector fixed be at least of length 5. "
                         "Array of lengths (%d) given",
                         n_fixed);
            return 0.0;
        }
        return DecayFit25::fit(x, fixed, p);
    }
}
%include "DecayFit25.h"
%pythoncode "../ext/python/DecayFit25.py"

