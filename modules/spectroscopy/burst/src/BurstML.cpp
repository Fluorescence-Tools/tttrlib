// SPDX-License-Identifier: BSD-3-Clause
#include "BurstML.h"

#include "NelderMead.h"
#include "QREigen.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tttrlib {

using cdouble = std::complex<double>;

// ===========================================================================
// Public API
// ===========================================================================

void BurstML::set_burst_data(
    const std::vector<double>& times,
    const std::vector<int32_t>& colours,
    const std::vector<int64_t>& offsets
) {
    times_   = times;
    colours_ = colours;
    offsets_ = offsets;
}

int BurstML::n_bursts() const {
    return offsets_.size() > 1 ? static_cast<int>(offsets_.size()) - 1 : 0;
}

int BurstML::n_photons() const {
    return static_cast<int>(times_.size());
}

double BurstML::neg_log_likelihood(const std::vector<double>& params) const {
    double ll = compute_log_likelihood(params);
    return -ll;
}

BurstML::FitResult BurstML::fit(
    const std::vector<double>& init_params,
    const std::vector<double>& lower_bounds,
    const std::vector<double>& upper_bounds,
    int n_states,
    int n_colours,
    int jmax,
    double qmax,
    double t_th,
    double n_th
) {
    n_states_  = n_states;
    n_colours_ = n_colours;
    jmax_      = jmax;
    qmax_      = qmax;
    t_th_      = t_th;
    n_th_      = n_th;

    // initial simplex step sizes: 10% of range, or 0.1 if degenerate
    int np = static_cast<int>(init_params.size());
    std::vector<double> init_step(np);
    for (int i = 0; i < np; ++i) {
        double range = upper_bounds[i] - lower_bounds[i];
        init_step[i] = std::max(range * 0.1, 0.01);
    }

    auto objective = [this](const std::vector<double>& p) -> double {
        return this->neg_log_likelihood(p);
    };

    auto result = nelder_mead(
        objective, init_params, lower_bounds, upper_bounds, init_step,
        2000, 1e-3, 1e-6
    );

    FitResult fr;
    fr.params = result.x;
    fr.log_likelihood = -result.fval;
    fr.iterations = result.iterations;
    fr.status = result.status;

    // BIC
    int total_ph = n_photons();
    if (total_ph > 0) {
        fr.bic = -2.0 * fr.log_likelihood + static_cast<double>(np) * std::log(static_cast<double>(total_ph));
    } else {
        fr.bic = std::numeric_limits<double>::quiet_NaN();
    }

    return fr;
}

// ===========================================================================
// Core likelihood — port of mlhDiffNTRbkg_MT.cpp analysisThread()
// ===========================================================================

double BurstML::compute_log_likelihood(const std::vector<double>& params) const {
    const int numSt = n_states_;
    const int numC  = n_colours_;
    const int numS  = jmax_;
    const int numS2 = numSt * numS;
    const double t_th = t_th_;
    const double N_th = n_th_;
    const double dq = qmax_ / numS;

    if (numS2 < 1) return -std::numeric_limits<double>::infinity();

    // ---- parse params (layout: n0, tau, k, f, E, bkg) ----
    std::vector<double> n0(numSt), tau(numSt), ratesumn(numSt - 1), frn(numSt - 1);
    std::vector<double> effn(numC > 1 ? (numC - 1) * numSt : 0);
    std::vector<double> bkgcnt(numC);

    for (int i = 0; i < numSt; ++i) {
        n0[i]  = params[i];
        tau[i] = params[i + numSt];
    }
    for (int i = 0; i < numSt - 1; ++i) {
        ratesumn[i] = params[i + 2 * numSt];
        frn[i]      = params[i - 1 + 3 * numSt];
    }
    for (int i = 0; i < numSt; ++i) {
        for (int j = 0; j < numC - 1; ++j)
            effn[i + numSt * j] = params[i - 2 + (4 + j) * numSt];
    }
    for (int i = 0; i < numC; ++i) {
        bkgcnt[i] = params[i - 2 + (3 + numC) * numSt];
    }

    // guard: tau > 0, n0 > 0
    for (int i = 0; i < numSt; ++i) {
        if (tau[i] <= 0.0 || n0[i] <= 0.0)
            return -std::numeric_limits<double>::infinity();
    }

    double bkgcnttot = 0.0;
    for (int i = 0; i < numC; ++i) bkgcnttot += bkgcnt[i];

    // ---- population vector peq from fractions ----
    std::vector<double> peq(numSt);
    double frnprod = 1.0;
    for (int i = 0; i < numSt - 1; ++i) {
        peq[i] = frn[i] * frnprod;
        frnprod *= (1.0 - frn[i]);
    }
    peq[numSt - 1] = frnprod;

    // ---- diffusion operator Lmat0 (numS × numS tridiagonal) ----
    std::vector<double> qplus(numS), qaxis(numS), qaxis2(numS);
    for (int i = 0; i < numS; ++i) {
        qplus[i]  = (i + 1) * dq;
        qaxis[i]  = qplus[i] - dq / 2.0;
        qaxis2[i] = qaxis[i] * qaxis[i];
    }

    // Lmat0: discretised radial Laplacian. FRET_burstML builds it through
    // gsl_matrix_set(row, col): for an interior column i BOTH off-diagonals
    // share the qaxis[i]^2 denominator. Getting this the transpose of the
    // reference swaps the model for its adjoint and inverts the likelihood
    // ordering, so the indexing here must mirror the MEX source exactly.
    std::vector<double> Lmat0(numS * numS, 0.0);
    for (int i = 1; i < numS - 1; ++i) {
        double up   = qplus[i - 1] * qplus[i - 1] / (qaxis[i] * qaxis[i]);
        double down = qplus[i] * qplus[i] / (qaxis[i] * qaxis[i]);
        Lmat0[(i - 1) * numS + i] = up;
        Lmat0[(i + 1) * numS + i] = down;
        Lmat0[i * numS + i]       = -(up + down);
    }
    // boundary: q=0 (reflecting)
    {
        double b = qplus[0] * qplus[0] / (qaxis[0] * qaxis[0]);
        Lmat0[1 * numS + 0] = b;
        Lmat0[0 * numS + 0] = -b;
    }
    // boundary: q=qmax (reflecting)
    {
        double b = qplus[numS - 2] * qplus[numS - 2] / (qaxis[numS - 1] * qaxis[numS - 1]);
        Lmat0[(numS - 2) * numS + (numS - 1)] = b;
        Lmat0[(numS - 1) * numS + (numS - 1)] = -b;
    }
    // scale by 1/dq^2
    double inv_dq2 = 1.0 / (dq * dq);
    for (auto& v : Lmat0) v *= inv_dq2;

    // ---- detection profile Pmat (diagonal): exp(-2*q^2) ----
    std::vector<double> Pmat(numS * numS, 0.0);
    for (int j = 0; j < numS; ++j)
        Pmat[j * numS + j] = std::exp(-2.0 * qaxis2[j]);

    // ---- kinetic rate matrix ratemat0 (numSt × numSt) ----
    std::vector<double> ratemat0(numSt * numSt, 0.0);
    for (int i = 0; i < numSt - 1; ++i) {
        double pi  = peq[i];
        double pi1 = peq[i + 1];
        double norm = pi + pi1;
        if (norm <= 0.0) norm = 1.0;
        // 2-state sub-rate-matrix scaled by ratesumn[i]
        double k_fwd = pi1 / norm * ratesumn[i];   // i -> i+1
        double k_bwd = pi  / norm * ratesumn[i];   // i+1 -> i
        ratemat0[i * numSt + i]           += -k_fwd;
        ratemat0[i * numSt + (i + 1)]     += k_bwd;
        ratemat0[(i + 1) * numSt + i]     += k_fwd;
        ratemat0[(i + 1) * numSt + (i + 1)] += -k_bwd;
    }

    // ---- taumat (numSt × numSt diagonal): 1/tau ----
    std::vector<double> taumat(numSt * numSt, 0.0);
    for (int i = 0; i < numSt; ++i)
        taumat[i * numSt + i] = 1.0 / tau[i];

    // ---- NmatR: diagonal photon count matrix (numS2 × numS2) ----
    std::vector<double> NmatR(numS2 * numS2, 0.0);
    for (int i = 0; i < numSt; ++i) {
        for (int j = 0; j < numS; ++j) {
            int idx = i * numS + j;
            NmatR[idx * numS2 + idx] = n0[i] * Pmat[j * numS + j] + bkgcnttot;
        }
    }

    // ---- NADmat[k]: per-colour diagonal photon count ----
    // For 2-colour: NADmat[0]=acceptor, NADmat[1]=donor
    // NADmat[numC-1] = total - sum(acceptors)
    std::vector<std::vector<double>> NADmat_R(numC, std::vector<double>(numS2 * numS2, 0.0));
    for (int k = 0; k < numC - 1; ++k) {
        for (int i = 0; i < numSt; ++i) {
            for (int j = 0; j < numS; ++j) {
                int idx = i * numS + j;
                NADmat_R[k][idx * numS2 + idx] = effn[i + numSt * k] * n0[i] * Pmat[j * numS + j] + bkgcnt[k];
            }
        }
    }
    // last colour = total - sum of other colours
    for (int i = 0; i < numS2; ++i)
        NADmat_R[numC - 1][i * numS2 + i] = NmatR[i * numS2 + i];
    for (int k = 0; k < numC - 1; ++k)
        for (int i = 0; i < numS2; ++i)
            NADmat_R[numC - 1][i * numS2 + i] -= NADmat_R[k][i * numS2 + i];

    // ---- Build combined operator: Lmat = kron(taumat, Lmat0) + kron(ratemat0, I) - NmatR ----
    std::vector<double> Lmat(numS2 * numS2, 0.0);

    // kron(taumat, Lmat0): block diagonal, block (i,j) = taumat[i,j] * Lmat0
    for (int bi = 0; bi < numSt; ++bi) {
        for (int bj = 0; bj < numSt; ++bj) {
            double scale = taumat[bi * numSt + bj];
            if (scale == 0.0) continue;
            for (int r = 0; r < numS; ++r)
                for (int c = 0; c < numS; ++c)
                    Lmat[(bi * numS + r) * numS2 + (bj * numS + c)] += scale * Lmat0[r * numS + c];
        }
    }
    // kron(ratemat0, I_numS)
    for (int bi = 0; bi < numSt; ++bi) {
        for (int bj = 0; bj < numSt; ++bj) {
            double scale = ratemat0[bi * numSt + bj];
            if (scale == 0.0) continue;
            for (int r = 0; r < numS; ++r)
                Lmat[(bi * numS + r) * numS2 + (bj * numS + r)] += scale;
        }
    }
    // subtract NmatR
    for (int i = 0; i < numS2; ++i)
        Lmat[i * numS2 + i] -= NmatR[i * numS2 + i];

    // ---- Eigendecompose Lmat ----
    std::vector<cdouble> evals_L, evecs_L, inv_evecs_L;
    if (!qr_eigendecompose(Lmat.data(), numS2, evals_L, evecs_L, inv_evecs_L))
        return -std::numeric_limits<double>::infinity();

    // ---- Transform photon matrices to eigenbasis ----
    // phiADmat[k] = inv_L * NADmat[k] * evecs_L
    std::vector<std::vector<cdouble>> phiADmat(numC);
    for (int k = 0; k < numC; ++k) {
        // convert NADmat_R[k] to complex
        std::vector<cdouble> NADk(numS2 * numS2);
        for (int i = 0; i < numS2 * numS2; ++i)
            NADk[i] = cdouble(NADmat_R[k][i], 0.0);
        std::vector<cdouble> tmp(numS2 * numS2);
        zmatmul(NADk.data(), evecs_L.data(), tmp.data(), numS2);    // tmp = NAD * L
        phiADmat[k].resize(numS2 * numS2);
        zmatmul(inv_evecs_L.data(), tmp.data(), phiADmat[k].data(), numS2); // phi = inv_L * NAD * L
    }

    // ---- Compute Mmat: the burst threshold propagator ----
    // phimattmp[j,j] = (exp(eval_L[j]*t_th) - 1) / eval_L[j]
    // Mmat = evecs_L * phimattmp * inv_evecs_L  (then multiply by Nmat)
    {
        std::vector<cdouble> phimattmp(numS2 * numS2, cdouble(0.0));
        for (int j = 0; j < numS2; ++j) {
            double ev = evals_L[j].real();
            cdouble val;
            if (std::abs(ev) < 1e-12)
                val = cdouble(t_th, 0.0);
            else
                val = cdouble((std::exp(ev * t_th) - 1.0) / ev, 0.0);
            phimattmp[j * numS2 + j] = val;
        }
        std::vector<cdouble> tmp1(numS2 * numS2), tmp2(numS2 * numS2);
        zmatmul(phimattmp.data(), inv_evecs_L.data(), tmp1.data(), numS2);  // tmp1 = phi * inv_L
        zmatmul(evecs_L.data(), tmp1.data(), tmp2.data(), numS2);            // tmp2 = L * phi * inv_L

        // Nmat as complex diagonal
        std::vector<cdouble> Nmat_c(numS2 * numS2, cdouble(0.0));
        for (int i = 0; i < numS2; ++i)
            Nmat_c[i * numS2 + i] = cdouble(NmatR[i * numS2 + i], 0.0);

        std::vector<cdouble> Mmat_c(numS2 * numS2);
        zmatmul(Nmat_c.data(), tmp2.data(), Mmat_c.data(), numS2);  // Mmat = Nmat * L * phi * inv_L

        // Extract real part and eigendecompose
        std::vector<double> Mmat_R(numS2 * numS2);
        for (int i = 0; i < numS2 * numS2; ++i)
            Mmat_R[i] = Mmat_c[i].real();

        std::vector<cdouble> evals_M, evecs_M, inv_evecs_M;
        if (!qr_eigendecompose(Mmat_R.data(), numS2, evals_M, evecs_M, inv_evecs_M))
            return -std::numeric_limits<double>::infinity();

        // store for logZ computation below
        // We need eigen_valuesM and inv_eigen_Mmat for logZ
        // and eigen_Mmat for the final transform

        // ---- Compute initial population pin ----
        // pin = evecs_L * diag(exp(eval_L[j]*t_th)) * inv_evecs_L * peqq
        std::vector<cdouble> peqq(numS2);
        for (int i = 0; i < numS; ++i) {
            for (int j = 0; j < numSt; ++j)
                peqq[i + j * numS] = cdouble(peq[j] * qaxis2[i], 0.0);
        }

        std::vector<cdouble> tmpv1(numS2), tmpv2(numS2), pin(numS2);
        zmatvec(inv_evecs_L.data(), peqq.data(), tmpv1.data(), numS2);  // inv_L * peqq
        for (int j = 0; j < numS2; ++j)
            tmpv2[j] = std::exp(evals_L[j] * t_th) * tmpv1[j];
        zmatvec(evecs_L.data(), tmpv2.data(), pin.data(), numS2);       // L * diag(exp) * inv_L * peqq

        // ---- Compute pfinmat = evecs_L * diag(exp(eval_L*t_th)) * inv_evecs_L ----
        std::vector<cdouble> pfinmat(numS2 * numS2);
        for (int j = 0; j < numS2; ++j)
            phimattmp[j * numS2 + j] = std::exp(evals_L[j] * t_th);
        zmatmul(phimattmp.data(), inv_evecs_L.data(), tmp1.data(), numS2);
        zmatmul(evecs_L.data(), tmp1.data(), pfinmat.data(), numS2);

        // ---- Compute logZ (normalisation for burst start/end) ----
        // logZ = log(sum( Nmat * inv_M * diag(evalM^(N_th-1)/(1-evalM)) * evecs_M * pfinmat * pin ))
        // Following the MEX code exactly:
        // probonesub = pin
        // phimattmp[j,j] = evalM[j]^(N_th-1) / (1 - evalM[j])
        // then: inv_M * phimattmp * evecs_M applied via zgemv chain
        std::vector<cdouble> probonesub(numS2), probonesub_t(numS2);
        for (int i = 0; i < numS2; ++i) probonesub[i] = pin[i];

        for (int j = 0; j < numS2; ++j) {
            phimattmp[j * numS2 + j] = cdouble(0.0);
            double em = evals_M[j].real();
            if (std::abs(1.0 - em) > 1e-15) {
                double val = std::pow(em, N_th - 1.0) / (1.0 - em);
                phimattmp[j * numS2 + j] = cdouble(val, 0.0);
            }
        }

        // probonesub_t = Nmat * probonesub  (Nmat is diagonal)
        for (int i = 0; i < numS2; ++i)
            probonesub_t[i] = Nmat_c[i * numS2 + i] * probonesub[i];
        // probonesub = inv_M * probonesub_t
        zmatvec(inv_evecs_M.data(), probonesub_t.data(), probonesub.data(), numS2);
        // probonesub_t = phimattmp * probonesub
        zmatvec(phimattmp.data(), probonesub.data(), probonesub_t.data(), numS2);
        // probonesub = evecs_M * probonesub_t
        zmatvec(evecs_M.data(), probonesub_t.data(), probonesub.data(), numS2);
        // probonesub_t = pfinmat * probonesub
        zmatvec(pfinmat.data(), probonesub.data(), probonesub_t.data(), numS2);

        double Z = 0.0;
        for (int i = 0; i < numS2; ++i)
            Z += std::abs(probonesub_t[i]);
        double logZ = (Z > 0.0) ? std::log(Z) : 0.0;

        // ---- Per-burst log-likelihood (OpenMP parallel) ----
        const int num_bursts = n_bursts();
        double total_ll = 0.0;

        #pragma omp parallel for reduction(+:total_ll) schedule(dynamic)
        for (int b = 0; b < num_bursts; ++b) {
            int64_t start = offsets_[b];
            int64_t stop  = offsets_[b + 1];
            if (stop - start < 2) continue;

            // init probonesub = pin
            std::vector<cdouble> psub(numS2), psub_t(numS2);
            for (int i = 0; i < numS2; ++i) psub[i] = pin[i];

            // transform to eigenbasis
            zmatvec(inv_evecs_L.data(), psub.data(), psub_t.data(), numS2);

            double logamp = 0.0;

            for (int64_t ii = start; ii < stop - 1; ++ii) {
                double dt = times_[ii + 1] - times_[ii];
                int color = colours_[ii];

                // apply coloured photon observation: phiADmat[color]
                zmatvec(phiADmat[color].data(), psub_t.data(), psub.data(), numS2);

                // apply time propagation: diag(exp(eval_L * dt))
                for (int i = 0; i < numS2; ++i)
                    psub_t[i] = std::exp(evals_L[i] * dt) * psub[i];

                // renormalise every 30 photons to prevent underflow
                if ((ii - start + 1) % 30 == 0) {
                    double norm_val = 0.0;
                    for (int i = 0; i < numS2; ++i)
                        norm_val += std::norm(psub_t[i]);
                    norm_val = std::sqrt(norm_val);
                    if (norm_val > 0.0) {
                        double inv_norm = 1.0 / norm_val;
                        for (int i = 0; i < numS2; ++i)
                            psub_t[i] *= inv_norm;
                        logamp += std::log(norm_val);
                    }
                }
            }

            // last photon: apply coloured observation then evecs_L then pfinmat
            int last_color = colours_[stop - 1];
            zmatvec(phiADmat[last_color].data(), psub_t.data(), psub.data(), numS2);
            zmatvec(evecs_L.data(), psub.data(), psub_t.data(), numS2);
            zmatvec(pfinmat.data(), psub_t.data(), psub.data(), numS2);

            double ll_burst = 0.0;
            double sum_p = 0.0;
            for (int i = 0; i < numS2; ++i)
                sum_p += std::abs(psub[i]);
            if (sum_p > 0.0)
                ll_burst = std::log(sum_p) + logamp;
            else
                ll_burst = -700.0;  // effectively -inf

            // total_ll is the total log-likelihood of the burst set: each burst
            // contributes logP(sequence | detected) = logL_burst - logZ. The MEX
            // reference minimises Σ(logZ - logL); here we keep the honest sign so
            // compute_log_likelihood returns log-likelihood (higher = better) and
            // neg_log_likelihood inverts it to the objective the optimiser minimises.
            total_ll += ll_burst - logZ;
        }

        return total_ll;
    }
}

} // namespace tttrlib
