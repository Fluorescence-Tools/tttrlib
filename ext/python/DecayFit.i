// SPDX-License-Identifier: BSD-3-Clause
//
// The decay fits, as one interface.
//
// This file used to wrap four estimator classes, a data container and a
// per-model JSON helper each, with a Python layer on top that re-implemented the
// same call once per model. All of that is gone: a fit is reached through
// `DecayFit2`, built by name, and what its parameters, setup and results mean is
// described in the registry rather than restated in a wrapper.
%{
#include "DecayFitProblem.h"
#include "DecayFitPrior.h"
#include "DecayFitContext.h"
#include "DecayFit.h"
#include "DecayFit23.h"   // for the raw model curve exposed below
#include "DecayFitModel.h"
#include "DecayFitNExp.h"
#include "DecayFitDFA.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
using json = nlohmann::json;
%}

%include <std_string.i>
%include <std_vector.i>
#ifdef SWIGJAVASCRIPT
// SWIG 4.2 ships no shared_ptr library for the Node-API backend; misc_types.i
// has already pulled in ext/js/js_shared_ptr.i, which supplies it.
#else
%include <std_shared_ptr.i>
#endif

// std::vector<std::string> is what the registry-driven name helpers return.
%template(VectorString) std::vector<std::string>;

// VectorInt32 (the link vector) and VectorDouble are already instantiated in
// misc_types.i; re-instantiating here would only shadow them.

// Member vectors must be returned BY VALUE, not by pointer into the object.
//
// Without this, `fit.fit(...).parameters[0]` segfaults: SWIG hands back a
// pointer to the member of a temporary that Python frees before the subscript
// runs. It only appears to work when the result is bound to a variable first,
// which makes it a trap that hides during development and crashes in a
// one-liner. %naturalvar gives these members copy semantics, which is also what
// a caller in any of the four languages expects of a field access.
%naturalvar DecayFitOutcome::parameters;
%naturalvar DecayFitOutcome::results;
%naturalvar DecayFitBatchOutcome::parameters;
%naturalvar DecayFitBatchOutcome::results;
%naturalvar DecayFitBatchOutcome::objective;
%naturalvar DecayFitProblem::data;
%naturalvar DecayFitProblem::irf;
%naturalvar DecayFitProblem::background;
%naturalvar DecayFitProblem::model;
%naturalvar DecayFitProblem::setup;
%naturalvar DecayFitConstraints::link;

%shared_ptr(DecayFitPrior)
%shared_ptr(UniformPrior)
%shared_ptr(NormalPrior)
%shared_ptr(TruncatedNormalPrior)
%shared_ptr(HalfNormalPrior)
%shared_ptr(LogNormalPrior)
%shared_ptr(ExponentialPrior)
%shared_ptr(GammaPrior)
%shared_ptr(BetaPrior)
%shared_ptr(ProductPrior)

// The abstract model and the raw factory stay out of the bindings: `DecayFit2`
// owns one and gives every language the same plain object, so no binding has to
// be taught shared-pointer-to-const-abstract-base.
%ignore make_decay_fit;
%ignore register_decay_fit;
%ignore DecayFitModel;
%ignore fit_batch;
%ignore DecayFitLinkMap;

%include "DecayFitProblem.h"
%include "DecayFitPrior.h"
%include "DecayFitModel.h"
%include "DecayFitNExp.i"

%extend DecayFitProblem {
    /*! The problem's scalar description as JSON — a provenance record. */
    std::string get_json() const {
        return $self->to_json().dump();
    }
    void set_json(const std::string& payload) {
        $self->from_json(json::parse(payload));
    }
}

%extend DecayFitConstraints {
    /*! Link vector and priors as JSON, round-trippable. */
    std::string get_json() const {
        return $self->to_json().dump();
    }
    void set_json(const std::string& payload) {
        *$self = DecayFitConstraints::from_json(json::parse(payload));
    }
    /*!
     * Attach a prior from the same `kind`/state payload the Python prior classes
     * serialise to, so a prior crosses the boundary losslessly in either
     * direction rather than being rebuilt by hand on each side.
     */
    void set_prior_json(int slot, const std::string& payload) {
        if (slot < 0) return;
        if ($self->priors.size() <= (size_t) slot) $self->priors.resize(slot + 1);
        $self->priors[slot] = DecayFitPrior::from_json(json::parse(payload));
    }
}

%extend DecayFitPrior {
    /*! The prior's `kind` and parameters, as JSON. */
    std::string get_json() const {
        return $self->to_json().dump();
    }
}

// Raw model curve at given parameters, with no reference to any data.
//
// `DecayFit2::evaluate` cannot serve this purpose: fit23 profiles the model's
// amplitude against the observed counts, so evaluating against empty data gives
// an identically zero curve. Simulation needs the un-normalised curve, so the
// kernel's model function is exposed directly. Kept because a capability with no
// replacement should not disappear in a refactor.
%apply (double* INPLACE_ARRAY1, int DIM1) {
    (double* param, int n_param),
    (double* irf, int n_irf),
    (double* bg, int n_bg),
    (double* corrections, int n_corrections),
    (double* model, int n_model)
}

%inline %{
/*!
 * \brief Evaluate the fit23 model into \p model.
 *
 * \param param `[tau, gamma, r0, rho]`.
 * \param irf Instrument response, `2 * n_bins` (parallel then perpendicular).
 * \param bg Background pattern, same layout.
 * \param dt Micro-time bin width.
 * \param corrections `[period, g, l1, l2, convolution_stop]`.
 * \param model Output, same length as \p irf.
 */
void decay_fit23_model_curve(
        double* param, int n_param,
        double* irf, int n_irf,
        double* bg, int n_bg,
        double dt,
        double* corrections, int n_corrections,
        double* model, int n_model) {
    if (n_param < 4 || n_corrections < 5 || n_irf < 2 || n_bg < n_irf ||
        n_model < n_irf) {
        throw std::invalid_argument(
            "decay_fit23_model_curve: param needs 4 values, corrections 5, and "
            "bg/model must be at least as long as irf");
    }
    // Parameters go straight to the model, exactly as the previous binding did.
    // `correct_input` must NOT be applied here: it re-derives rho from the
    // integrated signals, which are not defined outside a fit, and the caller of
    // a model function is asking for the curve at the parameters it supplied.
    DecayFit23::modelf(param, irf, bg, n_irf / 2, dt, corrections, model);
}
%}

// The donor(x)FRET(x)anisotropy kernel. Exposed as plain functions so the
// physics can be tested on its own — against a direct periodic sum, against the
// closed-form anisotropy — without constructing a fit around it.
//
// ---------------------------------------------------------------------------
// These cross as NumPy buffers, and they used to cross as VectorDouble
// ---------------------------------------------------------------------------
// The default `std::vector<double>` typemaps convert through the Python
// sequence protocol, one boxed float per element each way, so the wrapper set
// the runtime of every call. Measured before the change (arm64, best of 200,
// recursive backend, one rate):
//
//   n_bins   list in    ndarray in
//       64    3.00 us      5.58 us
//      512   12.17 us     26.42 us
//     4096   85.50 us    192.92 us
//    16384  326.00 us    761.96 us
//
// Note which column is worse. Passing a NumPy array — the obvious thing to do,
// and what every caller in this repository does — was *twice* the cost of
// passing a list, because unboxing a NumPy scalar per element is more work than
// unboxing a float. The natural call was the slow one.
//
// The example this kernel exists to justify makes the point: it timed the two
// convolution backends with `irf.tolist()` inside the timing loop, so its
// "recursion vs transform" figure was substantially a measurement of this
// wrapper. See okf/bindings/marshalling-cost.md and PERF.md.
//
// The Python call is unchanged — same names, same order, and a list still works
// because a NumPy input typemap accepts any sequence. What changes is that an
// ndarray is now passed by pointer, and the return is an ndarray rather than a
// VectorDouble proxy.
// These typemaps are NOT Python-only, which is worth stating because it is easy
// to assume otherwise. `ext/r/rarrays.i`, `ext/java/jarrays.i` and
// `ext/js/jsarrays.i` implement the same `IN_ARRAY*` / `INPLACE_ARRAY*` /
// `ARGOUTVIEW(M)_ARRAY*` names against R vectors, Java arrays and JS
// TypedArrays, precisely so one %apply line serves all four languages. Verified
// by generating the R wrapper: `dfa_convolve(rates, weights, irf, n_bins,
// shift_bins, method)` takes plain R numeric vectors. So this conversion makes
// the call cheaper everywhere, not just in Python — no per-language surface,
// and no `#ifdef SWIGPYTHON`.

%clear (double* irf, int n_irf);

%apply (double* IN_ARRAY1, int DIM1) {
    (double* rates, int n_rates),
    (double* weights, int n_weights),
    (double* irf, int n_irf),
    (double* kd, int n_kd), (double* pd, int n_pd),
    (double* kf, int n_kf), (double* pf, int n_pf),
    (double* ka, int n_ka), (double* pa, int n_pa)
}
%apply (double** ARGOUTVIEWM_ARRAY1, int* DIM1) {(double** out, int* n_out)}

%{
// ARGOUTVIEWM hands ownership to NumPy and frees with free(), so the copy out
// of the std::vector has to be malloc'd rather than new'd.
static void dfa_mem_out(const std::vector<double>& v, double** out, int* n_out) {
    double* p = (double*) std::malloc(std::max<size_t>(v.size(), 1) * sizeof(double));
    if (p != nullptr && !v.empty())
        std::memcpy(p, v.data(), v.size() * sizeof(double));
    *out = p;
    *n_out = (int) v.size();
}

// A borrowed NumPy buffer as the vector the dfa:: signatures take. This is a
// memcpy-rate copy inside C++ (~0.5 ns/element); what it avoids is the
// Python-side conversion, which boxes one object per element.
static inline std::vector<double> dfa_mem_in(const double* p, int n) {
    return (p == nullptr || n <= 0) ? std::vector<double>()
                                    : std::vector<double>(p, p + n);
}
%}

%inline %{
/*! VV/VH decay of a donor(x)FRET(x)anisotropy rate spectrum; see DecayFitDFA.h. */
void dfa_vv_vh_decay(
        double* kd, int n_kd, double* pd, int n_pd,
        double* kf, int n_kf, double* pf, int n_pf,
        double* ka, int n_ka, double* pa, int n_pa,
        double r0, double g, int n_bins,
        double** out, int* n_out) {
    std::vector<double> vv, vh;
    dfa::vv_vh_decay(dfa_mem_in(kd, n_kd), dfa_mem_in(pd, n_pd),
                     dfa_mem_in(kf, n_kf), dfa_mem_in(pf, n_pf),
                     dfa_mem_in(ka, n_ka), dfa_mem_in(pa, n_pa),
                     r0, g, (std::size_t) n_bins, vv, vh);
    vv.insert(vv.end(), vh.begin(), vh.end());   // returned in VV|VH layout
    dfa_mem_out(vv, out, n_out);
}

/*! One periodic multiexponential decay, from the closed form. */
void dfa_periodic_decay(
        double* rates, int n_rates, double* weights, int n_weights,
        int n_bins, double** out, int* n_out) {
    std::vector<std::complex<double>> spectrum;
    std::vector<double> decay;
    dfa::periodic_spectrum(dfa_mem_in(rates, n_rates), dfa_mem_in(weights, n_weights),
                           (std::size_t) n_bins, spectrum);
    dfa::inverse(spectrum, (std::size_t) n_bins, decay);
    dfa_mem_out(decay, out, n_out);
}

/*!
 * Convolve a periodic multiexponential with the IRF, choosing the backend.
 * method 0 = recursive (default, ~7x faster), 1 = spectral. Both describe the
 * same instrument: the spectral path pre-filters the IRF with the same [1/2,1/2]
 * kernel the recursion's trapezoid rule applies, so they do not sit half a bin
 * apart. See DecayFitDFA.h.
 */
void dfa_convolve(
        double* rates, int n_rates, double* weights, int n_weights,
        double* irf, int n_irf, int n_bins, double shift_bins, int method,
        double** out, int* n_out) {
    std::vector<double> decay;
    dfa::convolve(method == 1 ? dfa::ConvolutionMethod::Spectral
                              : dfa::ConvolutionMethod::Recursive,
                  dfa_mem_in(rates, n_rates), dfa_mem_in(weights, n_weights),
                  dfa_mem_in(irf, n_irf), (std::size_t) n_bins, shift_bins, decay);
    dfa_mem_out(decay, out, n_out);
}

/*! VV/VH decay convolved with the IRF, choosing the backend (0 rec, 1 spectral). */
void dfa_vv_vh_convolved(
        double* kd, int n_kd, double* pd, int n_pd,
        double* kf, int n_kf, double* pf, int n_pf,
        double* ka, int n_ka, double* pa, int n_pa,
        double r0, double g, double* irf, int n_irf,
        int n_bins, double shift_bins, int method,
        double** out, int* n_out) {
    std::vector<double> vv, vh;
    dfa::vv_vh_convolved(method == 1 ? dfa::ConvolutionMethod::Spectral
                                     : dfa::ConvolutionMethod::Recursive,
                         dfa_mem_in(kd, n_kd), dfa_mem_in(pd, n_pd),
                         dfa_mem_in(kf, n_kf), dfa_mem_in(pf, n_pf),
                         dfa_mem_in(ka, n_ka), dfa_mem_in(pa, n_pa),
                         r0, g, dfa_mem_in(irf, n_irf),
                         (std::size_t) n_bins, shift_bins, vv, vh);
    vv.insert(vv.end(), vh.begin(), vh.end());
    dfa_mem_out(vv, out, n_out);
}

/*!
 * A periodic decay convolved with a normalised IRF, the IRF carrying the
 * timeshift. The shift goes on the IRF and not on the decay: a phase ramp is
 * band-limited interpolation and the decay steps at the period boundary, so
 * shifting it rings and goes negative. See DecayFitDFA.h.
 */
void dfa_convolved_decay(
        double* rates, int n_rates, double* weights, int n_weights,
        double* irf, int n_irf, int n_bins, double shift_bins,
        double** out, int* n_out) {
    std::vector<std::complex<double>> si, sd;
    std::vector<double> decay;
    dfa::normalised_spectrum(dfa_mem_in(irf, n_irf), (std::size_t) n_bins, si);
    dfa::apply_timeshift(si, (std::size_t) n_bins, shift_bins);
    dfa::periodic_spectrum(dfa_mem_in(rates, n_rates), dfa_mem_in(weights, n_weights),
                           (std::size_t) n_bins, sd);
    for (std::size_t w = 0; w < si.size(); ++w) sd[w] *= si[w];
    dfa::inverse(sd, (std::size_t) n_bins, decay);
    dfa_mem_out(decay, out, n_out);
}
%}

%clear (double* rates, int n_rates);
%clear (double* weights, int n_weights);
%clear (double* irf, int n_irf);
%clear (double* kd, int n_kd);
%clear (double* pd, int n_pd);
%clear (double* kf, int n_kf);
%clear (double* pf, int n_pf);
%clear (double* ka, int n_ka);
%clear (double* pa, int n_pa);
%clear (double** out, int* n_out);


#ifdef SWIGPYTHON
%pythoncode "./ext/python/DecayFitPython.py"
%pythoncode "./ext/python/FitNExpWrapper.py"
// Deprecated pre-interface API, kept working until 0.29 so callers can migrate
// on their own schedule. Python only; the other bindings took the clean break.
%pythoncode "./ext/python/Fit2xCompat.py"
#endif
