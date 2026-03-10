// CLSMISM SWIG interface (embedded into the main tttrlib module)
%include "misc_types.i"

%{
#include "CLSMISM.h"
%}

// Ignore pointer-returning overloads; only expose the array+dims variants
%ignore CLSMISM::apr_reconstruction(const double*, int, int, int, bool, int, int, double, int, int);
%ignore CLSMISM::focus_reconstruction(const double*, int, int, int, bool, double, double, int, bool, int, int, const double*, int);

// Provide NumPy typemap for optional detector coordinates (n x 2)
%apply(double* IN_ARRAY1, int DIM1) {(const double* detector_coords, int detector_coords_len)};

// Rename the array-based APIs to private native entry points used by the Python shim
%rename(_native_apr_reconstruction) CLSMISM::apr_reconstruction(const double* data, int dim0, int dim1, int dim2, bool channels_last, double** output, int* out_dim1, int* out_dim2, int* out_dim3, int usf, int ref_idx, double filter_sigma, int nz, int n_det);
%rename(_native_focus_reconstruction) CLSMISM::focus_reconstruction(const double* data, int dim0, int dim1, int dim2, bool channels_last, double** output, int* out_dim1, int* out_dim2, int* out_dim3, double sigma_bound, double threshold, int calibration_size, bool parallelize, int nz, int n_det, const double* detector_coords, int detector_coords_len);

%include "CLSMISM.h"

%pythoncode "CLSMISM.py"
