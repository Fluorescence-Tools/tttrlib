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
// std::vector<unsigned char> (H2mmStateSidecar::states / ::streams) is already
// instantiated as VectorUint8 by Sim.i, which tttrlib.i includes first.

// Viterbi / marginal-draw path -> NumPy array + scalar out-arguments.
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **output, int *n_output)};
%apply double *OUTPUT { double *icl };
%apply long long *OUTPUT { long long *n_underflow };

// γ -> (N, n_states) float32; FFBS draws -> (n_samples, N) int64.  Distinct
// parameter names, because the 1-D (T** output, int* n_output) mappings in
// misc_types.i / TTTRMask.i would otherwise claim the leading pair.
%apply(float** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
    (float** gamma_out, int* gamma_rows, int* gamma_cols)};
%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {
    (long long** paths_out, int* path_rows, int* path_cols)};

// Per-photon state / stream arrays over the source range.  These are freshly
// malloc'd (unlike TTTRMask::get_mask, which hands back a cached view), so they
// must take the *owning* ARGOUTVIEWM typemap.
%apply(unsigned char** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (unsigned char** states_out, int* n_states_out)};
%apply(unsigned char** ARGOUTVIEWM_ARRAY1, int* DIM1) {
    (unsigned char** streams_out, int* n_streams_out)};

// Decoder input paths come in as NumPy int64 arrays.
%apply(long long* IN_ARRAY1, int DIM1) {(const long long* path, int n_path)};

// H2mmStateSidecar::set_arrays -- per-photon uint8 arrays in from NumPy.
%apply(unsigned char* IN_ARRAY1, int DIM1) {
    (unsigned char* states_in, int n_states_in)};
%apply(unsigned char* IN_ARRAY1, int DIM1) {
    (unsigned char* streams_in, int n_streams_in)};

// Release the GIL around the heavy EM / decode kernels (parallel over bursts).
TTTRLIB_NOGIL(tttrlib::H2MM::optimize)
TTTRLIB_NOGIL(tttrlib::H2MM::viterbi)
TTTRLIB_NOGIL(tttrlib::H2MM::fit)
TTTRLIB_NOGIL(tttrlib::H2MM::posterior)
TTTRLIB_NOGIL(tttrlib::H2MM::sample_states)
TTTRLIB_NOGIL(tttrlib::H2MM::sample_paths)

#ifdef SWIGPYTHON
// Array-out consistency: unique inter-photon dt values as a NumPy int64 array.
%feature("pythonappend") tttrlib::H2MM::get_unique_dt %{
    import numpy as _np
    val = _np.asarray(val, dtype=_np.int64)
%}
#endif

// The channel-budget check, the missing-photon-index guards and the sidecar I/O
// all report by throwing.  Without a handler the exception unwinds through the
// wrapper and aborts the interpreter instead of raising.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

%include "H2MM.h"

%exception;   // scoped to this header only

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

%extend tttrlib::H2mmChannelMap {
    %pythoncode %{
    @property
    def channels_np(self):
        """Allocated routing-channel ids as an (n_streams, n_states) array."""
        import numpy as np
        return np.asarray(self.channels, dtype=np.int32).reshape(
            self.n_streams, self.n_states)

    @property
    def source_map(self):
        """{original source channel: compressed id} for the untouched photons."""
        return dict(zip(self.used_channels, self.compressed_channels))

    def to_dict(self):
        return {
            "n_streams": self.n_streams,
            "n_states": self.n_states,
            "max_channel": self.max_channel,
            "channels": self.channels_np,
            "used_channels": list(self.used_channels),
            "compressed_channels": list(self.compressed_channels),
            "highest_channel": self.highest_channel(),
        }
    %}
}

%extend tttrlib::H2mmStateSidecar {
    %pythoncode %{
    @property
    def states_np(self):
        """Per-photon state over the source range (255 = unassigned)."""
        import numpy as np
        return np.asarray(self.states, dtype=np.uint8)

    @property
    def streams_np(self):
        """Per-photon stream index over the source range (255 = unassigned)."""
        import numpy as np
        return np.asarray(self.streams, dtype=np.uint8)

    def indices_for_state(self, state):
        """Source photon indices assigned to `state`."""
        import numpy as np
        return np.asarray(self.mask_for_state(state).get_indices(), dtype=np.int64)
    %}
}

%extend tttrlib::H2MM {
    %pythoncode %{
    def viterbi_path(self, model):
        """Return (path, icl): per-photon Viterbi state path and ICL score."""
        import numpy as np
        path, icl = self.viterbi(model)
        return np.asarray(path, dtype=np.int64), float(icl)

    def gamma(self, model):
        """Per-photon posterior state probabilities.

        Returns
        -------
        gamma : ndarray, shape (n_photons, n_states), float32
            Rows sum to 1.  This is a *distribution*, not an assignment -- the
            answer to "how do the photons distribute over the states", which
            the Viterbi argmax reports winner-takes-all.
        n_underflow : int
            Photons whose forward scale underflowed; their rows are uniform.
        """
        import numpy as np
        g, n_underflow = self.posterior(model)
        return np.asarray(g, dtype=np.float32), int(n_underflow)

    def jitter_path(self, model, seed=0):
        """Draw each photon's state independently from its gamma row.

        Faithful per-photon marginal, but the draws are independent, so the
        path fragments: use :meth:`ffbs_paths` for dwell times or transition
        counts.

        Returns (path, n_underflow).
        """
        import numpy as np
        path, n_underflow = self.sample_states(model, seed)
        return np.asarray(path, dtype=np.int64), int(n_underflow)

    def ffbs_paths(self, model, seed=0, n_samples=1):
        """Draw whole trajectories from P(path | data) (forward filtering,
        backward sampling).

        Returns an (n_samples, n_photons) int64 array.  The per-photon marginal
        over many draws converges to gamma *and* the dwell statistics are valid.
        """
        import numpy as np
        return np.asarray(self.sample_paths(model, seed, n_samples), dtype=np.int64)

    def occupancy(self, model, path=None):
        """Fraction of photons in each state.

        With no `path`, returns the gamma column means -- the unbiased
        posterior occupancy.  With a decoded `path`, returns the counted
        fractions, which is what any hard assignment reports.
        """
        import numpy as np
        if path is None:
            g, _ = self.gamma(model)
            return g.mean(axis=0).astype(float)
        path = np.asarray(path, dtype=np.int64)
        n = model.n_states()
        return np.bincount(path, minlength=n)[:n] / max(len(path), 1)
    %}
}
#endif
