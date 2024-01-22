%{
#include "DecayConvolution.h"
%}


// manually added instead of including header file as all other functions
%apply (double* INPLACE_ARRAY1, int DIM1) {
    (double* fit, int n_fit),
    (double *model, int n_model),
    (double *time_axis, int n_time_axis),
    (double *lifetime_spectrum, int n_lifetime_spectrum),
    (double *data, int n_data),
    (double* w_sq, int n_w_sq),
    (double* x, int n_x),
    (double* decay, int n_decay),
    (double* irf, int n_irf),
    (double* lamp, int n_lamp),
    (double* lampsh, int n_lampsh)
}

void add_pile_up_to_model(
        double* model, int n_model,
        double* decay, int n_decay,
        double repetition_rate,
        double instrument_dead_time,
        double measurement_time,
        std::string pile_up_model = "coates",
        int start = 0,
        int stop = -1
);


//// rescale
//////////////
%ignore rescale;
%rename (rescale) my_rescale;
%inline %{
double my_rescale(
        double* fit, int n_fit,
        double* decay, int n_decay,
        int start = 0,
        int stop = -1
){
    if (n_fit != n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_decay);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_decay;
    }
    if (start >= n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_decay);
    }
    if (stop > n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_decay);
    }
    double scale = 0.0;
    rescale(fit, decay, &scale, start, stop);
    return scale;
}
%}

//// rescale_w
////////////////
%ignore rescale_w;
%rename (rescale_w) my_rescale_w;
%inline %{
double my_rescale_w(
        double* fit, int n_fit,
        double* decay, int n_decay,
        double* w_sq, int n_w_sq,
        int start = 0,
        int stop = -1
){
    if (n_fit != n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_decay);
    }
    if (n_decay != n_w_sq) {
        PyErr_Format(PyExc_ValueError,
                     "Weight and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_decay, n_w_sq);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_decay;
    }
    if (start > n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_decay);
    }
    if (stop > n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_decay);
    }
    double scale = 0.0;
    rescale_w(fit, decay, w_sq, &scale, start, stop);
    return scale;
}
%}

/// rescale_w_bg
///////////////////
%ignore rescale_w_bg;
%rename (rescale_w_bg) my_rescale_w_bg;
%inline %{
double my_rescale_w_bg(
        double* fit, int n_fit,
        double* decay, int n_decay,
        double* w_sq, int n_w_sq,
        double bg,
        int start = 0,
        int stop = -1
){
    if (n_fit != n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_decay);
    }
    if (n_decay != n_w_sq) {
        PyErr_Format(PyExc_ValueError,
                     "Weight and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_decay, n_w_sq);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_decay;
    }
    if (start > n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_decay);
    }
    if (stop > n_decay) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_decay);
    }
    double scale = 0.0;
    rescale_w_bg(fit, decay, w_sq, bg, &scale, start, stop);
    return scale;
}
%}

//// fconv
///////////////////
%ignore fconv;
%rename (fconv) my_fconv;
%inline %{
void my_fconv(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* x, int n_x,
        int start = 0,
        int stop = -1,
        double dt = 1.0
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_irf;
    }
    if (start > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_irf);
    }
    if (stop > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_irf);
    }
    fconv(fit, x, irf, n_x / 2, start, stop, dt);
}
%}


//// fconv_avx
///////////////////
%ignore fconv_avx;
%rename (fconv_avx) my_fconv_avx;
%inline %{
void my_fconv_avx(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* x, int n_x,
        int start = 0,
        int stop = -1,
        double dt = 1.0
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_irf;
    }
    if (start > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_irf);
    }
    if (stop > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_irf);
    }
    fconv_avx(fit, x, irf, n_x / 2, start, stop, dt);
}
%}


//// fconv_per
///////////////////
%ignore fconv_per;
%rename (fconv_per) my_fconv_per;
%inline %{
void my_fconv_per(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* x, int n_x,
        double period,
        int start = 0,
        int stop = -1,
        double dt = 1.0
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_fit;
    }
    if (start > n_fit) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_fit);
    }
    if (stop > n_fit) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_fit);
    }
    fconv_per(fit, x, irf, n_x / 2, start, stop, n_irf, period, dt);
}
%}

//// fconv_per_avx
///////////////////
%ignore fconv_per_avx;
%rename (fconv_per_avx) my_fconv_per_avx;
%inline %{
void my_fconv_per_avx(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* x, int n_x,
        double period,
        int start = 0,
        int stop = -1,
        double dt = 1.0
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_irf;
    }
    if (start > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_irf);
    }
    if (stop > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_irf);
    }
    fconv_per_avx(fit, x, irf, n_x / 2, start, stop, n_irf, period, dt);
}
%}


//// fconv_per_cs
///////////////////
%ignore fconv_per_cs;
%rename (fconv_per_cs) my_fconv_per_cs;
%inline %{
void my_fconv_per_cs(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* x, int n_x,
        double period,
        int conv_stop = -1,
        int stop = -1,
        double dt = 1.0
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if(stop < 0){
        stop = n_irf - 1;
    }
    if(conv_stop < 0){
        conv_stop = n_irf;
    }
    if (stop > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_irf);
    }
    fconv_per_cs(fit, x, irf, n_x / 2, stop, n_irf, period, conv_stop, dt);
}
%}

//// fconv_ref
///////////////////
%ignore fconv_ref;
%rename (fconv_ref) my_fconv_ref;
%inline %{
void my_fconv_ref(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* x, int n_x,
        double tauref,
        int start = 0,
        int stop = -1,
        double dt = 1.0
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_irf;
    }
    if (start > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_irf);
    }
    if (stop > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_irf);
    }
    fconv_ref(fit, x, irf, n_x / 2, start, stop, tauref);
}
%}

//// sconv
///////////////////
%ignore sconv;
%rename (sconv) my_sconv;
%inline %{
void my_sconv(
        double* fit, int n_fit,
        double* irf, int n_irf,
        double* model, int n_model,
        int start = 0,
        int stop = -1
){
    if (n_fit != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and decay array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_fit, n_irf);
    }
    if (n_model != n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Model and fit array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_model, n_irf);
    }
    if(start < 0){
        PyErr_Format(PyExc_ValueError,
                     "Start index needs to be larger or equal to zero."
        );
    }
    if(stop < 0){
        stop = n_irf;
    }
    if (start > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Start index (%d) too large for array of lengths (%d).",
                     start, n_irf);
    }
    if (stop > n_irf) {
        PyErr_Format(PyExc_ValueError,
                     "Stop index (%d) too large for array of lengths (%d).",
                     stop, n_irf);
    }
    sconv(fit, model, irf, start, stop);
}
%}

//// shift_lamp
///////////////////
%ignore shift_lamp;
%rename (shift_lamp) my_shift_lamp;
%inline %{
void my_shift_lamp(
        double* lamp, int n_lamp,
        double* lampsh, int n_lampsh,
        double ts = 0.0,
        double out_value=0.0
){
    if (n_lamp != n_lampsh) {
        PyErr_Format(PyExc_ValueError,
                     "IRF and shifted IRF array should have same length. "
                     "Arrays of lengths (%d,%d) given",
                     n_lamp, n_lampsh);
    }
    shift_lamp(lampsh, lamp, ts, n_lamp, out_value);
}
%}



%include "DecayConvolution.h"
