// SPDX-License-Identifier: BSD-3-Clause
// CLSMSuperRes SWIG interface (embedded into the main tttrlib module)
%include "misc_types.i"

%{
#include "CLSMSuperRes.h"
%}

// Ignore pointer-returning overloads; only expose the array+dims variants
// (the Python shim calls the _native_ entry points and reshapes output)
%ignore CLSMSuperRes::rgc_map(const double*, int, int, int, double, int, bool);
%ignore CLSMSuperRes::temporal_combine(const double*, int, int, int, const char*);

// The global typemaps in misc_types.i already bind ARGOUTVIEWM_ARRAY patterns to
// the canonical parameter names (output, dim1, dim2, ...). Rename our native
// methods so those names match the global %apply in misc_types.i.

%rename(_native_rgc_map) CLSMSuperRes::rgc_map(
    const double* img, int nx, int ny, int magnification, double fwhm,
    int sensitivity, bool intensity_weighting,
    double** output, int* dim1, int* dim2);

%rename(_native_temporal_combine) CLSMSuperRes::temporal_combine(
    const double* stack, int n_frames, int ny, int nx, const char* mode,
    double** output);

// get_photon_positions: multiple output arrays, each (T** output, int* n_output)
// The global typemaps in misc_types.i handle (int** output, int* n_output) and
// (double** output, int* n_output). No rename needed if parameter names match.

%include "CLSMSuperRes.h"

#ifdef SWIGPYTHON
%pythoncode %{
import tttrlib

tttrlib.mark_experimental(
    CLSMSuperRes,
    "Photon-level super-resolution (eSRRF) is experimental. API may change."
)
%}
#endif
