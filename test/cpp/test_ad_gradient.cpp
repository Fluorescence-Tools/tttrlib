// SPDX-License-Identifier: BSD-3-Clause
//
// Guard for the vectorized forward-mode gradient used by the 2D-Gaussian
// localization fit.
//
//   c++ -std=c++17 -O2 -I modules/math/include -I thirdparty \
//       test/cpp/test_ad_gradient.cpp -o /tmp/test_ad_gradient && /tmp/test_ad_gradient
//
// `ImageLocalization.cpp` differentiates its objective by seeding
// `autodiff::detail::Dual<double, GradVec<N>>` with the N basis vectors and
// reading all N partials out of one forward pass. Two things about that are
// fragile enough to need a test that fails loudly:
//
//   1. Carrying a *vector* in the derivative slot is not a documented autodiff
//      feature. It works because `NumberTraits` can be specialized to say what
//      the underlying scalar is. An autodiff upgrade that changes what Dual
//      calls on `grad` would not fail to compile in an obvious place -- it
//      would compile and propagate wrong derivatives.
//   2. `GradVec` implements exactly the operators Dual uses. Getting one of
//      them wrong (a sign, an aliasing bug in `*=`) is silent in the same way:
//      the fit still converges, to the wrong place.
//
// So the check is a differential one. The same objective is differentiated
// three ways -- vectorized dual, scalar dual seeded N times, and central
// differences -- and they must agree. Scalar `dual` is the reference because it
// is autodiff's own tested path; central differences are the independent one,
// slack enough not to be a precision test and tight enough to catch a sign.
//
// The objective replicates `gauss_cost`: the logistic/exponential
// reparameterisation, a sum of Gaussians, and a Poisson maximum-likelihood
// cost. Not the production function itself (which is in an anonymous namespace
// inside a module source), but the same operation set -- exp, log, the four
// arithmetic operators, unary minus and division by a scalar -- which is what
// determines which of Dual's assignment paths get instantiated.
//
// Exit status is the number of failed checks.

#include "GradVec.h"

#include <autodiff/forward/dual.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

namespace autodiff {
namespace detail {
template <int N>
struct NumberTraits<tttrlib::GradVec<N>> {
    using NumericType = double;
    static constexpr auto Order = 0;
};
}  // namespace detail
}  // namespace autodiff

using tttrlib::GradVec;

static int g_failures = 0;

static void report(bool ok, const char* what, double value, double tol) {
    if (!ok) {
        std::printf("  FAIL  %s (%.3g > %.3g)\n", what, value, tol);
        ++g_failures;
    } else {
        std::printf("  ok    %s (%.3g)\n", what, value);
    }
}

static void check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL  %s\n", what); ++g_failures; }
    else       std::printf("  ok    %s\n", what);
}

// --------------------------------------------------------------------------
// The objective: same shape as localization's gauss_cost.
// --------------------------------------------------------------------------

const int NP = 12;      ///< kNModelPar in ImageLocalization.cpp
const int XLEN = 9;
const int YLEN = 7;

/// Synthetic photon counts, fixed so the test is deterministic.
static std::vector<double> make_data() {
    std::vector<double> d(XLEN * YLEN);
    for (int y = 0; y < YLEN; ++y)
        for (int x = 0; x < XLEN; ++x) {
            const double dx = x - 4.2, dy = y - 3.1;
            d[y * XLEN + x] = std::floor(60.0 * std::exp(-(dx * dx + dy * dy) / 3.0) + 2.0);
        }
    return d;
}

template <typename T>
static void model_gauss(const T& x0, const T& y0, const T& A, const T& sigma,
                        const T& eps, const T& bg, T* model, bool accumulate) {
    const T tx = T(0.5) / (sigma * sigma);
    const T ty = T(0.5) / (sigma * sigma * eps * eps);
    std::vector<T> ex(XLEN);
    for (int x = 0; x < XLEN; ++x) {
        const T d = T(double(x)) - x0;
        ex[x] = exp(-d * d * tx);
    }
    int i = 0;
    for (int y = 0; y < YLEN; ++y) {
        const T dy = T(double(y)) - y0;
        const T ey = exp(-dy * dy * ty);
        for (int x = 0; x < XLEN; ++x) {
            const T v = A * ex[x] * ey + bg;
            if (accumulate) model[i] += v;
            else model[i] = v;
            ++i;
        }
    }
}

/// Poisson MLR cost as a pure function of the *unconstrained* parameters.
template <typename T>
static T cost(const T* u, const double* data) {
    const int pos_idx[6] = {0, 1, 6, 7, 9, 10};
    const double pos_len[6] = {XLEN, YLEN, XLEN, YLEN, XLEN, YLEN};

    T c[NP];
    for (int k = 0; k < NP; ++k) c[k] = u[k];
    for (int k = 0; k < 6; ++k) {
        const int i = pos_idx[k];
        c[i] = T(pos_len[k]) / (T(1.0) + exp(-u[i]));
    }
    c[3] = exp(u[3]);
    c[4] = exp(u[4]);
    c[5] = exp(u[5]);

    const int osize = XLEN * YLEN;
    std::vector<T> model(osize);
    model_gauss<T>(c[0], c[1], c[2], c[3], c[4], c[5], model.data(), false);
    model_gauss<T>(c[6], c[7], c[8], c[3], c[4], T(0.0), model.data(), true);
    model_gauss<T>(c[9], c[10], c[11], c[3], c[4], T(0.0), model.data(), true);

    T w = T(0.0);
    for (int i = 0; i < osize; ++i) {
        if (data[i] > 1e-12) w += model[i] - data[i] * log(model[i]);
        else w += model[i];
    }
    return w / double(osize);
}

// --------------------------------------------------------------------------
// The three gradients.
// --------------------------------------------------------------------------

/// One forward pass, all NP partials -- what the library actually does.
static double grad_vectorized(const double* u, const double* data, double* g) {
    using Arr = GradVec<NP>;
    using DualN = autodiff::detail::Dual<double, Arr>;
    DualN ud[NP];
    for (int j = 0; j < NP; ++j) {
        ud[j].val = u[j];
        ud[j].grad = Arr::Unit(j);
    }
    const DualN r = cost<DualN>(ud, data);
    for (int j = 0; j < NP; ++j) g[j] = r.grad[j];
    return r.val;
}

/// NP forward passes with autodiff's own scalar dual -- the reference.
static double grad_scalar_dual(const double* u, const double* data, double* g) {
    using Dual1 = autodiff::detail::Dual<double, double>;
    double val = 0.0;
    for (int k = 0; k < NP; ++k) {
        Dual1 ud[NP];
        for (int j = 0; j < NP; ++j) {
            ud[j].val = u[j];
            ud[j].grad = (j == k) ? 1.0 : 0.0;
        }
        const Dual1 r = cost<Dual1>(ud, data);
        g[k] = r.grad;
        val = r.val;
    }
    return val;
}

/// Central differences at the cube-root step -- the independent check.
static void grad_central(const double* u, const double* data, double* g) {
    const double eps13 = std::cbrt(2.220446049250313e-16);
    double x[NP];
    for (int j = 0; j < NP; ++j) x[j] = u[j];
    for (int j = 0; j < NP; ++j) {
        const double h = eps13 * std::max(std::fabs(u[j]), 1.0);
        x[j] = u[j] + h;
        const double fp = cost<double>(x, data);
        x[j] = u[j] - h;
        const double fm = cost<double>(x, data);
        x[j] = u[j];
        g[j] = (fp - fm) / (2.0 * h);
    }
}

// --------------------------------------------------------------------------

/// GradVec's own algebra, checked directly. A wrong operator here is a wrong
/// derivative there, and the differential tests below share the operator set
/// with the thing they are testing only for the ops Dual happens to call.
static void test_gradvec_algebra() {
    std::printf("GradVec algebra\n");

    GradVec<4> a;
    for (int i = 0; i < 4; ++i) a[i] = 1.0 + i;

    GradVec<4> z = GradVec<4>::Zero();
    check(z[0] == 0.0 && z[3] == 0.0, "Zero() is zero");

    GradVec<4> e = GradVec<4>::Unit(2);
    check(e[0] == 0.0 && e[1] == 0.0 && e[2] == 1.0 && e[3] == 0.0, "Unit(2) is e_2");

    GradVec<4> s;
    s = 7.0;  // autodiff assigns Zero<G>(), a plain double
    check(s[0] == 7.0 && s[3] == 7.0, "assignment from a scalar broadcasts");

    GradVec<4> n = -a;
    check(n[0] == -1.0 && n[3] == -4.0, "unary minus");
    check(a[0] == 1.0, "unary minus does not modify its operand");

    GradVec<4> b = a;
    b += a;
    check(b[3] == 8.0, "operator+=");
    b -= a;
    check(b[3] == 4.0, "operator-=");
    b *= 3.0;
    check(b[3] == 12.0, "operator*= by a scalar");
    b /= 3.0;
    check(std::fabs(b[3] - 4.0) < 1e-15, "operator/= by a scalar");

    GradVec<4> l = 2.0 * a, r = a * 2.0;
    check(l[3] == 8.0 && r[3] == 8.0, "scalar product from both sides");
    GradVec<4> q = a / 2.0;
    check(q[3] == 2.0, "division by a scalar");

    // Dual's assignMul does `grad *= other.val; grad += val * aux` with aux a
    // copy of grad, guarding against self-aliasing. Confirm the copy is a real
    // copy and not a reference into the same storage.
    GradVec<4> self = a;
    const GradVec<4> aux = self;
    self *= 2.0;
    check(aux[3] == 4.0 && self[3] == 8.0, "copy is independent of its source");
}

int main() {
    const std::vector<double> data = make_data();

    // An interior point of the unconstrained space, away from any symmetry.
    const double u[NP] = {
        0.31, -0.22, 55.0, std::log(1.4), std::log(1.1), std::log(2.5),
        -0.7,  0.45, 12.0, 0.9, -1.3, 7.0
    };

    test_gradvec_algebra();

    std::printf("vectorized dual vs scalar dual\n");
    double gv[NP], gs[NP], gc[NP];
    const double fv = grad_vectorized(u, data.data(), gv);
    const double fs = grad_scalar_dual(u, data.data(), gs);
    const double fd = cost<double>(u, data.data());

    report(std::fabs(fv - fd) == 0.0, "value: vectorized == plain double",
           std::fabs(fv - fd), 0.0);
    report(std::fabs(fs - fd) == 0.0, "value: scalar dual == plain double",
           std::fabs(fs - fd), 0.0);

    // Bitwise agreement is *nearly* the expectation -- both paths execute the
    // same operations in the same order on the same doubles, only packed
    // differently -- but not quite, and the reason is worth recording rather
    // than rediscovering. `Dual::assignMul` computes `grad*other.val +
    // val*aux`, and whether the compiler contracts that into an FMA depends on
    // the shape it is inlined into: it does for the scalar `grad`, and not
    // always for the loop over GradVec's array. Compiling this file with
    // `-ffp-contract=off` makes the two agree to the last bit; at clang's
    // default (`on`) one component differs by 1 ulp.
    //
    // So the tolerance is tight enough that any real defect -- a sign, an
    // aliasing bug in `*=`, a missing term -- shows up as a relative error near
    // 1, and loose enough that a legal contraction does not fail the build.
    double max_rel_ref = 0.0;
    for (int j = 0; j < NP; ++j) {
        const double scale = std::max(std::fabs(gs[j]), 1e-30);
        max_rel_ref = std::max(max_rel_ref, std::fabs(gv[j] - gs[j]) / scale);
    }
    report(max_rel_ref < 1e-13, "gradient: vectorized vs scalar dual", max_rel_ref, 1e-13);

    // Nonzero, so agreement above is not two zero vectors matching.
    double max_mag = 0.0;
    for (int j = 0; j < NP; ++j) max_mag = std::max(max_mag, std::fabs(gs[j]));
    check(max_mag > 1e-6, "the reference gradient is not identically zero");

    std::printf("vectorized dual vs central differences\n");
    grad_central(u, data.data(), gc);
    double max_rel = 0.0;
    for (int j = 0; j < NP; ++j) {
        const double scale = std::max(std::fabs(gc[j]), 1e-8);
        max_rel = std::max(max_rel, std::fabs(gv[j] - gc[j]) / scale);
    }
    report(max_rel < 1e-6, "gradient: vectorized vs central differences", max_rel, 1e-6);

    if (g_failures) std::printf("\n%d check(s) FAILED\n", g_failures);
    else            std::printf("\nall checks passed\n");
    return g_failures;
}
