// SPDX-License-Identifier: BSD-3-Clause
%{
#include "H2MM.h"
#include "Channel.h"
#include "BurstFilter.h"
%}

// Nested vectors for the per-burst input arrays (not provided by misc_types.i).
%template(VectorVectorInt64) std::vector<std::vector<long long>>;
%template(VectorVectorInt32) std::vector<std::vector<int>>;
// Stream definitions for set_bursts_from_tttr / _from_filter.
%template(VectorChannelPtr) std::vector<std::shared_ptr<tttrlib::Channel>>;

// Viterbi path -> NumPy array + ICL scalar out-argument.
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **output, int *n_output)};
%apply double *OUTPUT { double *icl };

// Release the GIL around the heavy EM / Viterbi kernels (OpenMP over bursts).
TTTRLIB_NOGIL(tttrlib::H2MM::optimize)
TTTRLIB_NOGIL(tttrlib::H2MM::viterbi)
TTTRLIB_NOGIL(tttrlib::H2MM::fit)

#ifdef SWIGPYTHON
// Array-out consistency: unique inter-photon dt values as a NumPy int64 array.
%feature("pythonappend") tttrlib::H2MM::get_unique_dt %{
    import numpy as _np
    val = _np.asarray(val, dtype=_np.int64)
%}
#endif

%include "H2MM.h"

#ifdef SWIGPYTHON
%extend tttrlib::H2mmModel {
    %pythoncode %{
    @property
    def n_states_(self):
        return self.n_states()

    @property
    def n_streams_(self):
        return self.n_streams()

    @property
    def prior_np(self):
        import numpy as np
        return np.asarray(self.prior, dtype=float)

    @property
    def trans_np(self):
        import numpy as np
        n = self.n_states()
        return np.asarray(self.trans, dtype=float).reshape(n, n)

    @property
    def obs_np(self):
        import numpy as np
        n, p = self.n_states(), self.n_streams()
        return np.asarray(self.obs, dtype=float).reshape(n, p)

    def to_dict(self):
        return {
            "prior": self.prior_np,
            "trans": self.trans_np,
            "obs": self.obs_np,
            "loglik": self.loglik,
            "n_iter": self.n_iter,
            "n_phot": self.n_phot,
            "converged": self.converged,
            "bic": self.bic(),
        }
    %}
}

%extend tttrlib::H2MM {
    %pythoncode %{
    def viterbi_path(self, model):
        """Return (path, icl): per-photon Viterbi state path and ICL score."""
        import numpy as np
        path, icl = self.viterbi(model)
        return np.asarray(path, dtype=np.int64), float(icl)
    %}
}
#endif
