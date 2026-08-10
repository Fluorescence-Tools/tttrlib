// SPDX-License-Identifier: BSD-3-Clause
#include "MaxEntTcspc.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "Mat.h"

namespace tttrlib {

namespace {

constexpr double LOG_FLOOR = -1e300;

double log_clip(double v) {
    return v > 1e-300 ? std::log(v) : LOG_FLOOR;
}

} // anonymous namespace

std::vector<double> tcspc_shift_lamp(
    const std::vector<double>& lamp, double ts_channels
) {
    int n = static_cast<int>(lamp.size());
    std::vector<double> sh(n, 0.0);

    int tsint = static_cast<int>(std::floor(ts_channels));
    double tsdbl = ts_channels - static_cast<double>(tsint);

    int out_left = 0, out_right = 0;
    if (tsint < 0) out_left = -tsint;
    if (tsint + 1 > 0) out_right = tsint + 1;
    if (out_left > n) out_left = n;
    if (out_right > n) out_right = n;

    int limit = n - out_right;
    if (limit < out_left) limit = out_left;

    for (int j = out_left; j < limit; ++j) {
        int idx = j + tsint;
        if (idx < 0 || idx + 1 >= n) sh[j] = 0.0;
        else sh[j] = lamp[idx] * (1.0 - tsdbl) + lamp[idx + 1] * tsdbl;
    }
    return sh;
}

std::vector<double> tcspc_fconv_single_shot(
    const std::vector<double>& lampsh, double dt,
    const std::vector<double>& amps, const std::vector<double>& taus,
    int stop
) {
    int n_points = static_cast<int>(lampsh.size());
    if (stop >= n_points) stop = n_points - 1;
    if (stop < 1) stop = 1;

    std::vector<double> fit(n_points, 0.0);
    double deltathalf = 0.5 * dt;
    int nexp = static_cast<int>(amps.size());

    for (int k = 0; k < nexp; ++k) {
        double amp = amps[k], tau = taus[k];
        if (tau <= 0.0 || amp == 0.0) continue;
        double expcurr = std::exp(-dt / tau);
        double fitcurr = 0.0;
        for (int i = 1; i <= stop; ++i) {
            fitcurr = (fitcurr + deltathalf * lampsh[i - 1]) * expcurr
                      + deltathalf * lampsh[i];
            fit[i] += fitcurr * amp;
        }
    }
    return fit;
}

std::vector<double> tcspc_fconv_periodic(
    const std::vector<double>& lampsh, double dt,
    const std::vector<double>& amps, const std::vector<double>& taus,
    int start, int stop, double period
) {
    int n_points = static_cast<int>(lampsh.size());
    if (stop >= n_points) stop = n_points - 1;
    if (start < 0) start = 0;
    if (start > stop) start = stop;

    std::vector<double> fit(n_points, 0.0);
    if (period <= 0.0)
        return tcspc_fconv_single_shot(lampsh, dt, amps, taus, stop);

    int lamp_start = 0;
    while (lamp_start < n_points && lampsh[lamp_start] == 0.0) lamp_start++;

    int period_n = static_cast<int>(std::ceil(period / dt - 0.5));
    int stop1 = period_n + lamp_start;
    if (stop1 > n_points - 1) stop1 = n_points - 1;

    double deltathalf = 0.5 * dt;
    int nexp = static_cast<int>(amps.size());

    for (int k = 0; k < nexp; ++k) {
        double amp = amps[k], tau = taus[k];
        if (tau <= 0.0 || amp == 0.0) continue;
        double expcurr = std::exp(-dt / tau);
        double tail_a = 1.0 / (1.0 - std::exp(-period / tau));
        double fitcurr = 0.0;
        for (int i = 1; i <= stop1; ++i) {
            fitcurr = (fitcurr + deltathalf * lampsh[i - 1]) * expcurr
                      + deltathalf * lampsh[i];
            fit[i] += fitcurr * amp;
        }
        int steps_to_start = period_n - stop1 + start;
        if (steps_to_start > 0)
            fitcurr *= std::exp(-static_cast<double>(steps_to_start) * dt / tau);
        for (int i = start; i <= stop; ++i) {
            fitcurr *= expcurr;
            fit[i] += fitcurr * amp * tail_a;
        }
    }
    return fit;
}

std::vector<double> tcspc_quadpr_bound(
    const std::vector<double>& C_in, const std::vector<double>& d_in,
    double lower_bound
) {
    int n = static_cast<int>(d_in.size());
    std::vector<double> C(C_in);
    // Symmetrise
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i; ++j) {
            double s = 0.5 * (C[static_cast<size_t>(i) * n + j]
                              + C[static_cast<size_t>(j) * n + i]);
            C[static_cast<size_t>(i) * n + j] = s;
            C[static_cast<size_t>(j) * n + i] = s;
        }
    double lb = lower_bound;
    std::vector<double> x(n, 0.0);
    std::vector<char> active(n, 0);

    for (int sweep = 0; sweep < 50; ++sweep) {
        // Gather free indices
        std::vector<int> free_idx;
        for (int i = 0; i < n; ++i) if (!active[i]) free_idx.push_back(i);
        int nf = static_cast<int>(free_idx.size());
        if (nf > 0) {
            std::vector<double> Cff(static_cast<size_t>(nf) * nf);
            std::vector<double> df(nf);
            for (int a = 0; a < nf; ++a) {
                df[a] = d_in[free_idx[a]];
                for (int b = 0; b < nf; ++b)
                    Cff[static_cast<size_t>(a) * nf + b] =
                        C[static_cast<size_t>(free_idx[a]) * n + free_idx[b]];
            }
            for (int a = 0; a < nf; ++a) df[a] = -df[a];
            // mat_solve eliminates in place, so the fallback needs the system
            // as it was: copy first, or least squares gets handed a half-
            // triangularised matrix and a partly updated right-hand side.
            std::vector<double> Cff_tmp(Cff), df_tmp(df);
            if (!mat_solve(Cff, df, nf)) {
                // Near-singular free block: fall back to min-norm least squares
                // (matches numpy's np.linalg.lstsq fallback in chisurf).
                mat_lstsq_minnorm(Cff_tmp, df_tmp, nf, nf);
                df = std::move(df_tmp);
            }
            for (int a = 0; a < nf; ++a) x[free_idx[a]] = df[a];
        }
        // Enforce bound on active set
        for (int i = 0; i < n; ++i) if (active[i]) x[i] = lb;

        bool any_new = false;
        for (int i = 0; i < n; ++i) {
            if (x[i] < lb && !active[i]) { active[i] = 1; any_new = true; }
        }
        if (!any_new) break;
    }
    for (int i = 0; i < n; ++i) if (x[i] < lb) x[i] = lb;
    return x;
}

MemTcspcResult tcspc_run_mem(
    const std::vector<double>& H_in, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double nu,
    int max_iter, double tol, double min_prob
) {
    MemTcspcResult res;
    int n = static_cast<int>(m.size());
    if (n == 0) { res.success = false; return res; }

    std::vector<double> H(H_in);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < i; ++j) {
            double s = 0.5 * (H[static_cast<size_t>(i) * n + j]
                              + H[static_cast<size_t>(j) * n + i]);
            H[static_cast<size_t>(i) * n + j] = s;
            H[static_cast<size_t>(j) * n + i] = s;
        }

    // H_eps = H + diag(diag(H)*1e-12)
    std::vector<double> H_eps(H);
    for (int i = 0; i < n; ++i)
        H_eps[static_cast<size_t>(i) * n + i] +=
            H[static_cast<size_t>(i) * n + i] * 1e-12;

    std::vector<double> ng0(g0);
    for (auto& v : ng0) v = -v;
    auto p_esm = tcspc_quadpr_bound(H_eps, ng0, min_prob);

    auto quadratic = [&](const std::vector<double>& p) -> double {
        // 1/2 p^T H p - g0^T p + const, as a single Kahan-accumulated sum of
        // 0.5*p^T H p (Kahan over the outer index), minus the g0 term. Two
        // independent compensations; do NOT let one loop's comp leak into
        // another (that is a real bug: it turns a ~1e-2 fp error into an
        // order-of-magnitude one when the terms nearly cancel).
        double s = 0.0, comp = 0.0;
        for (int i = 0; i < n; ++i) {
            double hrow = 0.0;
            for (int j = 0; j < n; ++j)
                hrow += H[static_cast<size_t>(i) * n + j] * p[j];
            double y = 0.5 * p[i] * hrow - comp;
            double t = s + y;
            comp = (t - s) - y;
            s = t;
        }
        double gsum = 0.0, gcomp = 0.0;
        for (int i = 0; i < n; ++i) {
            double y = g0[i] * p[i] - gcomp;
            double t = gsum + y;
            gcomp = (t - gsum) - y;
            gsum = t;
        }
        return (s - gsum) + const_chi2;
    };
    auto entropy = [&](const std::vector<double>& p) -> double {
        double S = 0.0;
        for (int i = 0; i < n; ++i)
            S += (-log_clip(p[i] / m[i]) + 1.0) * p[i];
        return S - std::accumulate(m.begin(), m.end(), 0.0);
    };

    double chisq_esm = quadratic(p_esm);
    double S_esm = entropy(p_esm);
    double Q_esm = chisq_esm - 0.5 * nu * S_esm;

    std::vector<double> p = m;
    double chisq = quadratic(p);
    double S = entropy(p);
    double Q = chisq - 0.5 * nu * S;

    double dgrad = 1.0;
    int niter = 0;
    for (int iter = 1; iter <= max_iter; ++iter) {
        if (dgrad <= tol) break;
        niter = iter;

        // Delta_diag = 0.5 / max(p, min_prob); C_eff = H + diag(nu*Delta)
        std::vector<double> C_eff(H);
        std::vector<double> d_eff(g0);  // start as g0, then transform
        for (int i = 0; i < n; ++i) {
            double Delta = 0.5 / std::max(p[i], min_prob);
            C_eff[static_cast<size_t>(i) * n + i] += nu * Delta;
            d_eff[i] = -g0[i] + 0.5 * nu * (log_clip(p[i] / m[i]) - 1.0);
        }

        p = tcspc_quadpr_bound(C_eff, d_eff, min_prob);
        chisq = quadratic(p);
        S = entropy(p);
        Q = chisq - 0.5 * nu * S;

        // dgrad from normalised gradient difference
        std::vector<double> grad_chi2(n), grad_S(n);
        for (int i = 0; i < n; ++i) {
            double hrow = 0.0;
            for (int j = 0; j < n; ++j)
                hrow += H[static_cast<size_t>(i) * n + j] * p[j];
            grad_chi2[i] = hrow - g0[i];
            grad_S[i] = -log_clip(p[i] / m[i]);
        }
        double mask_lim = -1.1 * min_prob;
        for (int i = 0; i < n; ++i) {
            if (!(p[i] > mask_lim)) { grad_chi2[i] = 0.0; grad_S[i] = 0.0; }
        }
        double nrm_c = 0.0, nrm_s = 0.0;
        for (int i = 0; i < n; ++i) { nrm_c += grad_chi2[i] * grad_chi2[i]; nrm_s += grad_S[i] * grad_S[i]; }
        nrm_c = std::sqrt(nrm_c); nrm_s = std::sqrt(nrm_s);
        if (nrm_c == 0.0 || nrm_s == 0.0) dgrad = 0.0;
        else {
            double diff = 0.0;
            for (int i = 0; i < n; ++i) {
                double dc = grad_chi2[i] / nrm_c - grad_S[i] / nrm_s;
                diff += dc * dc;
            }
            dgrad = 0.5 * std::sqrt(diff);
        }
    }

    res.p = p;
    res.chisq = chisq;
    res.S = S;
    res.Q = Q;
    res.p_esm = p_esm;
    res.chisq_esm = chisq_esm;
    res.S_esm = S_esm;
    res.Q_esm = Q_esm;
    res.niter = niter;
    res.success = true;
    return res;
}

void tcspc_build_fi_lifetimes(
    const std::vector<double>& decay, const std::vector<double>& lamp,
    double dt, const std::vector<double>& tau,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    std::vector<double>& Fi, std::vector<double>& y,
    std::vector<double>& sigma, std::vector<double>& fit_additive
) {
    int n_decay = static_cast<int>(decay.size());
    int n_lamp = static_cast<int>(lamp.size());
    int n_tau = static_cast<int>(tau.size());
    if (fitstop >= n_decay) fitstop = n_decay - 1;
    if (fitstop >= n_lamp) fitstop = n_lamp - 1;
    if (fitstart < 0) fitstart = 0;
    if (fitstart > fitstop) fitstart = fitstop;
    int M = fitstop - fitstart + 1;

    y.assign(decay.begin() + fitstart, decay.begin() + fitstop + 1);
    sigma.resize(M);
    for (int i = 0; i < M; ++i) sigma[i] = std::sqrt(y[i]) + (y[i] == 0.0 ? 1.0 : 0.0);

    auto lampsh = tcspc_shift_lamp(lamp, timeshift);
    std::vector<double> lamp_seg(lampsh.begin() + fitstart, lampsh.begin() + fitstop + 1);

    fit_additive.resize(M);
    for (int i = 0; i < M; ++i)
        fit_additive[i] = background + lamp_scatter * lamp_seg[i];

    Fi.resize(static_cast<size_t>(M) * n_tau);
    std::vector<double> amps(1, 1.0);
    for (int j = 0; j < n_tau; ++j) {
        std::vector<double> taus(1, tau[j]);
        std::vector<double> fit_full;
        if (period > 0.0)
            fit_full = tcspc_fconv_periodic(lampsh, dt, amps, taus, fitstart, fitstop, period);
        else
            fit_full = tcspc_fconv_single_shot(lampsh, dt, amps, taus, fitstop);
        for (int i = 0; i < M; ++i)
            Fi[static_cast<size_t>(i) * n_tau + j] = fit_full[fitstart + i] / sigma[i];
    }
}

// The engine both axes share: design matrix in, MEM distribution out. The
// lifetime and the distance solve differ ONLY in how Fi was built, which is
// the point of having one of these -- a second copy of the H/g0 assembly is
// how the two would drift.
static MemTcspcResult run_mem_from_design(
    const std::vector<double>& Fi, const std::vector<double>& y,
    const std::vector<double>& sigma, const std::vector<double>& fit_additive,
    int n, double nu, int max_iter, double tol, double min_prob,
    const std::vector<double>& prior
) {
    MemTcspcResult res;
    int M = static_cast<int>(y.size());

    // y_eff = y - fit_additive; y_w = y_eff / sigma
    std::vector<double> y_w(M);
    for (int i = 0; i < M; ++i) y_w[i] = (y[i] - fit_additive[i]) / sigma[i];

    // H = 2/M Fi.T Fi ; g0 = 2/M Fi.T y_w ; const = sum(y_w^2)/M
    std::vector<double> H(static_cast<size_t>(n) * n, 0.0);
    std::vector<double> g0(n, 0.0);
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b) {
            double s = 0.0;
            for (int i = 0; i < M; ++i)
                s += Fi[static_cast<size_t>(i) * n + a]
                   * Fi[static_cast<size_t>(i) * n + b];
            H[static_cast<size_t>(a) * n + b] = (2.0 / M) * s;
        }
    for (int a = 0; a < n; ++a) {
        double s = 0.0;
        for (int i = 0; i < M; ++i)
            s += y_w[i] * Fi[static_cast<size_t>(i) * n + a];
        g0[a] = (2.0 / M) * s;
    }
    double const_chi2 = 0.0;
    for (int i = 0; i < M; ++i) const_chi2 += y_w[i] * y_w[i];
    const_chi2 /= M;

    // Prior
    std::vector<double> m(n, 1.0);
    if (!prior.empty()) {
        if (static_cast<int>(prior.size()) != n) { res.success = false; return res; }
        for (int i = 0; i < n; ++i)
            m[i] = prior[i] <= 0.0 ? min_prob : prior[i];
    }
    double msum = std::accumulate(m.begin(), m.end(), 0.0);
    for (auto& v : m) v /= msum;

    return tcspc_run_mem(H, g0, m, const_chi2, nu, max_iter, tol, min_prob);
}

MemTcspcResult solve_tcspc_mem_lifetime(
    const std::vector<double>& decay, const std::vector<double>& lamp,
    double dt, const std::vector<double>& tau,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double nu, int max_iter, double tol, double min_prob,
    const std::vector<double>& prior
) {
    MemTcspcResult res;
    int n_tau = static_cast<int>(tau.size());
    if (n_tau == 0) { res.success = false; return res; }

    std::vector<double> Fi, y, sigma, fit_additive;
    tcspc_build_fi_lifetimes(decay, lamp, dt, tau, timeshift, background,
                             lamp_scatter, fitstart, fitstop, period,
                             Fi, y, sigma, fit_additive);
    return run_mem_from_design(Fi, y, sigma, fit_additive, n_tau,
                               nu, max_iter, tol, min_prob, prior);
}

// gfit e1te2.m: all pairwise products of two [c,tau,...] sets, lifetimes
// combined in parallel (1/tau = 1/tau1 + 1/tau2).
static void e1te2_pairs(
    const std::vector<double>& e1, const std::vector<double>& e2,
    std::vector<double>& amps, std::vector<double>& taus
) {
    const size_t n1 = e1.size() / 2, n2 = e2.size() / 2;
    amps.clear(); taus.clear();
    amps.reserve(n1 * n2); taus.reserve(n1 * n2);
    for (size_t i = 0; i < n1; ++i)
        for (size_t j = 0; j < n2; ++j) {
            amps.push_back(e1[2 * i] * e2[2 * j]);
            taus.push_back(1.0 / (1.0 / e1[2 * i + 1] + 1.0 / e2[2 * j + 1]));
        }
}

void tcspc_build_fi_distances(
    const std::vector<double>& decay, const std::vector<double>& lamp,
    double dt, const std::vector<double>& R,
    double tau0, double R0,
    const std::vector<double>& donly, double x_donly,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double irf_background,
    std::vector<double>& Fi, std::vector<double>& y,
    std::vector<double>& sigma, std::vector<double>& fit_additive
) {
    if (decay.empty() || lamp.empty() || R.empty() || donly.empty())
        throw std::invalid_argument("decay, lamp, R and donly must be non-empty");
    if (donly.size() % 2 != 0)
        throw std::invalid_argument("donly must contain amplitude/tau pairs");

    int n_decay = static_cast<int>(decay.size());
    int n_lamp = static_cast<int>(lamp.size());
    int n_R = static_cast<int>(R.size());
    if (fitstop >= n_decay) fitstop = n_decay - 1;
    if (fitstop >= n_lamp) fitstop = n_lamp - 1;
    if (fitstart < 0) fitstart = 0;
    if (fitstart > fitstop) fitstart = fitstop;
    int M = fitstop - fitstart + 1;

    y.assign(decay.begin() + fitstart, decay.begin() + fitstop + 1);
    sigma.resize(M);
    for (int i = 0; i < M; ++i) sigma[i] = std::sqrt(y[i]) + (y[i] == 0.0 ? 1.0 : 0.0);

    // The IRF arrives raw here, unlike the lifetime builder: the caller states
    // its baseline and it is removed before the shift, as chisurf does.
    std::vector<double> lamp_corr(lamp);
    for (auto& v : lamp_corr) v = std::max(v - irf_background, 0.0);
    auto lampsh = tcspc_shift_lamp(lamp_corr, timeshift);

    std::vector<double> amps_donly, taus_donly;
    for (size_t k = 0; k + 1 < donly.size(); k += 2) {
        amps_donly.push_back(donly[k]);
        taus_donly.push_back(donly[k + 1]);
    }
    std::vector<double> donor_full;
    if (period > 0.0)
        donor_full = tcspc_fconv_periodic(lampsh, dt, amps_donly, taus_donly,
                                          fitstart, fitstop, period);
    else
        donor_full = tcspc_fconv_single_shot(lampsh, dt, amps_donly, taus_donly,
                                             fitstop);

    double x_d = x_donly < 0.0 ? 0.0 : (x_donly > 1.0 ? 1.0 : x_donly);
    double x_fret = 1.0 - x_d;

    fit_additive.resize(M);
    for (int i = 0; i < M; ++i)
        fit_additive[i] = background + lamp_scatter * lampsh[fitstart + i];

    Fi.resize(static_cast<size_t>(M) * n_R);
    std::vector<double> e2(2), amps, taus, fit_full;
    for (int j = 0; j < n_R; ++j) {
        if (R[j] <= 0.0) throw std::invalid_argument("R grid must be positive");
        const double kfret = (1.0 / tau0) * std::pow(R0 / R[j], 6.0);
        e2[0] = 1.0; e2[1] = 1.0 / kfret;
        e1te2_pairs(donly, e2, amps, taus);
        if (period > 0.0)
            fit_full = tcspc_fconv_periodic(lampsh, dt, amps, taus,
                                            fitstart, fitstop, period);
        else
            fit_full = tcspc_fconv_single_shot(lampsh, dt, amps, taus, fitstop);
        for (int i = 0; i < M; ++i)
            Fi[static_cast<size_t>(i) * n_R + j] =
                (x_fret * fit_full[fitstart + i] + x_d * donor_full[fitstart + i])
                / sigma[i];
    }
}

MemTcspcResult solve_tcspc_mem_fret(
    const std::vector<double>& decay, const std::vector<double>& lamp,
    double dt, const std::vector<double>& R,
    double tau0, double R0,
    const std::vector<double>& donly, double x_donly,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double irf_background,
    double nu, int max_iter, double tol, double min_prob,
    const std::vector<double>& prior
) {
    MemTcspcResult res;
    int n_R = static_cast<int>(R.size());
    if (n_R == 0) { res.success = false; return res; }

    std::vector<double> Fi, y, sigma, fit_additive;
    tcspc_build_fi_distances(decay, lamp, dt, R, tau0, R0, donly, x_donly,
                             timeshift, background, lamp_scatter,
                             fitstart, fitstop, period, irf_background,
                             Fi, y, sigma, fit_additive);
    return run_mem_from_design(Fi, y, sigma, fit_additive, n_R,
                               nu, max_iter, tol, min_prob, prior);
}

} // namespace tttrlib