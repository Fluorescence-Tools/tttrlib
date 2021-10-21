%{
#include "DecayFit24.h"
%}

%extend DecayFit24{

    static double my_fit(double* x, int n_x, short* fixed, int n_fixed, MParam* p){
        if (n_x != 8) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 8"
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        if (n_fixed < 5) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the vector fixed be at least of length 6"
                         "Arrays of lengths (%d) given",
                         n_fixed);
            return 0.0;
        }
        return DecayFit24::fit(x, fixed, p);
    }

    static double my_targetf(double* x, int n_x, MParam* p){
        if (n_x != 8) {
            PyErr_Format(PyExc_ValueError,
                         "The length of the parameter vector must of length 8. "
                         "Arrays of length (%d) given",
                         n_x);
            return 0.0;
        }
        return DecayFit24::targetf(x, p);
    }

    static int my_modelf(
        double* param,int n_param,
        double* irf,int n_irf,
        double* bg,int n_bg,
        double dt,
        double* corrections,int n_corrections,
        double* mfunction, int n_mfunction
    ){
        if (n_irf != n_bg) {
            PyErr_Format(PyExc_ValueError,
                         "IRF and Bg array should have same length. "
                         "Arrays of lengths (%d,%d) given",
                         n_irf, n_bg);
            return 0.0;
        }
        if (n_mfunction != n_bg) {
            PyErr_Format(PyExc_ValueError,
                         "Output array should be of length inputs. "
                         "Arrays of lengths (%d,%d) given",
                         n_mfunction, n_bg);
            return 0.0;
        }
        if (n_param != 5) {
            PyErr_Format(PyExc_ValueError,
                         "Parameter array should be of length 5. "
                         "Arrays of length (%d) given",
                         n_param);
            return 0.0;
        }
        if (n_corrections != 5) {
            PyErr_Format(PyExc_ValueError,
                         "Corrections array should be of length 5. "
                         "Arrays of length (%d) given",
                         n_param);
            return 0.0;
        }
        return DecayFit24::modelf(param, irf, bg, n_mfunction / 2, dt, corrections, mfunction);
    }

}

%include "DecayFit24.h"
%pythoncode "../ext/python/DecayFit24.py"


