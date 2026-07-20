// SPDX-License-Identifier: BSD-3-Clause
%{
#include "BVA.h"
%}

// std::vector<int>, std::vector<std::pair<int,int>>, std::vector<int64_t> and
// std::pair<std::vector<double>,std::vector<double>> templates come from
// misc_types.i (VectorInt32, VectorPairInt, VectorInt64T, PairVectorDouble).

// Burst boundaries arrive as a NumPy int array (Python) / numeric vector (R) /
// long[] (Java) — one buffer conversion, no per-element list boxing. Also used
// by H2MM.i (included right after this file).
%apply (long long* IN_ARRAY2, int DIM1, int DIM2) {(long long* bursts, int n_bursts, int n_cols)};

// Release the GIL around the burst loop (embarrassingly parallel, OpenMP).
TTTRLIB_NOGIL(tttrlib::BVA::compute)

#ifdef SWIGPYTHON
// Array-out consistency: the static BVA line comes back as a
// (mean, std) tuple of NumPy float64 arrays.
%feature("pythonappend") tttrlib::BVA::compute_static_bva_line %{
    import numpy as _np
    val = (_np.asarray(val[0], dtype=_np.float64),
           _np.asarray(val[1], dtype=_np.float64))
%}
#endif

%include "BVA.h"

#ifdef SWIGPYTHON
%extend tttrlib::BVA {
    %pythoncode %{
    @property
    def proximity_ratio_mean(self):
        """Per-burst mean proximity ratio as a NumPy array."""
        import numpy as np
        return np.asarray(self.get_proximity_ratio_mean(), dtype=float)

    @property
    def proximity_ratio_std(self):
        """Per-burst proximity-ratio standard deviation as a NumPy array."""
        import numpy as np
        return np.asarray(self.get_proximity_ratio_std(), dtype=float)
    %}
}
#endif
