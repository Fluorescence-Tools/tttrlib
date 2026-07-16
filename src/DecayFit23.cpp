// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFit23.h"
#include "include/Verbose.h"

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

inline Decay23Parameters sanitise_parameters(const double *param) {
    Decay23Parameters result{};
    result.tau = param[0] < kMinTau ? kMinTau : param[0];
    result.gamma = clamp_value(param[1], kMinGamma, kMaxGamma);
    result.r0 = param[2];
    result.rho = param[3] < kMinRho ? kMinRho : param[3];
    return result;
}

inline void apply_corrections(double *corrections) {
    fit_corrections.period = corrections[0];
    fit_corrections.g = corrections[1];
    fit_corrections.l1 = corrections[2];
    fit_corrections.l2 = corrections[3];
    fit_corrections.convolution_stop = static_cast<int>(corrections[4]);
}

unsigned int fit23_batch_threads(int n_rows) {
    if (n_rows < 1024) return 1;

    const char *enabled = std::getenv("TTTRLIB_USE_OPENMP");
    if (enabled != nullptr &&
        (enabled[0] == '0' || enabled[0] == 'f' || enabled[0] == 'F' ||
         enabled[0] == 'n' || enabled[0] == 'N')) {
        return 1;
    }

    unsigned int requested = 0;
    const char *thread_variables[] = {"TTTRLIB_NUM_THREADS", "OMP_NUM_THREADS"};
    for (const char *name : thread_variables) {
        const char *value = std::getenv(name);
        if (value != nullptr) {
            const int parsed = std::atoi(value);
            if (parsed > 0) {
                requested = static_cast<unsigned int>(parsed);
                break;
            }
        }
    }
    if (requested == 0) requested = 4;
    return std::max(1u, std::min(requested,
                                static_cast<unsigned int>(n_rows / 512)));
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
    fit_settings.penalty = (x[0] < kMinTau) ? -x[0] : 0.;

    fit_corrections.set_gamma(initial.gamma);
    apply_corrections(corrections);

    xm[1] = initial.gamma;
    xm[2] = initial.r0;

    double rho_value = initial.rho;
    if (!fit_settings.fixedrho) {
        rho_value = fit_signals.rho(xm[0], xm[2]);
        x[3] = rho_value;
    }
    if (rho_value < kMinRho) {
        rho_value = kMinRho;
    }
    xm[3] = rho_value;

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
    DecayFitData *p = (DecayFitData *) pv;

    int *expdata = p->data.data();
    int Nchannels = p->n_channels();
    double *irf = p->irf.data(), *bg = p->background.data(),
            *corrections = p->corrections.data(), *M = p->model.data();
    DecayFit23::correct_input(x, xm, corrections, 0);
    // Sp/Ss/B are data-only and constant during a fit (fit() computes them once
    // before the optimiser runs); only Bexpected, used by the soft-BIFL term
    // below, depends on the parameters. So recompute the signal integrals here
    // only when soft-BIFL is active — otherwise reuse the cached values and save
    // an O(2*Nchannels) pass on every objective evaluation.
    if (fit_settings.softbifl)
        fit_signals.compute_signal_and_background(p);

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
    std::cout << p->str();
    std::cout << "score:"  << v << std::endl;
}
    return v;
}


double DecayFit23::fit(double *x, short *fixed, DecayFitData *p) {
    double tIstar, xm[8];
    int info = -1;

    if (fit_settings.firstcall) init_fact();

    fit_settings.firstcall = 0;
    fit_settings.softbifl = (x[4] < 0.);
    fit_settings.p2s_twoIstar = (x[5] > 0.);
    fit_signals.corrections = &fit_corrections;
    fit_settings.fixedrho = fixed[3];

    double *corrections = p->corrections.data(), *M = p->model.data();
    fit_signals.compute_signal_and_background(p);
    correct_input(x, xm, corrections, 1);

    int *expdata = p->data.data();
    int Nchannels = p->n_channels();

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
        x[0] = brent_minimize_tau(kMinTau, hi, feval, 1.0e-4, 100);
        feval(x[0]);  // leave p->model evaluated at the minimizer
        info = 1;
    } else {
        bfgs bfgs_o(DecayFit23::targetf, 4);

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
        DecayFitData *p,
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
        static_cast<int>(p->irf.size()) != n_cols ||
        static_cast<int>(p->background.size()) != n_cols ||
        p->corrections.size() < 5) {
        return false;
    }

    const int n_channels = n_cols / 2;
    const double *corrections = p->corrections.data();
    const double g = corrections[1];
    if (!std::isfinite(g) || g <= 0.0 ||
        !std::equal(p->irf.begin(), p->irf.begin() + n_channels,
                    p->irf.begin() + n_channels)) {
        return false;
    }
    // The generic model evaluates bg[i] * gamma even for gamma == 0. Avoid
    // changing its NaN semantics for non-finite background inputs.
    for (double value : p->background) {
        if (!std::isfinite(value)) return false;
    }

    p->data.resize(static_cast<size_t>(n_cols));
    p->model.resize(static_cast<size_t>(n_cols), 0.0);

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
        p->data[i] = cp + cs;
        p->data[i + n_channels] = cp;
        sp += cp;
        ss += cs;
        data_entropy += count_entropy(cp) + count_entropy(cs);
    }
    const double total = sp + ss;
    const double perpendicular_scale = 1.0 / g;
    const double log_total = total > 0.0 ? std::log(total) : 0.0;
    const double log_perpendicular = std::log(perpendicular_scale);
    const double log_channel_scale = std::log1p(perpendicular_scale);
    double *model = p->model.data();
    const double *irf = p->irf.data();

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
            const double counts = p->data[i];
            prefix_counts += counts;
            prefix_index += counts * i;
        }
        pulse_counts = p->data[delta_bin];
        for (int i = delta_bin + 1; i < n_channels; ++i) {
            const double counts = p->data[i];
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
            const int cp = p->data[i + n_channels];
            const int cs = p->data[i] - cp;
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
            const int cp = p->data[i + n_channels];
            const int cs = p->data[i] - cp;
            if (cp > 0)
                two_istar_sum += cp * std::log(model[i] / cp);
            if (cs > 0)
                two_istar_sum += cs * std::log(model[i + n_channels] / cs);
        }
        two_istar = -two_istar_sum / n_channels;
    }
    if (retain_model) {
        for (int i = 0; i < n_channels; ++i) {
            const int cp = p->data[i + n_channels];
            const int cs = p->data[i] - cp;
            p->data[i] = cp;
            p->data[i + n_channels] = cs;
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


void DecayFit23::fit_matrix(
        double *data_in,
        int n_rows,
        int n_cols,
        double *x0,
        int n_x0,
        short *fixed_in,
        int n_fixed,
        double bifl_scatter,
        double p2s_flag,
        DecayFitData *p,
        double *out,
        int n_out_rows,
        int n_out_cols) {
    if (data_in == nullptr || x0 == nullptr || fixed_in == nullptr ||
        p == nullptr || out == nullptr || n_rows < 0 || n_cols <= 0 ||
        n_x0 < 4 || n_fixed < 4 || n_out_rows != n_rows || n_out_cols < 5) {
        return;
    }

    std::vector<short> fixed(fixed_in, fixed_in + n_fixed);
    if (fixed.size() < 6) fixed.resize(6, 0);

    const unsigned int n_threads = fit23_batch_threads(n_rows);
    std::vector<DecayFitData> workspaces(n_threads, *p);
    for (DecayFitData &workspace : workspaces) {
        workspace.data.resize(static_cast<size_t>(n_cols));
        workspace.model.resize(static_cast<size_t>(n_cols), 0.0);
    }

    auto fit_range = [&](unsigned int worker, int begin, int end) {
        DecayFitData *workspace = &workspaces[worker];
        for (int i = begin; i < end; ++i) {
            const double *row = data_in + static_cast<size_t>(i) * n_cols;
            double *result = out + static_cast<size_t>(i) * n_out_cols;
            if (fit_tau_only_unpolarized_row(
                    row, n_cols, x0, n_x0, fixed.data(),
                    static_cast<int>(fixed.size()), bifl_scatter, p2s_flag,
                    workspace, result, n_out_cols, i == n_rows - 1)) {
                continue;
            }

            for (int channel = 0; channel < n_cols; ++channel)
                workspace->data[channel] = static_cast<int>(row[channel]);
            double x[8] = {x0[0], x0[1], x0[2], x0[3],
                           bifl_scatter, p2s_flag, 0.0, 0.0};
            const double two_istar = fit(x, fixed.data(), workspace);
            result[0] = x[0];
            result[1] = x[1];
            result[2] = x[2];
            result[3] = x[3];
            result[4] = two_istar;
            if (n_out_cols > 5) result[5] = x[6];
            if (n_out_cols > 6) result[6] = x[7];
        }
    };

    if (n_threads == 1) {
        fit_range(0, 0, n_rows);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(n_threads);
        const int block = (n_rows + static_cast<int>(n_threads) - 1) /
                          static_cast<int>(n_threads);
        for (unsigned int worker = 0; worker < n_threads; ++worker) {
            const int begin = static_cast<int>(worker) * block;
            const int end = std::min(n_rows, begin + block);
            workers.emplace_back(fit_range, worker, begin, end);
        }
        for (std::thread &worker : workers) worker.join();
    }

    // Preserve the historical postcondition that the caller's container holds
    // the final row and model after a batch fit.
    if (n_rows > 0) {
        p->data = workspaces.back().data;
        p->model = workspaces.back().model;
    }
}


std::string DecayFit23::fit_to_json(const double *x,
                                   const short *fixed,
                                   const DecayFitData *p,
                                   double result) {
    json j;

    if (x != nullptr) {
        j["parameters"] = json::array();
        for (int i = 0; i < 8; i++) {
            j["parameters"].push_back(x[i]);
        }
    }

    if (fixed != nullptr) {
        j["fixed"] = json::array();
        for (int i = 0; i < 4; i++) {
            j["fixed"].push_back(static_cast<int>(fixed[i]));
        }
    }

    j["result"] = result;

    if (p != nullptr) {
        json jp;
        jp["dt"] = p->dt;
        jp["data_length"] = static_cast<int>(p->data.size());
        {
            json jcorr = json::array();
            for (double v: p->corrections) jcorr.push_back(v);
            jp["corrections"] = jcorr;
        }
        jp["irf_length"] = static_cast<int>(p->irf.size());
        jp["background_length"] = static_cast<int>(p->background.size());
        j["mparam"] = jp;
    }

    return j.dump();
}


std::string DecayFit23::to_json(const double *x,
                               const short *fixed,
                               const DecayFitData *p,
                               double result) {
    return fit_to_json(x, fixed, p, result);
}


void DecayFit23::from_json(const json &j,
                          double *x,
                          short *fixed) {
    if (j.contains("parameters") && j.at("parameters").is_array()) {
        const auto &params = j.at("parameters");
        for (int i = 0; i < std::min(8, static_cast<int>(params.size())); ++i) {
            x[i] = params.at(i);
        }
    }

    if (j.contains("fixed") && j.at("fixed").is_array()) {
        const auto &fixed_arr = j.at("fixed");
        for (int i = 0; i < std::min(4, static_cast<int>(fixed_arr.size())); ++i) {
            fixed[i] = static_cast<short>(fixed_arr.at(i));
        }
    }
}


std::string DecayFit23::modelf_to_json(const double *param,
                                      const double *irf,
                                      const double *bg,
                                      int Nchannels,
                                      double dt,
                                      const double *corrections,
                                      const double *mfunction,
                                      int result) {
    json j;

    if (param != nullptr) {
        j["parameters"] = json::array();
        for (int i = 0; i < 4; i++) {
            j["parameters"].push_back(param[i]);
        }
    }

    if (corrections != nullptr) {
        j["corrections"] = json::array();
        for (int i = 0; i < 5; i++) {
            j["corrections"].push_back(corrections[i]);
        }
    }

    j["Nchannels"] = Nchannels;
    j["dt"] = dt;
    j["result"] = result;

    if (irf != nullptr) {
        json jirf = json::array();
        for (int i = 0; i < 2 * Nchannels; ++i) {
            jirf.push_back(irf[i]);
        }
        j["irf"] = jirf;
    }

    if (bg != nullptr) {
        json jbg = json::array();
        for (int i = 0; i < 2 * Nchannels; ++i) {
            jbg.push_back(bg[i]);
        }
        j["background"] = jbg;
    }

    if (mfunction != nullptr) {
        json jm = json::array();
        for (int i = 0; i < 2 * Nchannels; ++i) {
            jm.push_back(mfunction[i]);
        }
        j["model"] = jm;
    }

    return j.dump();
}
