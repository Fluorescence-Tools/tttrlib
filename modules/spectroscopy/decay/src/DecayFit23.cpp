// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit23.h"
#include "Verbose.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>


static thread_local DecayFitIntegrateSignals fit_signals;
static thread_local DecayFitCorrections fit_corrections;
static thread_local DecayFitSettings fit_settings;

namespace {

constexpr double kMinTau = 1.0e-3;
constexpr double kMinRho = 1.0e-6;
constexpr double kMinGamma = 0.0;
constexpr double kMaxGamma = 0.999;

inline double clamp_value(double value, double lower, double upper) {
    return std::max(lower, std::min(value, upper));
}

struct Decay23Parameters {
    double tau;
    double gamma;
    double r0;
    double rho;
};

/// Keep the model arithmetically evaluable, without flattening the objective.
///
/// tau and rho go through `soft_floor` rather than a hard clamp: identical
/// above the floor (bit-for-bit, so no ordinary fit moves), smooth, strictly
/// positive and with a nonzero derivative below it. The hard clamp made the
/// model *identical* for every value under the floor, so the objective was flat
/// and its gradient exactly zero there -- which is what a bound then has to
/// supply from outside, and what an analytic gradient would read as a genuine
/// zero. See soft_floor in DecayFit.h.
///
/// gamma is still hard-clamped. Unlike tau it has no arithmetic failure outside
/// its range -- the model is finite at gamma = -0.2 and gamma = 1.5, measured --
/// so its clamp is a modelling constraint, and constraints belong in
/// `set_bounds`, not here. Removing it is a real behaviour change on its own
/// and is deliberately not bundled into this one.
inline Decay23Parameters sanitise_parameters(const double *param) {
    Decay23Parameters result{};
    result.tau = soft_floor(param[0], kMinTau);
    result.gamma = clamp_value(param[1], kMinGamma, kMaxGamma);
    result.r0 = param[2];
    result.rho = soft_floor(param[3], kMinRho);
    return result;
}

inline void apply_corrections(double *corrections) {
    fit_corrections.period = corrections[0];
    fit_corrections.g = corrections[1];
    fit_corrections.l1 = corrections[2];
    fit_corrections.l2 = corrections[3];
    fit_corrections.convolution_stop = static_cast<int>(corrections[4]);
}


inline double count_entropy(int count) {
    static const std::array<double, 4096> table = [] {
        std::array<double, 4096> values{};
        for (size_t i = 1; i < values.size(); ++i)
            values[i] = static_cast<double>(i) * std::log(static_cast<double>(i));
        return values;
    }();
    if (count >= 0 && count < static_cast<int>(table.size()))
        return table[static_cast<size_t>(count)];
    return count > 0 ? count * std::log(static_cast<double>(count)) : 0.0;
}

// Bounded derivative-free 1-D minimiser (Brent 1973 / Forsythe-Malcolm-Moler).
// Keeping the evaluator generic lets the public fit use the full Fit23 target
// while the exact unpolarized batch specialization uses its fused target.
template <typename Feval>
inline double brent_minimize_tau(double a, double b, Feval &&feval,
                                 double tol, int max_iter) {
    const double gc = 0.5 * (3.0 - std::sqrt(5.0));  // golden-section fraction
    const double eps = std::sqrt(std::numeric_limits<double>::epsilon());
    double x = a + gc * (b - a), w = x, v = x;
    double fx = feval(x), fw = fx, fv = fx;
    double d = 0.0, e = 0.0;
    for (int iter = 0; iter < max_iter; ++iter) {
        double m = 0.5 * (a + b);
        double tol1 = eps * std::fabs(x) + tol;
        double tol2 = 2.0 * tol1;
        if (std::fabs(x - m) <= tol2 - 0.5 * (b - a)) break;
        double pp = 0.0, q = 0.0, r = 0.0;
        bool golden = true;
        if (std::fabs(e) > tol1) {  // try a parabolic interpolation step
            r = (x - w) * (fx - fv);
            q = (x - v) * (fx - fw);
            pp = (x - v) * q - (x - w) * r;
            q = 2.0 * (q - r);
            if (q > 0.0) pp = -pp;
            q = std::fabs(q);
            double etemp = e;
            e = d;
            if (std::fabs(pp) < std::fabs(0.5 * q * etemp) &&
                pp > q * (a - x) && pp < q * (b - x)) {
                d = pp / q;
                double u = x + d;
                if (u - a < tol2 || b - u < tol2)
                    d = (m > x) ? tol1 : -tol1;
                golden = false;
            }
        }
        if (golden) {  // fall back to a golden-section step
            e = (x < m) ? (b - x) : (a - x);
            d = gc * e;
        }
        double u = (std::fabs(d) >= tol1) ? (x + d)
                                          : (x + ((d > 0.0) ? tol1 : -tol1));
        double fu = feval(u);
        if (fu <= fx) {
            if (u < x) b = x; else a = x;
            v = w; fv = fw; w = x; fw = fx; x = u; fx = fu;
        } else {
            if (u < x) a = u; else b = u;
            if (fu <= fw || w == x) { v = w; fv = fw; w = u; fw = fu; }
            else if (fu <= fv || v == x || v == w) { v = u; fv = fu; }
        }
    }
    return x;
}

inline double safe_harmonic_mean(double a, double b) {
    if (b <= 0.) {
        return a;
    }
    const double denom = 1. / a + 1. / b;
    if (denom <= std::numeric_limits<double>::min()) {
        return a;
    }
    return 1. / denom;
}

} // namespace



void DecayFit23::correct_input(double *x, double *xm, double *corrections, int return_r) {
    fit_signals.corrections = &fit_corrections;

    const Decay23Parameters initial = sanitise_parameters(x);

    xm[0] = initial.tau;
    // The tau bound is the optimiser's job (set_bounds below), not a term
    // hand-added to the objective here. The old line was
    //
    //     fit_settings.penalty = (x[0] < kMinTau) ? -x[0] : 0.;
    //
    // and it was wrong in three ways, all measured on the real objective:
    //
    //  * **The sign is inverted over most of its range.** For the band
    //    0 < tau < kMinTau the term is *negative*, so stepping below the bound
    //    made the objective BETTER. Measured: crossing from tau = 1.1e-3 to
    //    9e-4 improved it by 9e-4. It only penalises once tau goes negative.
    //  * It is discontinuous at kMinTau, which puts a spurious spike in the
    //    numerical gradient there (measured d/dtau = 4999.5 at the crossing,
    //    against -0.0004 just above it).
    //  * It is invisible to an analytic gradient. It is added to the objective
    //    outside the model, so a templated AD pass over the model chain would
    //    miss it entirely and be wrong by exactly -1 in the tau component
    //    below the bound. `i_lbfgs` adds its own bound penalty *and that
    //    penalty's gradient* to whatever the analytic callback returns
    //    (i_lbfgs.h:313-320), so routing the bound through set_bounds is what
    //    makes the AD conversion possible at all.
    fit_settings.penalty = 0.;

    fit_corrections.set_gamma(initial.gamma);
    apply_corrections(corrections);

    xm[1] = initial.gamma;
    xm[2] = initial.r0;

    double rho_value = initial.rho;
    if (!fit_settings.fixedrho) {
        rho_value = fit_signals.rho(xm[0], xm[2]);
        x[3] = rho_value;
    }
    // Same smooth floor as sanitise_parameters. This second guard exists because
    // rho is usually *derived* here (fit_signals.rho) rather than taken from the
    // caller, so it has not been through sanitise_parameters at this point.
    xm[3] = soft_floor(rho_value, kMinRho);

    if (return_r) {
        x[7] = fit_signals.rs();
        x[6] = fit_signals.r();
    }
if (is_verbose()) {
    std::cout << "CORRECT_INPUT23" << std::endl;
    std::cout << fit_corrections.str();
    std::cout << fit_signals.str();
    std::cout << "-- Corrected parameters:" << std::endl;
    std::cout << "-- tau / tau_c: " << x[0] << "/" << xm[0] << std::endl;
    std::cout << "-- gamma / gamma_c: " << x[1] << "/" << xm[1] << std::endl;
    std::cout << "-- r0 / r0_c: " << x[2] << "/" << xm[2] << std::endl;
    std::cout << "-- rho / rho_c: " << x[3] << "/" << xm[3] << std::endl;
}
}

int DecayFit23::modelf(
        double *param,            // here: [tau gamma r0 rho]
        double *irf,
        double *bg,
        int Nchannels,
        double dt,                // time per channel
        double *corrections,      // [period g l1 l2]
        double *mfunction)        // out: model function in Jordi-girl format
{
    (void)dt; // parameter kept for API compatibility
    fit_signals.corrections = &fit_corrections;

    const Decay23Parameters safe_param = sanitise_parameters(param);
    const double tau = safe_param.tau;
    const double gamma = safe_param.gamma;
    const double r0 = safe_param.r0;
    const double rho = safe_param.rho;

    fit_corrections.set_gamma(gamma);
    apply_corrections(corrections);

    const double taurho = safe_harmonic_mean(tau, rho);

    // The parallel (vv) and perpendicular (vh) channels share the two
    // lifetimes (tau, taurho) and differ only in amplitude and IRF, so both
    // are convolved together (NEON 2-lane on AArch64).
    double x_vv[4], x_vh[4];
    x_vv[0] = 1.;
    x_vv[1] = tau;
    x_vv[2] = r0 * (2. - 3. * fit_corrections.l1);
    x_vv[3] = taurho;
    x_vh[0] = 1. / fit_corrections.g;
    x_vh[1] = tau;
    x_vh[2] = x_vh[0] * r0 * (-1. + 3. * fit_corrections.l2);
    x_vh[3] = taurho;
    fconv_per_cs_2ch(
            mfunction, mfunction + Nchannels, x_vv, x_vh, irf, irf + Nchannels,
            2, Nchannels - 1, Nchannels,
            fit_corrections.period, fit_corrections.convolution_stop, dt);

    /// add background
    //
    // gamma weights the (caller-supplied) background pattern; the caller is
    // responsible for its normalisation. This is the cross-language model
    // contract exercised by the Python/R/Java reference tests, so it is left
    // exactly as-is. (chisurf's burst-MLE wizard area-normalises the background
    // before the fit so gamma is a true 0..1 fraction there.)
    double sum_m = 0.;
    for (int i = 0; i < 2 * Nchannels; i++) sum_m += mfunction[i];
    if (sum_m <= 0.) {
        for (int i = 0; i < 2 * Nchannels; i++) {
            mfunction[i] = bg[i] * gamma;
        }
    } else {
        const double scale = (1. - gamma) / sum_m;
        for (int i = 0; i < 2 * Nchannels; i++) {
            mfunction[i] = mfunction[i] * scale + bg[i] * gamma;
        }
    }

if (is_verbose()) {
    std::cout << "COMPUTE MODEL23" << std::endl;
    std::cout << "-- tau: " << tau << std::endl;
    std::cout << "-- gamma: " << gamma << std::endl;
    std::cout << "-- r0: " << r0 << std::endl;
    std::cout << "-- rho: " << rho << std::endl;
    std::cout << fit_corrections.str();
}

    return 0;
}


double DecayFit23::targetf(double *x, void *pv) {

    double w, xm[8], Bgamma;
    (void)Bgamma; // silence unused variable warning on MSVC
    DecayFitContext *p = (DecayFitContext *) pv;

    // Defensive: targetf is also exposed directly to the bindings, so it must
    // not read past inconsistently sized arrays. fit() validates once before the
    // optimiser runs; this covers a direct call. Return a large finite penalty so
    // an optimiser moving through here is simply pushed away, never crashed.
    if (p == nullptr || !p->is_usable()) {
        return std::numeric_limits<double>::max();
    }

    p->iterations++;
    const int *expdata = p->counts;
    int Nchannels = p->n_bins;
    double *irf = const_cast<double *>(p->irf()), *bg = const_cast<double *>(p->background()),
            *corrections = const_cast<double *>(p->corrections), *M = p->model();
    DecayFit23::correct_input(x, xm, corrections, 0);
    // Sp/Ss/B are data-only and constant during a fit (fit() computes them once
    // before the optimiser runs); only Bexpected, used by the soft-BIFL term
    // below, depends on the parameters. So recompute the signal integrals here
    // only when soft-BIFL is active — otherwise reuse the cached values and save
    // an O(2*Nchannels) pass on every objective evaluation.
    if (fit_settings.softbifl)
        fit_signals.compute_signal_and_background(expdata, bg, Nchannels);

    DecayFit23::modelf(xm, irf, bg, Nchannels, p->dt, corrections, M);
    fit_signals.normM(M, 1., Nchannels);
    if (fit_settings.p2s_twoIstar)
        w = Wcm_p2s(expdata, M, Nchannels);
    else
        w = Wcm(expdata, M, Nchannels);

    if (fit_settings.softbifl && (fit_signals.Bexpected > 0.)) {
        w -= fit_signals.Bexpected * log(fit_signals.Bexpected) - loggammaf(fit_signals.Bexpected + 1.);
    }
    double v = w / Nchannels + fit_settings.penalty;
if (is_verbose()) {
    std::cout << "COMPUTING TARGET23" << std::endl;
    std::cout << "xm:" ; for(int i=0; i<8;i++) std::cout << xm[i] << " "; std::cout << std::endl;
    std::cout << "score:"  << v << std::endl;
}
    return v;
}


/*!
 * Score `x` without optimising, with the same preamble `fit` runs.
 *
 * `targetf` alone is not a complete evaluation: `correct_input` derives rho from
 * the integrated signals, and which parameters are held is thread-local state
 * that `fit` sets before the optimiser starts. Calling `targetf` cold therefore
 * yields NaN rather than a score. This is the entry point a caller wanting "the
 * objective at these parameters" should use.
 */
double DecayFit23::evaluate(double *x, short *fixed, DecayFitContext *p) {
    if (p == nullptr || x == nullptr || fixed == nullptr || !p->is_usable()) {
        return std::numeric_limits<double>::infinity();
    }
    if (fit_settings.firstcall) init_fact();
    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[4] < 0.);
    fit_settings.p2s_twoIstar = (x[5] > 0.);
    fit_signals.corrections = &fit_corrections;
    fit_settings.fixedrho = fixed[3];
    fit_signals.compute_signal_and_background(p->counts, p->background(), p->n_bins);
    return DecayFit23::targetf(x, p);
}


double DecayFit23::fit(double *x, short *fixed, DecayFitContext *p) {
    double tIstar, xm[8];
    int info = -1;

    // Guard against inconsistently sized inputs before touching any array: a
    // decay longer than the IRF would make the objective read past the end of
    // irf/background and crash. Every binding (Python/Java/R) reaches native
    // code through here, so validating here fails safely everywhere rather than
    // segfaulting in whichever binding got there first.
    if (p == nullptr || x == nullptr || fixed == nullptr) {
        if (x != nullptr) x[0] = -1.0;
        return std::numeric_limits<double>::infinity();
    }
    if (!p->is_usable()) {
        x[0] = -1.0;   // report an invalid fit rather than crashing
        return std::numeric_limits<double>::infinity();
    }

    if (fit_settings.firstcall) init_fact();

    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[4] < 0.);
    fit_settings.p2s_twoIstar = (x[5] > 0.);
    fit_signals.corrections = &fit_corrections;
    fit_settings.fixedrho = fixed[3];

    double *corrections = const_cast<double *>(p->corrections), *M = p->model();
    const int *expdata = p->counts;
    int Nchannels = p->n_bins;
    fit_signals.compute_signal_and_background(expdata, p->background(), Nchannels);
    correct_input(x, xm, corrections, 1);

    // Fast path: when only the lifetime tau is free (gamma/r0/rho fixed) the
    // fit is a 1-D minimisation, so use the bounded Brent minimiser instead of
    // the general BFGS engine — far fewer objective evaluations, same minimum.
    const bool tau_only = (!fixed[0]) && fixed[1] && fixed[2] && fixed[3];
    if (tau_only) {
        double xloc[8];
        for (int k = 0; k < 8; ++k) xloc[k] = x[k];
        const double hi = corrections[0] > 0.0
                              ? corrections[0]                      // excitation period
                              : std::max(1.0, Nchannels * p->dt);   // else TAC range
        auto feval = [&](double tau) {
            xloc[0] = tau;
            return DecayFit23::targetf(xloc, p);
        };
        // The same prior bounds the general path applies. If the two disagreed,
        // a fit's answer would depend on which internal path it happened to
        // take — a specialisation is an optimisation, not a different model.
        double lo_tau = kMinTau, hi_tau = hi;
        if (p->lower != nullptr && std::isfinite(p->lower[0]))
            lo_tau = std::max(lo_tau, p->lower[0]);
        if (p->upper != nullptr && std::isfinite(p->upper[0]))
            hi_tau = std::min(hi_tau, p->upper[0]);
        if (hi_tau <= lo_tau) hi_tau = lo_tau;
        x[0] = brent_minimize_tau(lo_tau, hi_tau, feval, 1.0e-4, 100);
        feval(x[0]);  // leave p->model evaluated at the minimizer
        info = 1;
    } else {
        bfgs bfgs_o(DecayFit23::targetf, 4);

        // One mechanism for every bound in this fit.
        //
        // `sanitise_parameters` still floors tau and rho before building the
        // model, but that is a *numerical guard and nothing else*: without it
        // exp(-dt/tau) overflows for tau in roughly (-dt/709, 0) and the model
        // comes back non-finite -- measured, at tau = -1e-6 every model bin is
        // inf and the objective is NaN. It is a smooth floor rather than a hard
        // clamp (soft_floor, DecayFit.h) so the parameter map has no corner.
        //
        // The guard is not, and cannot be, the constraint. Two reasons, both
        // measured. A clamped objective is *flat* outside the box -- d/dgamma is
        // exactly 0 at gamma = 1.0, 1.2 and 2.0. And below kMinTau the objective
        // is flat no matter what the guard does, because the data cannot resolve
        // a lifetime that short: the numerical derivative is already zero at
        // tau = 1.1e-3, above the floor. Nothing about the parameter map can
        // manufacture information the likelihood does not contain.
        //
        // So the restoring gradient has to come from outside the objective, and
        // it comes from here. set_bounds is a soft exterior penalty: untouched
        // inside the box, a real gradient outside it, and applied to the
        // finite-difference and analytic-gradient paths alike.
        bfgs_o.set_bounds(0, kMinTau, std::numeric_limits<double>::infinity());
        bfgs_o.set_bounds(1, kMinGamma, kMaxGamma);

        // Bounds are priors in this interface, so anything the caller attached
        // to a slot has to reach the optimiser that actually moves it. Without
        // this the bound was accepted, stored, serialised — and ignored. Applied
        // after the defaults above so a caller-supplied prior wins.
        apply_context_bounds(bfgs_o, p, 4);

        bfgs_o.fix(1);    // gamma
        bfgs_o.fix(2);    // r0
        bfgs_o.fix(3);    // rho is set in targetf23

        // pre-fit with fixed gamma
        //  bfgs_o.maxiter = 20;
        if(!fixed[0]){
            info = bfgs_o.minimize(x, p);
        }else {
            bfgs_o.fix(0);
        }

        // fit with free gamma
        // bfgs_o.maxiter = 100;
        if (!fixed[1] && (x[4] <= 0.)) {
            bfgs_o.free(1);
            // gamma's soft bound is set once, above, with every other bound --
            // it used to be applied only here, in the branch that frees gamma,
            // which meant the pre-fit above and every caller-supplied bound
            // lived under different rules than this one call.
            info = bfgs_o.minimize(x, p);
        }
    }

    // use return_r to get the anisotropy in x
    correct_input(x, xm, corrections, 1);
    if (fit_settings.p2s_twoIstar)
        tIstar = twoIstar_p2s(expdata, M, Nchannels);
    else
        tIstar = twoIstar(expdata, M, Nchannels);

    if (info == 5 || x[0] < 0.) x[0] = -1.;        // for report
    x[1] = xm[1];
if (is_verbose()) {
    std::cout << "FIT23" << std::endl;
    std::cout << "-- BFGS info: " << info << std::endl;
    std::cout << "-- Initial parameters / fixed: " << std::endl;
    std::cout << "-- tau: " << x[0] << " / " << fixed[0] << std::endl;
    std::cout << "-- gamma: " << x[1] << " / " << fixed[1] << std::endl;
    std::cout << "-- r0: " << x[2] << " / " << fixed[2] << std::endl;
    std::cout << "-- rho: " << x[3] << " / " << fixed[3] << std::endl;
    std::cout << "-- Soft BIFL scatter fit?: " << x[4] << std::endl;
    std::cout << "-- 2I*: P+2S?: " << x[5] << std::endl;
    std::cout << "-- r Scatter (output only): " << x[6] << std::endl;
    std::cout << "-- r Experimental (output only): " << x[7] << std::endl;
}
    return tIstar;
}


bool DecayFit23::fit_tau_only_unpolarized_row(
        const double *data,
        int n_cols,
        const double *x0,
        int n_x0,
        const short *fixed,
        int n_fixed,
        double bifl_scatter,
        double p2s_flag,
        DecayFitContext *p,
        double *out,
        int n_out_cols,
        bool retain_model) {
    (void)bifl_scatter;  // gamma == 0 makes the soft-BIFL term identically zero

    if (data == nullptr || x0 == nullptr || fixed == nullptr || p == nullptr ||
        out == nullptr || n_x0 < 4 || n_fixed < 4 || n_out_cols < 5 ||
        n_cols <= 0 || (n_cols & 1) != 0 ||
        fixed[0] || !fixed[1] || !fixed[2] || !fixed[3] ||
        p2s_flag > 0.0 || !std::isfinite(x0[1]) || x0[1] > 0.0 ||
        !std::isfinite(x0[2]) || x0[2] != 0.0 ||
        p->problem == nullptr ||
        static_cast<int>(p->problem->irf.size()) != n_cols ||
        static_cast<int>(p->problem->background.size()) != n_cols ||
        p->counts == nullptr || p->corrections == nullptr) {
        return false;
    }

    const int n_channels = n_cols / 2;
    const double *corrections = p->corrections;
    const double g = corrections[1];
    if (!std::isfinite(g) || g <= 0.0 ||
        !std::equal(p->problem->irf.begin(), p->problem->irf.begin() + n_channels,
                    p->problem->irf.begin() + n_channels)) {
        return false;
    }
    // The generic model evaluates bg[i] * gamma even for gamma == 0. Avoid
    // changing its NaN semantics for non-finite background inputs.
    for (double value : p->problem->background) {
        if (!std::isfinite(value)) return false;
    }


    // During minimization the first half stores Cp+Cs and the second half Cp.
    // This avoids an allocation for combined counts. The original Jordi row is
    // restored when the caller asks to retain the final model; batch workers
    // can skip that work for all non-final rows.
    double sp = 0.0;
    double ss = 0.0;
    double data_entropy = 0.0;
    for (int i = 0; i < n_channels; ++i) {
        const int cp = static_cast<int>(data[i]);
        const int cs = static_cast<int>(data[i + n_channels]);
        if (cp < 0 || cs < 0) return false;
        p->counts[i] = cp + cs;
        p->counts[i + n_channels] = cp;
        sp += cp;
        ss += cs;
        data_entropy += count_entropy(cp) + count_entropy(cs);
    }
    const double total = sp + ss;
    const double perpendicular_scale = 1.0 / g;
    const double log_total = total > 0.0 ? std::log(total) : 0.0;
    const double log_perpendicular = std::log(perpendicular_scale);
    const double log_channel_scale = std::log1p(perpendicular_scale);
    double *model = p->model();
    const double *irf = p->problem->irf.data();

    // A one-bin IRF over one complete TAC period has a piecewise geometric
    // convolution. Its Poisson score therefore depends on only five row
    // sufficient statistics. This keeps Brent a true continuous MLE while
    // reducing its intermediate objective evaluations from O(n_channels) to
    // O(1). Any case where the generic Wcm threshold could matter falls back
    // to the convolution below.
    int delta_bin = -1;
    bool delta_irf = g > 0.0 && p->dt > 0.0 &&
                     static_cast<int>(corrections[4]) == n_channels - 1 &&
                     corrections[0] == n_channels * p->dt;
    if (delta_irf) {
        for (int i = 0; i < n_channels; ++i) {
            if (irf[i] != 0.0) {
                if (delta_bin >= 0 || !std::isfinite(irf[i]) || irf[i] <= 0.0) {
                    delta_irf = false;
                    break;
                }
                delta_bin = i;
            }
        }
        // The last-bin pulse has a different finite-convolution boundary
        // term; keep that rare case on the generic exact path.
        delta_irf = delta_irf && delta_bin > 0 &&
                    delta_bin < n_channels - 1;
    }

    double prefix_counts = 0.0;
    double prefix_index = 0.0;
    double pulse_counts = 0.0;
    double suffix_counts = 0.0;
    double suffix_distance = 0.0;
    bool used_sufficient_statistics = false;
    if (delta_irf) {
        for (int i = 0; i < delta_bin; ++i) {
            const double counts = p->counts[i];
            prefix_counts += counts;
            prefix_index += counts * i;
        }
        pulse_counts = p->counts[delta_bin];
        for (int i = delta_bin + 1; i < n_channels; ++i) {
            const double counts = p->counts[i];
            suffix_counts += counts;
            suffix_distance += counts * (i - delta_bin);
        }
    }

    auto evaluate = [&](double tau, bool keep_model) {
        used_sufficient_statistics = false;
        tau = std::max(tau, kMinTau);
        if (delta_irf && !keep_model && total > 0.0) {
            const double log_e = -p->dt / tau;
            const double e = std::exp(log_e);
            const double period_tail = std::exp(n_channels * log_e);
            const double tail_denominator = 1.0 - period_tail;
            const int suffix_length = n_channels - 1 - delta_bin;
            const double e_to_pulse = std::exp(delta_bin * log_e);
            const double e_to_suffix = std::exp(suffix_length * log_e);
            const double wrap = period_tail * e / tail_denominator;

            // The common factor dt*IRF_amplitude cancels between q and its
            // normalization. Work with the dimensionless shape to avoid two
            // transcendental operations per objective evaluation.
            const double log_t = std::log(wrap) -
                                 std::log(e_to_pulse);
            const double log_pulse = std::log(0.5 + wrap);
            const double log_suffix_base = std::log1p(wrap);
            const double geometric_denominator = 1.0 - e;
            const double prefix_sum = (wrap / e_to_pulse) *
                    (1.0 - e_to_pulse) / geometric_denominator;
            const double suffix_sum = (1.0 + wrap) * e *
                    (1.0 - e_to_suffix) / geometric_denominator;
            const double sum_q = prefix_sum + (0.5 + wrap) + suffix_sum;
            const double log_common = log_total - std::log(sum_q) -
                                      log_channel_scale;

            // Wcm ignores model bins <= 1e-12. Use the sufficient-statistic
            // score only when every bin in both channels clears that exact
            // threshold; otherwise the generic loop below preserves behavior.
            const double min_prefix_log = log_t +
                    (delta_bin - 1) * log_e + log_common;
            const double min_suffix_log = log_suffix_base +
                    suffix_length * log_e + log_common;
            const double min_parallel_log = std::min(
                    log_pulse + log_common,
                    std::min(min_prefix_log, min_suffix_log));
            const double min_model_log = std::min(
                    min_parallel_log, min_parallel_log + log_perpendicular);
            if (std::isfinite(min_model_log) &&
                min_model_log > std::log(1.0e-12)) {
                const double counts_log_q =
                        prefix_counts * log_t + prefix_index * log_e +
                        pulse_counts * log_pulse +
                        suffix_counts * log_suffix_base +
                        suffix_distance * log_e;
                const double w = counts_log_q + total * log_common +
                                 ss * log_perpendicular;
                used_sufficient_statistics = true;
                return -w / n_channels;
            }
        }

        double spectrum[2] = {1.0, std::max(tau, kMinTau)};
        fconv_per_cs(model, spectrum, const_cast<double *>(irf),
                     1, n_channels - 1, n_channels,
                     corrections[0], static_cast<int>(corrections[4]), p->dt);

        double sum_q = 0.0;
        for (int i = 0; i < n_channels; ++i) sum_q += model[i];
        const double sum_model = sum_q * (1.0 + perpendicular_scale);
        if (sum_model <= 0.0) {
            if (keep_model)
                std::fill(model, model + n_cols, 0.0);
            return 0.0;
        }

        if (total <= 0.0) {
            if (keep_model)
                std::fill(model, model + n_cols, 0.0);
            return 0.0;
        }

        // Match the generic path's two scaling operations: modelf first
        // normalizes to unit area, then normM multiplies by total counts.
        const double probability_scale = 1.0 / sum_model;
        const double log_parallel_scale = log_total - std::log(sum_model);
        const double log_perpendicular_scale = log_parallel_scale +
                                               log_perpendicular;
        constexpr double log_model_threshold = -27.631021115928547;
        double w = 0.0;
        for (int i = 0; i < n_channels; ++i) {
            const int cp = p->counts[i + n_channels];
            const int cs = p->counts[i] - cp;
            if (model[i] > 0.0) {
                const double log_q = std::log(model[i]);
                const double log_mp = log_q + log_parallel_scale;
                const double log_ms = log_q + log_perpendicular_scale;
                if (log_mp > log_model_threshold) w += cp * log_mp;
                if (log_ms > log_model_threshold) w += cs * log_ms;
            }
            if (keep_model) {
                model[i + n_channels] = ((model[i] * perpendicular_scale) *
                                         probability_scale) * total;
                model[i] = (model[i] * probability_scale) * total;
            }
        }
        return -w / n_channels;
    };

    const double hi = corrections[0] > 0.0
                          ? corrections[0]
                          : std::max(1.0, n_channels * p->dt);
    auto objective = [&](double tau) { return evaluate(tau, false); };
    const double tau = brent_minimize_tau(kMinTau, hi, objective, 1.0e-4, 100);
    const double target_at_tau = objective(tau);
    const bool entropy_identity_is_exact = used_sufficient_statistics;
    double two_istar;
    if (entropy_identity_is_exact) {
        two_istar = target_at_tau + data_entropy / n_channels;
        if (retain_model) evaluate(tau, true);
    } else {
        // Wcm skips bins whose model is <= 1e-12 whereas twoIstar does not.
        // Materialize the final model when that threshold is active so the
        // generic Fit23 reporting semantics remain exact.
        evaluate(tau, true);
        double two_istar_sum = 0.0;
        for (int i = 0; i < n_channels; ++i) {
            const int cp = p->counts[i + n_channels];
            const int cs = p->counts[i] - cp;
            if (cp > 0)
                two_istar_sum += cp * std::log(model[i] / cp);
            if (cs > 0)
                two_istar_sum += cs * std::log(model[i + n_channels] / cs);
        }
        two_istar = -two_istar_sum / n_channels;
    }
    if (retain_model) {
        for (int i = 0; i < n_channels; ++i) {
            const int cp = p->counts[i + n_channels];
            const int cs = p->counts[i] - cp;
            p->counts[i] = cp;
            p->counts[i + n_channels] = cs;
        }
    }

    const double anisotropy_denominator =
            sp * (1.0 - 3.0 * corrections[3]) +
            (2.0 - 3.0 * corrections[2]) * g * ss;
    const double anisotropy = (sp - g * ss) / anisotropy_denominator;

    out[0] = tau;
    out[1] = 0.0;
    out[2] = 0.0;
    out[3] = x0[3];
    out[4] = two_istar;
    if (n_out_cols > 5) out[5] = anisotropy;
    if (n_out_cols > 6) out[6] = anisotropy;
    return true;
}


