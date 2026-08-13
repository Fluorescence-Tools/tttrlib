// SPDX-License-Identifier: BSD-3-Clause
#include "MaxEnt.h"
#include "MaxEntQp.h"

namespace tttrlib {

// Thin wrapper over the shared Skilling-Bryan engine (MaxEntQp.h). This used
// to be its own projected-gradient implementation whose entropy term had the
// wrong sign (rewarded moving away from the uniform prior instead of toward
// it -- see MaxEntQp.h's docstring for the measurement that caught it); it
// now shares the engine that MaxEntTcspc.cpp's tcspc_run_mem already used and
// was verified against simulated data.
//
// ||Ax - b||^2 - nu^2 * S(x) (this function's documented objective) is
// run_mem's `chisq - 0.5*nu_run*S`, so nu_run = 2*nu^2. run_mem's `m` is the
// prior -- ones, matching this function's uniform-prior default.
std::vector<double> maxent_invert(
    const std::vector<double>& A,
    const std::vector<double>& b,
    double nu,
    int n_rows, int n_cols,
    int max_iter, double tol
) {
    std::vector<double> H, g0;
    double const_term = 0.0;
    build_normal_equations(A, b, {}, n_rows, n_cols, H, g0, const_term);
    std::vector<double> prior(n_cols, 1.0);
    const double nu_run = 2.0 * nu * nu;
    const MaxEntResult r =
        run_mem(H, g0, prior, const_term, nu_run, max_iter, tol, 1e-12);
    return r.p;
}

} // namespace tttrlib