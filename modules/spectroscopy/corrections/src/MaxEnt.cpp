// SPDX-License-Identifier: BSD-3-Clause
#include "MaxEnt.h"
#include "Registry.h"
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

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kMaxentInversionEntry = R"JSON({
  "name": "maxent_inversion",
  "label": "Maximum-entropy inversion of a linear model",
  "summary": "Solves A x = b for a non-negative x by maximising entropy under a chi-squared constraint (Skilling & Bryan), for distributions such as lifetime spectra.",
  "description": "The Skilling-Bryan maximum-entropy method: among all non-negative x consistent with the data to within the chi-squared target, the one of maximal entropy relative to a flat prior, so features are only those the data demand. `nu` sets the entropy weight, iterations run until the gradient criterion. Not equivalent to ChiSurf's `mem.py` (which minimises chi-squared with an entropy-flavoured gradient), which is why that was rejected as a reference.",
  "operation_type": "fitting",
  "method": "maxent_invert",
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
        "default": 500
      },
      "tol": {
        "type": "number",
        "title": "Tolerance",
        "default": 1e-08
      }
    }
  },
  "inputs": {
    "required": [
      "design_matrix",
      "data"
    ]
  },
  "outputs": {
    "columns": [
      "x"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "journal",
      "authors": "Skilling, J., Bryan, R. K.",
      "title": "Maximum entropy image reconstruction: general algorithm",
      "journal": "Mon Not R Astron Soc",
      "year": 1984,
      "volume": "211",
      "pages": "111-124"
    },
    {
      "type": "journal",
      "authors": "Brochon, J.-C.",
      "title": "Maximum entropy method of data analysis in time-resolved spectroscopy",
      "journal": "Methods Enzymol",
      "year": 1994,
      "volume": "240",
      "pages": "262-311"
    }
  ],
  "api": [
    "maxent_invert"
  ],
  "can_replay": true
})JSON";
bool register_maxent_entries() {
    tttrlib::register_algorithm_json("corrections", "maxent_inversion", kMaxentInversionEntry);
    return true;
}
const bool kMaxEntRegistered = register_maxent_entries();
}  // namespace
