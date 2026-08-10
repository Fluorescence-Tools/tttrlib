// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_I_BFGS_H
#define TTTRLIB_I_BFGS_H

/*!
 * Header-only limited-memory BFGS minimizer with numerical gradients.
 *
 * Self-contained implementation of the standard L-BFGS two-loop recursion
 * (Nocedal 1980; Liu & Nocedal 1989) with an Armijo backtracking line
 * search and central-difference gradients. Licensed with the library
 * (BSD-3-Clause); no third-party code.
 *
 * The public interface of the bfgs class (construction with a target
 * function double f(double* x, void* user), fixed-parameter masks, eps and
 * iteration control, minimize() info codes) is unchanged:
 *   -1 wrong parameters, 0 aborted, 1 function decrease below EpsF,
 *    2 step below EpsX, 4 gradient norm below EpsG, 5 iteration limit.
 */

#include <cmath>      /* std::sqrt, std::fabs, std::isfinite */
#include <cstdlib>    /* std::abs */
#include <limits>     /* std::numeric_limits (soft-bound sentinels) */
#include <vector>
#include <algorithm>  /* std::min, std::max */

// pointer to the target function
typedef double(*TargetFP)(double*, void*);

/*!
 * @brief Optional analytic-gradient callback.
 *
 * Fills @p grad_out with @f$\partial f/\partial x_i@f$ over the **full**
 * parameter space (length ``N``; entries for fixed parameters are ignored) and
 * returns @f$f(x)@f$ — one call yields both, which is what a forward-mode
 * automatic-differentiation pass naturally produces.
 *
 * When no gradient is registered, ::bfgs falls back to its central-difference
 * scheme, so existing callers are unaffected.
 */
typedef double(*GradientFP)(double* x, double* grad_out, void* p);

/* Numerical Jacobians and gradients (1-, 2- and 4-point approximations).
 * Kept for interface compatibility; header-only now. */

inline int fjac1(void (*f)(double*, double*), double* x, int m, int n, double eps, double* fjac) {
    int ij = 0;
    double h, temp;
    std::vector<double> fvec(m), wa(m);
    f(x, fvec.data());
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * std::fabs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, wa.data());
        x[j] = temp;
        for (int i = 0; i < m; i++) {
            fjac[ij] = (wa[i] - fvec[i]) / h;
            ij += 1;    /* fjac[i+m*j] */
        }
    }
    return 0;
}

inline int fgrad1(void (*f)(double*, double&), double* x, int n, double eps, double* fgrad) {
    double h, temp, fval, w;
    f(x, fval);
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * std::fabs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, w);
        x[j] = temp;
        fgrad[j] = (w - fval) / h;
    }
    return 0;
}

inline int fjac2(void (*f)(double*, double*), double* x, int m, int n, double eps, double* fjac) {
    int ij = 0;
    double h, temp;
    std::vector<double> wa1(m), wa2(m);
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * std::fabs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, wa1.data());
        x[j] = temp - h;
        f(x, wa2.data());
        x[j] = temp;
        for (int i = 0; i < m; i++) {
            fjac[ij] = 0.5 * (wa1[i] - wa2[i]) / h;
            ij += 1;
        }
    }
    return 0;
}

inline int fgrad2(void (*f)(double*, double&), double* x, int n, double eps, double* fgrad) {
    double h, temp, w1, w2;
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * std::fabs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, w1);
        x[j] = temp - h;
        f(x, w2);
        x[j] = temp;
        fgrad[j] = 0.5 * (w1 - w2) / h;
    }
    return 0;
}

inline int fjac4(void (*f)(double*, double*), double* x, int m, int n, double eps, double* fjac) {
    const double c1 = 2. / 3.;
    const double c2 = 1. / 12.;
    int ij = 0;
    double h, temp;
    std::vector<double> wa1(m), wa2(m);
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * std::fabs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, wa1.data());
        x[j] = temp - h;
        f(x, wa2.data());
        for (int i = 0; i < m; i++) {
            fjac[ij] = c1 * (wa1[i] - wa2[i]) / h;
            ij += 1;
        }
        ij -= m;
        x[j] = temp + 2. * h;
        f(x, wa1.data());
        x[j] = temp - 2. * h;
        f(x, wa2.data());
        x[j] = temp;
        for (int i = 0; i < m; i++) {
            fjac[ij] += c2 * (wa2[i] - wa1[i]) / h;
            ij += 1;
        }
    }
    return 0;
}

inline int fgrad4(void (*f)(double*, double&), double* x, int n, double eps, double* fgrad) {
    const double c1 = 2. / 3.;
    const double c2 = 1. / 12.;
    double h, temp, w1, w2;
    for (int j = 0; j < n; j++) {
        temp = x[j];
        h = eps * std::fabs(temp);
        if (h == 0.) h = eps;
        x[j] = temp + h;
        f(x, w1);
        x[j] = temp - h;
        f(x, w2);
        fgrad[j] = c1 * (w1 - w2) / h;
        x[j] = temp + 2. * h;
        f(x, w1);
        x[j] = temp - 2. * h;
        f(x, w2);
        x[j] = temp;
        fgrad[j] += c2 * (w2 - w1) / h;
    }
    return 0;
}


class bfgs
{

 public:

  bfgs(TargetFP fun) { setdefaults(); N = 0; f = fun; }
  bfgs(TargetFP fun, int n) { setdefaults(); setN(n); f = fun; }
  ~bfgs() = default;

  // change dimension
  void setN(int n) {
    N = n;
    xd.assign(N, 0.0);
    fixed.assign(N, 0);
    lb.assign(N, -std::numeric_limits<double>::infinity());
    ub.assign(N, std::numeric_limits<double>::infinity());
  }

  /*!
   * @brief Soft box constraint on parameter @p i: keep it within [@p lo, @p hi].
   *
   * Implemented as a smooth exterior penalty added to the objective rather than
   * a hard projection/clamp. A hard clamp (e.g. the caller sanitising a
   * parameter to a maximum before evaluating) makes the objective *flat* beyond
   * the bound: the numerical gradient there is zero, the line search sees no
   * reason to come back, and the optimiser stalls pinned at the bound even when
   * a better interior point exists. A soft bound instead adds
   * @f$k\,(x-hi)^2@f$ once @f$x>hi@f$ (and symmetrically below @p lo), which is
   * zero and gradient-free inside the box — so the true objective is untouched
   * where it matters — but rises with a real gradient outside, pushing the
   * iterate back in. @p stiffness scales the penalty; the default suits an
   * O(1) objective. Call with no bound (the default @f$\pm\infty@f$) to leave a
   * parameter unconstrained.
   */
  void set_bounds(int i, double lo, double hi, double stiffness = 1.0e6) {
    if (i < 0 || i >= N) return;
    lb[i] = lo;
    ub[i] = hi;
    bound_k = stiffness;
    has_bounds = true;
  }

  // set epsilon explicitly
  void seteps(double e) {
    eps = e;
    sqrt_eps = std::sqrt(e);
  }

  // estimate epsilon
  void seteps() {
    double x1 = 1.0, x2 = 1.0, e = 1.e-18, estep = 1.001;
    for (;;) {
      x2 = x1 + e;
      if (x2 > x1) break;
      else e *= estep;
    }
    seteps(e);
  }

  /*!
   * @brief Register an analytic gradient, replacing central differences.
   *
   * The callback receives the full-length parameter vector and fills the
   * full-length gradient, returning @f$f(x)@f$. Passing ``nullptr`` restores
   * the finite-difference default.
   *
   * Exact gradients cost one pass instead of @f$2N@f$ objective evaluations
   * and remove the step-size compromise, which also makes the ``EpsG``
   * termination test trustworthy at tight tolerances.
   */
  void set_gradient(GradientFP g) { fgrad = g; }
  /// Whether an analytic gradient is in use.
  bool has_gradient() const { return fgrad != nullptr; }

  // fix or unfix a parameter
  void fix(int n) {
    if (n >= N || n < 0) return;
    fixed[n] = 1;
  }
  void free(int n) {
    if (n >= N || n < 0) return;
    fixed[n] = 0;
  }

  // max number of iterations
  int maxiter;

  /*!
   * Minimize f(x, p) over the non-fixed entries of x (in place).
   *
   * @return -1 wrong parameters, 0 aborted (non-finite target at the start),
   *         1 |dF| below EpsF, 2 step below EpsX, 4 |grad| below EpsG,
   *         5 iteration limit reached.
   */
  int minimize(double* x, void* p)
  {
    if (N == 0) return -1;

    pcopy = p;
    for (int i = 0; i < N; i++) xd[i] = x[i];

    // indices of the free parameters (reduced optimization space)
    std::vector<int> idx;
    idx.reserve(N);
    for (int i = 0; i < N; i++) {
      if (!fixed[i]) idx.push_back(i);
    }
    const int n = static_cast<int>(idx.size());
    if (n == 0) return 4;  // nothing to optimize: trivially converged

    const int m = std::min(n, 7);  // history size (matches previous bfgsM)

    // z: current point in the reduced space
    std::vector<double> z(n);
    for (int j = 0; j < n; j++) z[j] = x[idx[j]];

    // Objective seen by the optimiser: the target plus the soft-bound penalty
    // (identically zero when no bounds are set, so the unconstrained path is
    // unchanged). Using it everywhere f was called makes both the line search
    // and the numerical gradient feel the bounds.
    auto fp = [&](double* xx) -> double {
      return f(xx, pcopy) + bound_penalty(xx);
    };

    // evaluate f at a reduced-space point (fixed entries stay at input values)
    auto eval = [&](const std::vector<double>& zz) -> double {
      for (int j = 0; j < n; j++) xd[idx[j]] = zz[j];
      return fp(xd.data());
    };

    // Gradient in the reduced space. Uses the registered analytic gradient when
    // one is available (one pass, exact); otherwise falls back to the
    // central-difference scheme with step h = sqrt_eps * |x| (h = sqrt_eps at
    // 0), matching the previous 2-point behaviour. Returns f(zz).
    std::vector<double> gfull(N);
    auto grad = [&](const std::vector<double>& zz, std::vector<double>& g) -> double {
      for (int j = 0; j < n; j++) xd[idx[j]] = zz[j];
      if (fgrad != nullptr) {
        double fval = fgrad(xd.data(), gfull.data(), pcopy);
        // The analytic gradient does not know about the soft bounds, so add the
        // penalty and its (piecewise-linear) gradient here.
        if (has_bounds) {
          fval += bound_penalty(xd.data());
          for (int j = 0; j < n; j++) {
            const int i = idx[j];
            if (xd[i] < lb[i]) gfull[i] += -2.0 * bound_k * (lb[i] - xd[i]);
            else if (xd[i] > ub[i]) gfull[i] += 2.0 * bound_k * (xd[i] - ub[i]);
          }
        }
        for (int j = 0; j < n; j++) g[j] = gfull[idx[j]];
        return fval;
      }
      double fval = fp(xd.data());
      for (int j = 0; j < n; j++) {
        const double temp = zz[j];
        double h = sqrt_eps * std::fabs(temp);
        if (h == 0.) h = sqrt_eps;
        xd[idx[j]] = temp + h;
        const double w1 = fp(xd.data());
        xd[idx[j]] = temp - h;
        const double w2 = fp(xd.data());
        xd[idx[j]] = temp;
        g[j] = 0.5 * (w1 - w2) / h;
      }
      return fval;
    };

    auto dot = [n](const std::vector<double>& a, const std::vector<double>& b) {
      double r = 0.0;
      for (int j = 0; j < n; j++) r += a[j] * b[j];
      return r;
    };

    // L-BFGS history ring buffers
    std::vector<std::vector<double>> S, Y;
    std::vector<double> rho_hist;

    std::vector<double> g(n), g_new(n), d(n), z_new(n), s(n), y(n), q(n);

    double fx = grad(z, g);
    if (!std::isfinite(fx)) return 0;

    int info = 5;  // default: iteration limit
    for (int iter = 0; iter < maxiter; ++iter) {
      const double gnorm = std::sqrt(dot(g, g));
      if (!(gnorm > sqrt_eps)) { info = 4; break; }

      // two-loop recursion: d = -H*g
      q = g;
      const int k = static_cast<int>(S.size());
      std::vector<double> alpha(k);
      for (int i = k - 1; i >= 0; --i) {
        alpha[i] = rho_hist[i] * dot(S[i], q);
        for (int j = 0; j < n; j++) q[j] -= alpha[i] * Y[i][j];
      }
      if (k > 0) {
        // initial Hessian scaling gamma = s'y / y'y
        const double yy = dot(Y[k - 1], Y[k - 1]);
        const double sy = 1.0 / rho_hist[k - 1];
        const double gamma_k = (yy > 0.0) ? sy / yy : 1.0;
        for (int j = 0; j < n; j++) q[j] *= gamma_k;
      }
      for (int i = 0; i < k; ++i) {
        const double beta = rho_hist[i] * dot(Y[i], q);
        for (int j = 0; j < n; j++) q[j] += (alpha[i] - beta) * S[i][j];
      }
      for (int j = 0; j < n; j++) d[j] = -q[j];

      // ensure a descent direction
      double dg = dot(d, g);
      if (!(dg < 0.0) || !std::isfinite(dg)) {
        for (int j = 0; j < n; j++) d[j] = -g[j];
        dg = -gnorm * gnorm;
      }

      // Armijo backtracking line search
      double t = (iter == 0) ? std::min(1.0, 1.0 / std::max(gnorm, 1e-16)) : 1.0;
      double f_new = fx;
      bool accepted = false;
      for (int ls = 0; ls < 60; ++ls) {
        for (int j = 0; j < n; j++) z_new[j] = z[j] + t * d[j];
        f_new = eval(z_new);
        if (std::isfinite(f_new) && f_new <= fx + 1e-4 * t * dg) {
          accepted = true;
          break;
        }
        t *= 0.5;
      }
      if (!accepted) { info = 2; break; }  // no admissible step: converged in x

      grad(z_new, g_new);

      // history update (skip when curvature is not positive)
      double sy = 0.0, ss = 0.0;
      for (int j = 0; j < n; j++) {
        s[j] = z_new[j] - z[j];
        y[j] = g_new[j] - g[j];
        sy += s[j] * y[j];
        ss += s[j] * s[j];
      }
      if (std::isfinite(sy) && sy > 1e-16 * std::sqrt(ss) * std::sqrt(dot(y, y))) {
        S.push_back(s);
        Y.push_back(y);
        rho_hist.push_back(1.0 / sy);
        if (static_cast<int>(S.size()) > m) {
          S.erase(S.begin());
          Y.erase(Y.begin());
          rho_hist.erase(rho_hist.begin());
        }
      }

      const double df = std::fabs(f_new - fx);
      const double fscale = std::max(std::max(std::fabs(f_new), std::fabs(fx)), 1.0);
      const double step = std::sqrt(ss);

      z = z_new;
      std::swap(g, g_new);
      fx = f_new;

      if (df <= eps * fscale) { info = 1; break; }
      if (step <= sqrt_eps) { info = 2; break; }
    }

    // copy results back to x
    for (int j = 0; j < n; j++) x[idx[j]] = z[j];
    return info;
  }

 private:

  int N;
  double eps;
  double sqrt_eps;
  TargetFP f;
  GradientFP fgrad = nullptr;  ///< optional analytic gradient; central differences when null
  void* pcopy;
  std::vector<double> xd;
  std::vector<int> fixed;
  std::vector<double> lb, ub;   ///< per-parameter soft bounds (±inf = unbounded)
  double bound_k = 1.0e6;        ///< penalty stiffness for soft bounds
  bool has_bounds = false;       ///< skip the penalty entirely when unused

  /// Smooth exterior penalty enforcing the soft box bounds (0 inside the box).
  double bound_penalty(const double* x) const {
    if (!has_bounds) return 0.0;
    double p = 0.0;
    for (int i = 0; i < N; ++i) {
      if (fixed[i]) continue;
      if (x[i] < lb[i]) { const double d = lb[i] - x[i]; p += bound_k * d * d; }
      else if (x[i] > ub[i]) { const double d = x[i] - ub[i]; p += bound_k * d * d; }
    }
    return p;
  }

  void setdefaults()
  {
    N = 0;
    seteps(2.2e-16);
    maxiter = 100;
  }

};

#endif //TTTRLIB_I_BFGS_H
