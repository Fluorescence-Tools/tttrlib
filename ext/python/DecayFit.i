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

// The donor(x)FRET(x)anisotropy kernel. Exposed as plain vector functions so the
// physics can be tested on its own — against a direct periodic sum, against the
// closed-form anisotropy — without constructing a fit around it.
%inline %{
/*! VV/VH decay of a donor(x)FRET(x)anisotropy rate spectrum; see DecayFitDFA.h. */
std::vector<double> dfa_vv_vh_decay(
        const std::vector<double>& kd, const std::vector<double>& pd,
        const std::vector<double>& kf, const std::vector<double>& pf,
        const std::vector<double>& ka, const std::vector<double>& pa,
        double r0, double g, int n_bins) {
    std::vector<double> vv, vh;
    dfa::vv_vh_decay(kd, pd, kf, pf, ka, pa, r0, g, (std::size_t) n_bins, vv, vh);
    vv.insert(vv.end(), vh.begin(), vh.end());   // returned in VV|VH layout
    return vv;
}

/*! One periodic multiexponential decay, from the closed form. */
std::vector<double> dfa_periodic_decay(
        const std::vector<double>& rates, const std::vector<double>& weights,
        int n_bins) {
    std::vector<std::complex<double>> spectrum;
    std::vector<double> decay;
    dfa::periodic_spectrum(rates, weights, (std::size_t) n_bins, spectrum);
    dfa::inverse(spectrum, (std::size_t) n_bins, decay);
    return decay;
}

/*!
 * Convolve a periodic multiexponential with the IRF, choosing the backend.
 * method 0 = recursive (default, ~7x faster), 1 = spectral. Both describe the
 * same instrument: the spectral path pre-filters the IRF with the same [1/2,1/2]
 * kernel the recursion's trapezoid rule applies, so they do not sit half a bin
 * apart. See DecayFitDFA.h.
 */
std::vector<double> dfa_convolve(
        const std::vector<double>& rates, const std::vector<double>& weights,
        const std::vector<double>& irf, int n_bins, double shift_bins, int method) {
    std::vector<double> out;
    dfa::convolve(method == 1 ? dfa::ConvolutionMethod::Spectral
                              : dfa::ConvolutionMethod::Recursive,
                  rates, weights, irf, (std::size_t) n_bins, shift_bins, out);
    return out;
}

/*! VV/VH decay convolved with the IRF, choosing the backend (0 rec, 1 spectral). */
std::vector<double> dfa_vv_vh_convolved(
        const std::vector<double>& kd, const std::vector<double>& pd,
        const std::vector<double>& kf, const std::vector<double>& pf,
        const std::vector<double>& ka, const std::vector<double>& pa,
        double r0, double g, const std::vector<double>& irf,
        int n_bins, double shift_bins, int method) {
    std::vector<double> vv, vh;
    dfa::vv_vh_convolved(method == 1 ? dfa::ConvolutionMethod::Spectral
                                     : dfa::ConvolutionMethod::Recursive,
                         kd, pd, kf, pf, ka, pa, r0, g, irf,
                         (std::size_t) n_bins, shift_bins, vv, vh);
    vv.insert(vv.end(), vh.begin(), vh.end());
    return vv;
}

/*!
 * A periodic decay convolved with a normalised IRF, the IRF carrying the
 * timeshift. The shift goes on the IRF and not on the decay: a phase ramp is
 * band-limited interpolation and the decay steps at the period boundary, so
 * shifting it rings and goes negative. See DecayFitDFA.h.
 */
std::vector<double> dfa_convolved_decay(
        const std::vector<double>& rates, const std::vector<double>& weights,
        const std::vector<double>& irf, int n_bins, double shift_bins) {
    std::vector<std::complex<double>> si, sd;
    std::vector<double> out;
    dfa::normalised_spectrum(irf, (std::size_t) n_bins, si);
    dfa::apply_timeshift(si, (std::size_t) n_bins, shift_bins);
    dfa::periodic_spectrum(rates, weights, (std::size_t) n_bins, sd);
    for (std::size_t w = 0; w < si.size(); ++w) sd[w] *= si[w];
    dfa::inverse(sd, (std::size_t) n_bins, out);
    return out;
}
%}

#ifdef SWIGPYTHON
%pythoncode "./ext/python/DecayFitPython.py"
// Deprecated pre-interface API, kept working until 0.29 so callers can migrate
// on their own schedule. Python only; the other bindings took the clean break.
%pythoncode "./ext/python/Fit2xCompat.py"
#endif
