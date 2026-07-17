// SPDX-License-Identifier: BSD-3-Clause
%{
#include "BVA.h"
%}

// std::vector<int>, std::vector<std::pair<int,int>>, std::vector<int64_t> and
// std::pair<std::vector<double>,std::vector<double>> templates come from
// misc_types.i (VectorInt32, VectorPairInt, VectorInt64T, PairVectorDouble).

// Release the GIL around the burst loop (embarrassingly parallel, OpenMP).
TTTRLIB_NOGIL(tttrlib::BVA::compute)

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
