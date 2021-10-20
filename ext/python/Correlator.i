%{
#include "../include/TTTR.h"
#include "../include/CorrelatorPhotonStream.h"
#include "../include/CorrelatorCurve.h"
#include "../include/Correlator.h"
%}

%apply (unsigned long long* IN_ARRAY1, int DIM1) {(unsigned long long *t1, int n_t1),(unsigned long long *t2, int n_t2)}
%apply (double* IN_ARRAY1, int DIM1) {(double* weight_ch1, int n_weights_ch1)}
%apply (double* IN_ARRAY1, int DIM1) {(double* weight_ch2, int n_weights_ch2)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short* tac_1, int n_tac_1)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short* tac_2, int n_tac_2)}
%apply (unsigned short* IN_ARRAY1, int DIM1) {(unsigned short *tac, int n_tac)}

%attribute(CorrelatorCurve, int, n_bins, get_n_bins, set_n_bins);
%attribute(CorrelatorCurve, int, n_casc, get_n_casc, set_n_casc);

%attribute(Correlator, int, n_bins, get_n_bins, set_n_bins);
%attribute(Correlator, int, n_casc, get_n_casc, set_n_casc);
%attribute(Correlator, CorrelatorCurve*, curve, get_curve);
%attributestring(Correlator, std::string, method, get_correlation_method, set_correlation_method);

// Templates
%template(PairVectorDouble) std::pair<std::shared_ptr<TTTR>, std::shared_ptr<TTTR>>;

%include "../include/CorrelatorPhotonStream.h"
%include "../include/CorrelatorCurve.h"
%include "../include/Correlator.h"

%extend Correlator{
    %pythoncode "../ext/python/Correlator_ext.py"
}
%extend CorrelatorCurve{
        %pythoncode "../ext/python/CorrelatorCurve_ext.py"
}
