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
%extend tttrlib::TwoCDE {
// A non-overloaded entry point for the burst-array form.
//
// SWIG's R overload dispatcher calls extends(class(arg), ...), and since R 4.0
// class(matrix) is TWO values -- c("matrix", "array") -- so extends() raises
// "'class1' must be the name of a class or a class definition" and NO overload
// can be selected. Any overloaded R function taking a matrix hits this.
//
// One signature and no default arguments means SWIG emits a single function
// and no dispatcher, so the matrix arrives. The overloaded compute() is
// unchanged and is what every other language keeps using.
    void compute_bursts(long long* bursts, int n_bursts, int n_cols,
                        double tau, int variant, int kernel) {
        $self->compute(bursts, n_bursts, n_cols, tau, variant, kernel);
    }
}


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
