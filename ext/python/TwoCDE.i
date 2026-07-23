// SPDX-License-Identifier: BSD-3-Clause
%{
#include "TwoCDE.h"
%}

// Burst boundaries arrive as a NumPy int array (Python) / numeric vector (R) /
// long[] (Java): one buffer conversion, no per-element list boxing. BVA.i
// applies the same typemap earlier; re-apply so this file is self-contained.
%apply (long long* IN_ARRAY2, int DIM1, int DIM2) {(long long* bursts, int n_bursts, int n_cols)};

// Release the GIL around the (parallel) KDE + burst reduction.
TTTRLIB_NOGIL(tttrlib::TwoCDE::compute)

%include "TwoCDE.h"

#ifdef SWIGPYTHON
%extend tttrlib::TwoCDE {
    %pythoncode %{
    @property
    def two_cde(self):
        """Per-burst 2CDE value as a NumPy array (NaN where undefined)."""
        import numpy as np
        return np.asarray(self.get_result(), dtype=float)
    %}
}
#endif
