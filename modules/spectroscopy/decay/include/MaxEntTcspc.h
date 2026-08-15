// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file MaxEntTcspc.h
 * \brief Maximum-entropy TCSPC lifetime analysis (me_vin4_E.m analogue).
 *
 * Recovers a distribution of lifetimes P(tau) from a TCSPC decay by
 * maximum-entropy deconvolution against a shifted instrument response. The
 * objective is a quadratic MEM form:
 *
 *   Q(p) = 1/2 p^T H p - g0^T p + const - 1/2 nu * S(p)
 *
 * with S the Shannon entropy and H the (weighted) Hessian of the chi^2. Each
 * MEM iterate solves a small bound-constrained quadratic program
 *   min 1/2 x^T C x + d^T x,  x >= lb
 * by an active-set method.
 *
 * Ported from chisurf's `maxent_decay.core.solver` (_run_mem, _quadpr_bound,
 * _shift_lamp, _fconv_single_shot/_fconv_periodic, _build_Fi_lifetimes).
 */
#ifndef TTTRLIB_MAXENTTCSPC_H
#define TTTRLIB_MAXENTTCSPC_H

#include <vector>

namespace tttrlib {

/// Result of a maximum-entropy TCSPC lifetime fit.
struct MemTcspcResult {
    std::vector<double> p;        ///< recovered species amplitudes (length n_tau)
    double chisq = 0.0;
    double S = 0.0;
    double Q = 0.0;
    std::vector<double> p_esm;    ///< early-stop-maximum (ESM) amplitudes
    double chisq_esm = 0.0;
    double S_esm = 0.0;
    double Q_esm = 0.0;
    int niter = 0;
    bool success = false;
    double nu_used = 0.0;         ///< the nu the fit ran at: the given nu on the
                                  ///< fixed-nu path, the nu found by the search
                                  ///< when target_chisq > 0 was requested
    bool target_chisq_converged = true; ///< only meaningful when target_chisq > 0
                                        ///< was requested; always true otherwise
};

/*!
 * \brief Fractionally shift an IRF by ts channels (linear interpolation).
 *        Matches chisurf `_shift_lamp`.
 */
std::vector<double> tcspc_shift_lamp(
    const std::vector<double>& lamp, double ts_channels
);

/*!
 * \brief Convolve a sum of exponentials with a shifted IRF (single shot).
 *        Matches chisurf `_fconv_single_shot`.
 */
std::vector<double> tcspc_fconv_single_shot(
    const std::vector<double>& lampsh, double dt,
    const std::vector<double>& amps, const std::vector<double>& taus,
    int stop
);

/*!
 * \brief Periodic convolution (finite laser repetition). Matches
 *        chisurf `_fconv_periodic`.
 */
std::vector<double> tcspc_fconv_periodic(
    const std::vector<double>& lampsh, double dt,
    const std::vector<double>& amps, const std::vector<double>& taus,
    int start, int stop, double period
);

/*!
 * \brief Bounded quadratic program: min 1/2 x^T C x + d^T x, x >= lb.
 *        Active-set method. Matches chisurf `_quadpr_bound`.
 */
std::vector<double> tcspc_quadpr_bound(
    const std::vector<double>& C, const std::vector<double>& d,
    double lower_bound
);

/*!
 * \brief Run the MEM iteration given H, g0, m, const_chi2, nu.
 *        Matches chisurf `_run_mem`.
 */
MemTcspcResult tcspc_run_mem(
    const std::vector<double>& H, const std::vector<double>& g0,
    const std::vector<double>& m, double const_chi2, double nu,
    int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12
);

/*!
 * \brief Build the Fi design matrix, y, sigma and additive term for the
 *        lifetime MEM. Matches chisurf `_build_Fi_lifetimes`.
 *
 * \param decay measured TCSPC decay
 * \param lamp instrument response (pre-background-corrected)
 * \param dt channel width
 * \param tau lifetime grid
 * \param timeshift IRF shift in channels (fractional allowed)
 * \param background, lamp_scatter additive terms
 * \param fitstart, fitstop fit range (inclusive)
 * \param period laser period (<=0 for single shot)
 * \param Fi out (M * n_tau) row-major
 * \param y, sigma, fit_additive out (M each)
 */
void tcspc_build_fi_lifetimes(
    const std::vector<double>& decay,
    const std::vector<double>& lamp,
    double dt,
    const std::vector<double>& tau,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    std::vector<double>& Fi, std::vector<double>& y,
    std::vector<double>& sigma, std::vector<double>& fit_additive
);

/*!
 * \brief High-level maximum-entropy TCSPC lifetime fit.
 *
 * Computes H, g0, const from the decay/lamp/tau and runs the MEM iteration.
 * Matches the non-nuisance path of chisurf `solve_lifetime_mem`.
 *
 * \param decay measured decay
 * \param lamp instrument response
 * \param dt channel width
 * \param tau lifetime grid
 * \param timeshift IRF shift (channels)
 * \param background, lamp_scatter additive terms
 * \param fitstart, fitstop fit range
 * \param period laser period (<=0 single shot)
 * \param nu entropy regularisation
 * \param max_iter, tol, min_prob MEM settings
 * \param prior optional prior amplitudes (length n_tau); empty = uniform
  * \param target_chisq opt-in historic-MaxEnt mode: <= 0 (the default)
  *        disables it and the fixed `nu` above is used unchanged; > 0 finds
  *        nu automatically via `run_mem_target_chisq` so the fit's chi-square
  *        lands at this value, and `nu` instead SEEDS that search. Units:
  *        this solver's chi-square is a MEAN over the fit bins, so the
  *        classic target here is ~1.0, NOT the number of bins. Watch
  *        `MemTcspcResult::target_chisq_converged`.
  */
MemTcspcResult solve_tcspc_mem_lifetime(
    const std::vector<double>& decay,
    const std::vector<double>& lamp,
    double dt,
    const std::vector<double>& tau,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double nu = 1e-5,
    int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12,
    const std::vector<double>& prior = {},
    double target_chisq = -1.0
);

/*!
 * \brief Build the Fi design matrix, y, sigma and additive term for the
 *        DISTANCE MEM. Matches chisurf `_build_Fi_distances`.
 *
 * Column j is the donor decay quenched at the FRET rate of distance R[j]:
 * `k_FRET = (1/tau0) (R0/R)^6`, combined pairwise with the donor-only decay
 * (`e1te2`, parallel lifetimes), convolved with the shifted IRF, and mixed
 * with the unquenched donor by the donor-only fraction. This is the piece
 * that makes the inversion a distance distribution p(R_DA) rather than a
 * lifetime one.
 *
 * \param decay measured TCSPC decay
 * \param lamp instrument response (raw; `irf_background` is subtracted here)
 * \param dt channel width
 * \param R distance grid (all entries > 0)
 * \param tau0 donor lifetime without acceptor
 * \param R0 Foerster radius, same unit as R
 * \param donly donor-only reference as amplitude/tau pairs [c1,tau1,c2,tau2,..]
 * \param x_donly fraction of donor-only species (clamped to [0,1])
 * \param timeshift IRF shift in channels (fractional allowed)
 * \param background, lamp_scatter additive terms
 * \param fitstart, fitstop fit range (inclusive)
 * \param period laser period (<=0 for single shot)
 * \param irf_background counts subtracted from the IRF before shifting
 * \param Fi out (M * n_R) row-major
 * \param y, sigma, fit_additive out (M each)
 */
void tcspc_build_fi_distances(
    const std::vector<double>& decay,
    const std::vector<double>& lamp,
    double dt,
    const std::vector<double>& R,
    double tau0, double R0,
    const std::vector<double>& donly, double x_donly,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double irf_background,
    std::vector<double>& Fi, std::vector<double>& y,
    std::vector<double>& sigma, std::vector<double>& fit_additive
);

/*!
 * \brief High-level maximum-entropy TCSPC FRET fit: recovers p(R_DA).
 *
 * The distance-axis sibling of \ref solve_tcspc_mem_lifetime — the same MEM
 * engine, the design matrix built over a distance grid instead of a lifetime
 * grid. Matches the non-nuisance path of chisurf `solve_fret_mem`.
 *
 * \param decay measured decay
 * \param lamp instrument response (raw; `irf_background` subtracted here)
 * \param dt channel width
 * \param R distance grid
 * \param tau0 donor lifetime without acceptor
 * \param R0 Foerster radius, same unit as R
 * \param donly donor-only reference as amplitude/tau pairs
 * \param x_donly donor-only fraction in [0,1]
 * \param timeshift IRF shift (channels)
 * \param background, lamp_scatter additive terms
 * \param fitstart, fitstop fit range
 * \param period laser period (<=0 single shot)
 * \param irf_background counts subtracted from the IRF
 * \param nu entropy regularisation
 * \param max_iter, tol, min_prob MEM settings
 * \param prior optional prior amplitudes (length n_R); empty = uniform
  * \param target_chisq opt-in historic-MaxEnt auto-nu, exactly as on
  *        \ref solve_tcspc_mem_lifetime (mean-chi^2 units, so a classic
  *        target is ~1.0; <= 0 disables, and `nu` then applies unchanged).
  */
MemTcspcResult solve_tcspc_mem_fret(
    const std::vector<double>& decay,
    const std::vector<double>& lamp,
    double dt,
    const std::vector<double>& R,
    double tau0, double R0,
    const std::vector<double>& donly, double x_donly,
    double timeshift, double background, double lamp_scatter,
    int fitstart, int fitstop, double period,
    double irf_background = 0.0,
    double nu = 1e-5,
    int max_iter = 200, double tol = 1e-4, double min_prob = 1e-12,
    const std::vector<double>& prior = {},
    double target_chisq = -1.0
);

} // namespace tttrlib

#endif // TTTRLIB_MAXENTTCSPC_H