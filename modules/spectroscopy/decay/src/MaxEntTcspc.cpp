// SPDX-License-Identifier: BSD-3-Clause
#include "MaxEntTcspc.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "Mat.h"
#include "MaxEntQp.h"

namespace tttrlib {

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

// Thin wrapper: the active-set bounded QP is now the shared engine in
// modules/math (MaxEntQp.h) -- see that file for why, and for the docstring
// this used to carry. Kept as a separate name/entry point for API stability
// (this is public, SWIG-exposed, and test_maxent_tcspc.py::TestTcspcQuadpr
// calls it directly), not because the algorithm differs.
std::vector<double> tcspc_quadpr_bound(
    const std::vector<double>& C, const std::vector<double>& d,
    double lower_bound
) {
    return quadpr_bound(C, d, lower_bound);
}

// Thin wrapper around the shared MEM engine (MaxEntQp.h) -- same reason as
// tcspc_quadpr_bound above. MemTcspcResult and MaxEntResult have identical
// fields; this just copies between the two names so every existing caller
// (test_maxent_tcspc.py, solve_tcspc_mem_lifetime/_fret below) is unaffected.
MemTcspcResult tcspc_run_mem(
    const std::vector<double>& H, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double nu,
    int max_iter, double tol, double min_prob
) {
    const MaxEntResult r = run_mem(H, g0, m, const_chi2, nu, max_iter, tol, min_prob);
    MemTcspcResult res;
    res.p = r.p;
    res.chisq = r.chisq;
    res.S = r.S;
    res.Q = r.Q;
    res.p_esm = r.p_esm;
    res.chisq_esm = r.chisq_esm;
    res.S_esm = r.S_esm;
    res.Q_esm = r.Q_esm;
    res.niter = r.niter;
    res.success = r.success;
    res.nu_used = nu;
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
    const std::vector<double>& prior,
    double target_chisq = -1.0
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

    if (target_chisq <= 0.0) {
        MemTcspcResult r = tcspc_run_mem(H, g0, m, const_chi2, nu,
                                         max_iter, tol, min_prob);
        r.nu_used = nu;
        return r;
    }

    // Historic-MaxEnt auto-nu: the caller's `nu` seeds the joint controller
    // (MaxEntQp.h), which finds the nu whose chi-square lands at target_chisq.
    // The controller cap is floored: max_iter is sized for ONE fixed-nu MEM
    // solve (~20 converged iterations on real fixtures), while the joint
    // interleaving of nu moves needs several hundred (measured 647 on the
    // lifetime fixture), so a fixed-nu-appropriate cap would silently
    // under-run the search.
    const MemTargetChisqResult tr = run_mem_target_chisq(
        H, g0, m, const_chi2, target_chisq,
        /*nu0=*/nu, /*max_iter=*/std::max(max_iter, 1000),
        /*chisq_tol=*/1e-2, tol, min_prob);
    MemTcspcResult r;
    r.p = tr.result.p;
    r.chisq = tr.result.chisq;
    r.S = tr.result.S;
    r.Q = tr.result.Q;
    r.p_esm = tr.result.p_esm;
    r.chisq_esm = tr.result.chisq_esm;
    r.S_esm = tr.result.S_esm;
    r.Q_esm = tr.result.Q_esm;
    r.niter = tr.result.niter;
    r.success = tr.result.success;
    r.nu_used = tr.nu;
    r.target_chisq_converged = tr.converged;
    return r;
}

MemTcspcResult solve_tcspc_mem_lifetime(
    const std::vector<double>& decay, const std::vector<double>& lamp,
    double dt, const std::vector<double>& tau,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double nu, int max_iter, double tol, double min_prob,
    const std::vector<double>& prior,
    double target_chisq
) {
    MemTcspcResult res;
    int n_tau = static_cast<int>(tau.size());
    if (n_tau == 0) { res.success = false; return res; }

    std::vector<double> Fi, y, sigma, fit_additive;
    tcspc_build_fi_lifetimes(decay, lamp, dt, tau, timeshift, background,
                             lamp_scatter, fitstart, fitstop, period,
                             Fi, y, sigma, fit_additive);
    return run_mem_from_design(Fi, y, sigma, fit_additive, n_tau,
                               nu, max_iter, tol, min_prob, prior,
                               target_chisq);
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
    const std::vector<double>& prior,
    double target_chisq
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
                               nu, max_iter, tol, min_prob, prior,
                               target_chisq);
}

} // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kTcspcMaxentEntry = R"JSON({
  "name": "tcspc_maxent",
  "label": "Maximum-entropy lifetime / distance distributions from TCSPC",
  "summary": "Fits a TCSPC decay with a maximum-entropy distribution over lifetimes, or over donor-acceptor distances (FRET), reconvolved with the lamp.",
  "description": "Brochon's MEM for time-resolved fluorescence: the decay is a superposition over a lifetime grid (or a distance grid mapped through the Förster relation, with a donor-only fraction) convolved with the measured lamp, shifted and scaled; the amplitude distribution is the maximum-entropy solution at the chi-squared target. Design matrices are exposed (`tcspc_build_fi_*`) so the same solver serves both parameterisations. Assumes a periodic excitation of known period and a lamp measured at the same settings.",
  "operation_type": "tcspc_fitting",
  "method": "solve_tcspc_mem_lifetime",
  "params_schema": {
    "type": "object",
    "properties": {
      "nu": {
        "type": "number",
        "title": "Entropy weight",
        "default": 1e-05
      },
      "max_iter": {
        "type": "integer",
        "title": "Max iterations",
        "default": 200
      },
      "tol": {
        "type": "number",
        "title": "Tolerance",
        "default": 0.0001
      },
      "period": {
        "type": "number",
        "title": "Period (ns)"
      },
      "timeshift": {
        "type": "number",
        "title": "Lamp shift (channels)",
        "default": 0.0
      }
    }
  },
  "inputs": {
    "required": [
      "decay_histogram",
      "irf"
    ]
  },
  "outputs": {
    "columns": [
      "amplitude_distribution",
      "chi2"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "journal",
      "authors": "Brochon, J.-C.",
      "title": "Maximum entropy method of data analysis in time-resolved spectroscopy",
      "journal": "Methods Enzymol",
      "year": 1994,
      "volume": "240",
      "pages": "262-311"
    },
    {
      "type": "journal",
      "authors": "Skilling, J., Bryan, R. K.",
      "title": "Maximum entropy image reconstruction: general algorithm",
      "journal": "Mon Not R Astron Soc",
      "year": 1984,
      "volume": "211",
      "pages": "111-124"
    }
  ],
  "api": [
    "solve_tcspc_mem_lifetime",
    "solve_tcspc_mem_fret",
    "tcspc_run_mem",
    "tcspc_build_fi_lifetimes",
    "tcspc_build_fi_distances",
    "tcspc_fconv_periodic",
    "tcspc_fconv_single_shot",
    "tcspc_shift_lamp",
    "tcspc_quadpr_bound",
    "MemTcspcResult"
  ],
  "can_replay": true
})JSON";
bool register_maxenttcspc_entries() {
    tttrlib::register_algorithm_json("decay", "tcspc_maxent", kTcspcMaxentEntry);
    return true;
}
const bool kMaxEntTcspcRegistered = register_maxenttcspc_entries();
}  // namespace
