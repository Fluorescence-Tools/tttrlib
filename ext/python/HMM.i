// SPDX-License-Identifier: BSD-3-Clause
%{
#include "HMM.h"
#include "HMMEmission.h"
#include "HMMBayes.h"
#include "Channel.h"
#include "BurstFilter.h"
%}

// Nested vectors for the per-burst input arrays (not provided by misc_types.i).
%template(VectorVectorInt64) std::vector<std::vector<long long>>;
%template(VectorVectorInt32) std::vector<std::vector<int>>;
// Stream definitions for set_bursts_from_tttr / _from_filter.
%template(VectorChannelPtr) std::vector<std::shared_ptr<tttrlib::Channel>>;
// std::vector<unsigned char> (HmmStateSidecar::states / ::streams) is already
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

// HmmStateSidecar::set_arrays -- per-photon uint8 arrays in from NumPy.
%apply(unsigned char* IN_ARRAY1, int DIM1) {
    (unsigned char* states_in, int n_states_in)};
%apply(unsigned char* IN_ARRAY1, int DIM1) {
    (unsigned char* streams_in, int n_streams_in)};

// Release the GIL around the heavy EM / decode kernels (parallel over bursts).
TTTRLIB_NOGIL(tttrlib::HMM::optimize)
TTTRLIB_NOGIL(tttrlib::HMM::viterbi)
TTTRLIB_NOGIL(tttrlib::HMM::fit)
TTTRLIB_NOGIL(tttrlib::HMM::posterior)
TTTRLIB_NOGIL(tttrlib::HMM::sample_states)
TTTRLIB_NOGIL(tttrlib::HMM::sample_paths)

#ifdef SWIGPYTHON
// Array-out consistency: unique inter-photon dt values as a NumPy int64 array.
%feature("pythonappend") tttrlib::HMM::get_unique_dt %{
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

// The emission spec is part of the HMM's surface, not the simulator's: it
// reuses SimDecay to evaluate a decay but exposes none of it, so it can be
// wrapped here even though Sim.i comes later.  It must precede HMM.h, whose
// `optimize` takes an `HmmEmissionSpec*` -- parsed the other way round SWIG
// sees an unknown type and the argument stops accepting a Python spec.
%template(VectorHmmLifetimeSpectrum) std::vector<tttrlib::HmmLifetimeSpectrum>;
%include "HMMEmission.h"

// Same ordering reason as above: HMM.h's `sample` returns an HmmPosterior.
%include "HMMBayes.h"

%include "HMM.h"

%exception;   // scoped to this header only

#ifdef SWIGPYTHON
%extend tttrlib::HmmModel {
    %pythoncode %{
    @property
    def n_states_(self):
        return self.n_states()

    @property
    def n_streams_(self):
        return self.n_streams()

    @property
    def n_symbols_(self):
        return self.n_symbols()

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
        """Emission table as (n_states, n_symbols).

        The alphabet, not the stream count: with a micro-time axis a row is
        `n_streams * n_micro_bins` long.  Use :attr:`obs_micro_np` to see it
        split back into (stream, bin).
        """
        import numpy as np
        n, p = self.n_states(), self.n_symbols()
        return np.asarray(self.obs, dtype=float).reshape(n, p)

    @property
    def obs_micro_np(self):
        """Emission table as (n_states, n_streams, n_micro_bins).

        The same numbers as :attr:`obs_np`, unflattened.  Summing over the last
        axis gives P(stream | state); normalising a (state, stream) row gives
        that state's decay in that stream.
        """
        import numpy as np
        n = self.n_states()
        m = max(int(self.n_micro_bins), 1)
        return np.asarray(self.obs, dtype=float).reshape(n, self.n_streams(), m)

    def to_dict(self):
        return {
            "prior": self.prior_np,
            "trans": self.trans_np,
            "obs": self.obs_np,
            "n_micro_bins": self.n_micro_bins,
            "loglik": self.loglik,
            "logpost": self.logpost,
            "n_iter": self.n_iter,
            "n_phot": self.n_phot,
            "converged": self.converged,
            "bic": self.bic(),
        }
    %}
}

%extend tttrlib::HmmPosterior {
    %pythoncode %{
    def _split(self, flat):
        """One packed parameter block back into (prior, trans, obs)."""
        import numpy as np
        v = np.asarray(flat, dtype=float)
        n, p = self.n_states, self.n_symbols
        return v[:n], v[n:n + n * n].reshape(n, n), v[n + n * n:].reshape(n, p)

    @property
    def draws_np(self):
        """All draws as (n_chains, n_draws, n_par) -- unrelabelled, as sampled."""
        import numpy as np
        return np.asarray(self.draws, dtype=float).reshape(
            self.n_chains, self.n_draws(), self.n_par)

    @property
    def loglik_np(self):
        import numpy as np
        return np.asarray(self.loglik, dtype=float).reshape(
            self.n_chains, self.n_draws())

    def mean_model(self):
        """Posterior mean as (prior, trans, obs), canonically relabelled."""
        return self._split(self.mean())

    def sd_model(self):
        return self._split(self.sd())

    def interval(self, level=0.95):
        """Equal-tailed credible interval as ((lo...), (hi...)) per block."""
        tail = (1.0 - level) / 2.0
        return self._split(self.quantile(tail)), self._split(self.quantile(1.0 - tail))

    def diagnostics(self):
        """Worst split-Rhat and smallest ESS -- the two numbers to check first.

        Quote ESS, not the draw count: 2000 draws at ESS 40 carry the
        information of 40, and an interval quoted from the former is about five
        times narrower than the data support.
        """
        import numpy as np
        r = np.asarray(self.rhat(), dtype=float)
        e = np.asarray(self.ess(), dtype=float)
        r = r[np.isfinite(r)]
        return {"rhat_max": float(r.max()) if r.size else float("nan"),
                "ess_min": float(e.min()) if e.size else float("nan")}
    %}
}

%extend tttrlib::HmmEval {
    %pythoncode %{
    @property
    def xi_np(self):
        """Expected one-tick transition counts as (n_states, n_states)."""
        import numpy as np
        x = np.asarray(self.xi, dtype=float)
        n = int(round(len(x) ** 0.5))
        return x.reshape(n, n)

    def gamma_obs_np(self, n_states):
        """Expected per-(state, symbol) counts as (n_states, n_symbols)."""
        import numpy as np
        return np.asarray(self.gamma_obs, dtype=float).reshape(n_states, -1)

    def score_np(self, n_states):
        """Score split back into (prior, trans, obs) blocks.

        The packed vector is `[prior | trans | obs]`; splitting it needs the
        state count, which the struct does not carry.
        """
        import numpy as np
        s = np.asarray(self.score, dtype=float)
        n = int(n_states)
        return s[:n], s[n:n + n * n].reshape(n, n), s[n + n * n:].reshape(n, -1)

    def posterior_sd_analytic(self, n_states, alpha=1.0):
        """Closed-form posterior sd of (prior, trans, obs) — **a lower bound**.

        Conditional on the state path the posterior is Dirichlet, so plugging in
        the E-step's *expected* counts gives a posterior in closed form, with no
        sampling at all. Measured against `HMM.sample` on 7500 photons: **364x
        cheaper** (0.04 s against 16.3 s) with the means agreeing.

        .. warning::
           It is systematically **too narrow** — measured 1.92x to 2.11x across
           parameters on that dataset. The reason is structural::

               Var(theta|y) = E[Var(theta|y,path)] + Var(E[theta|y,path])
                              \\_ this is what you get _/   \\_ dropped _/

           Only the conjugate term survives; uncertainty in the *path* is
           discarded, and there it was 3x larger. Report this as a lower bound on
           the width, never as a credible interval. For an honest interval use
           `HMM.sample`.

           The factor is not a constant to divide out: it depends on how well the
           path is determined — photon count, state separation, switching rate —
           and simulation-based calibration of the equivalent variational
           approximation measured 2.3-3.4x on other data. Calibrate it with a
           short sampling run if you need one, per dataset class.

        Returns (sd_prior, sd_trans, sd_obs) shaped like the model.
        """
        import numpy as np

        def dir_sd(counts):
            a = np.asarray(counts, dtype=float) + alpha
            a0 = a.sum()
            return np.sqrt(a * (a0 - a) / (a0 * a0 * (a0 + 1.0)))

        n = int(n_states)
        pri = dir_sd(np.asarray(self.prior_counts, dtype=float))
        xi = self.xi_np
        gob = self.gamma_obs_np(n)
        return (pri,
                np.stack([dir_sd(xi[i]) for i in range(n)]),
                np.stack([dir_sd(gob[i]) for i in range(n)]))

    def to_dict(self, n_states):
        import numpy as np
        pr, tr, ob = self.score_np(n_states)
        return {
            "loglik": self.loglik,
            "xi": self.xi_np,
            "gamma_obs": self.gamma_obs_np(n_states),
            "prior_counts": np.asarray(self.prior_counts, dtype=float),
            "score_prior": pr, "score_trans": tr, "score_obs": ob,
        }
    %}
}

%extend tttrlib::HmmEmissionSpec {
    %pythoncode %{
    def build_np(self):
        """Emission table as (n_states, n_streams, n_micro_bins)."""
        import numpy as np
        return np.asarray(self.build(), dtype=float).reshape(
            self.n_states, self.n_streams, self.n_micro_bins)

    def set_irf(self, pattern):
        """Set the IRF from any sequence (list, tuple, NumPy array).

        The member itself is a typed vector, and the decay helpers hand back
        tuples, so assigning one to the other needs a conversion the caller
        should not have to know about.
        """
        self.irf = VectorDouble([float(v) for v in pattern])

    def set_background(self, pattern):
        """Set the background shape from any sequence, `n_streams * n_micro_bins`."""
        self.background = VectorDouble([float(v) for v in pattern])

    def set_model(self, model):
        """Write this spec's table into `model.obs`, tagging its alphabet."""
        model.obs = VectorDouble(list(self.build()))
        model.n_micro_bins = self.n_micro_bins
        return model
    %}
}

%extend tttrlib::HmmChannelMap {
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

%extend tttrlib::HmmStateSidecar {
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

%extend tttrlib::HMM {
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
