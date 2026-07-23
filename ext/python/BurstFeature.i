// SPDX-License-Identifier: BSD-3-Clause
%{
#include "BurstFeature.h"
%}

// std::vector<int>, std::vector<std::pair<int,int>> and std::vector<double>
// templates come from misc_types.i (VectorInt32, VectorPairInt, VectorDouble).

// Base class for per-burst features (named streams + optional KDE machinery).
// The protected KDE/burst-dispatch helpers are not exposed; only set_stream,
// get_tttr and get_result are part of the public surface. Must be %included
// before its subclasses (BVA.i, TwoCDE.i) so SWIG knows the base type.
%include "BurstFeature.h"

#ifdef SWIGPYTHON
%extend tttrlib::BurstFeature {
    %pythoncode %{
    @property
    def result(self):
        """Per-burst feature result as a NumPy array (NaN where undefined)."""
        import numpy as np
        return np.asarray(self.get_result(), dtype=float)
    %}
}
#endif
