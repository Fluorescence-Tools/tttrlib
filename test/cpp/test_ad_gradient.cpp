// SPDX-License-Identifier: BSD-3-Clause
//
// The vectorized forward-mode gradient used by the 2D-Gaussian localization
// fit and DecayFit23's tau/gamma fit: `tttrlib::Dual<GradVec<N>>`
// (modules/math).
//
//   c++ -std=c++17 -O2 -I modules/math/include -I modules/spectroscopy/decay/include \
//       test/cpp/test_ad_gradient.cpp -o /tmp/test_ad_gradient && /tmp/test_ad_gradient
//
// `ImageLocalization.cpp` differentiates its objective by seeding one Dual per
// parameter with a basis vector and reading all N partials out of a single
// forward pass. Both halves are now the library's own -- Dual.h replaced
// autodiff, which was a ~10k-line vendored package used for one class template
// and which only worked here through an undocumented trait hook.
//
// Owning the code changes what this test is for. It used to guard a contract
// with an upstream package: an autodiff bump could keep compiling and silently
// propagate wrong derivatives. Now it checks an implementation, and the failure
// mode is the same either way -- a sign, an aliasing bug in `*=`, a missing
// term in the product rule does not crash and does not fail to compile. The fit
// still converges, to the wrong place.
//
// So the checks are differential and layered:
//
//   1. GradVec's algebra and Dual's algebra, directly, against derivatives
//      that can be written down by hand.
//   2. The localization objective differentiated four ways -- vectorized dual,
//      scalar dual, a long-double scalar dual, and central differences -- which
//      must agree. The scalar dual shares Dual's formulas, so it only catches a
//      carrier bug; the long-double dual shares them at higher precision, so it
//      bounds the rounding; central differences share nothing at all, so they
//      are what catches a wrong formula.
//
// The objective replicates `gauss_cost`: the logistic/exponential
// reparameterisation, a sum of Gaussians, and a Poisson maximum-likelihood
// cost. Not the production function itself (which is in an anonymous namespace
// inside a module source), but the same operation set -- exp, log, the four
// arithmetic operators, unary minus and division by a scalar -- which is what
// determines which of Dual's paths get instantiated.
//
// Exit status is the number of failed checks.

#include "Dual.h"
#include "GradVec.h"
#include "DecayConvolution.h"   // fconv_per_cs_ad -- the shared, production kernel
#include "DecayStatistics.h"    // Wcm_ad/log_m_ext_ad -- likewise

#include <cmath>
#include <cstdio>
#include <vector>

using tttrlib::Dual;
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
// The four gradients.
// --------------------------------------------------------------------------

/// One forward pass, all NP partials -- what the library actually does.
static double grad_vectorized(const double* u, const double* data, double* g) {
    using Arr = GradVec<NP>;
    using DualN = Dual<Arr>;
    DualN ud[NP];
    for (int j = 0; j < NP; ++j) ud[j] = DualN(u[j], Arr::Unit(j));
    const DualN r = cost<DualN>(ud, data);
    for (int j = 0; j < NP; ++j) g[j] = r.grad[j];
    return r.val;
}

/// NP forward passes with a plain `double` in the derivative slot. Same
/// formulas, different carrier -- so this isolates GradVec from Dual.
static double grad_scalar_dual(const double* u, const double* data, double* g) {
    using Dual1 = Dual<double>;
    double val = 0.0;
    for (int k = 0; k < NP; ++k) {
        Dual1 ud[NP];
        for (int j = 0; j < NP; ++j) ud[j] = Dual1(u[j], (j == k) ? 1.0 : 0.0);
        const Dual1 r = cost<Dual1>(ud, data);
        g[k] = r.grad;
        val = r.val;
    }
    return val;
}

/// The same arithmetic in long double, written out independently of Dual.h.
///
/// It cannot catch a wrong derivative *rule* -- it uses the same rules -- but
/// it computes them to a 64-bit mantissa, which is what makes the tolerance
/// below meaningful: it says how much of the disagreement between the double
/// paths is rounding and how much is not.
namespace {
struct LDual {
    long double val, grad;
    LDual() : val(0), grad(0) {}
    explicit LDual(double v) : val(v), grad(0) {}
    LDual(long double v, long double g) : val(v), grad(g) {}
    LDual& operator+=(const LDual& o) { val += o.val; grad += o.grad; return *this; }
    LDual& operator-=(const LDual& o) { val -= o.val; grad -= o.grad; return *this; }
    LDual& operator*=(const LDual& o) {
        grad = grad * o.val + val * o.grad;
        val *= o.val;
        return *this;
    }
    LDual& operator/=(const LDual& o) {
        val /= o.val;
        grad = (grad - val * o.grad) / o.val;
        return *this;
    }
    LDual operator-() const { return LDual(-val, -grad); }
};
LDual operator+(LDual a, const LDual& b) { a += b; return a; }
LDual operator-(LDual a, const LDual& b) { a -= b; return a; }
LDual operator*(LDual a, const LDual& b) { a *= b; return a; }
LDual operator/(LDual a, const LDual& b) { a /= b; return a; }
LDual operator*(double s, const LDual& a) { return LDual(s * a.val, s * a.grad); }
LDual operator/(const LDual& a, double s) { return LDual(a.val / s, a.grad / s); }
LDual exp(const LDual& a) { const long double v = expl(a.val); return LDual(v, a.grad * v); }
LDual log(const LDual& a) { return LDual(logl(a.val), a.grad / a.val); }
}  // namespace

static void grad_long_double(const double* u, const double* data, double* g) {
    for (int k = 0; k < NP; ++k) {
        LDual ud[NP];
        for (int j = 0; j < NP; ++j) ud[j] = LDual((long double)u[j], (j == k) ? 1.0L : 0.0L);
        g[k] = (double)cost<LDual>(ud, data).grad;
    }
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
    s = 7.0;  // a scalar broadcasts, which is how a zero derivative is made
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

    // The fused `x += s * g` path: the proxy must not be evaluated into a
    // temporary that then gets added to itself.
    GradVec<4> f = a;
    f += 2.0 * a;
    check(f[3] == 12.0, "fused += scalar * GradVec");
    f -= 2.0 * a;
    check(f[3] == 4.0, "fused -= scalar * GradVec");

    // Dual's operator*= does `grad *= other.val; grad += val * aux` with aux a
    // copy of the other gradient, guarding against self-aliasing. Confirm the
    // copy is a real copy and not a reference into the same storage.
    GradVec<4> self = a;
    const GradVec<4> aux = self;
    self *= 2.0;
    check(aux[3] == 4.0 && self[3] == 8.0, "copy is independent of its source");
}

/// Dual's own algebra: each rule against a derivative written by hand.
///
/// Every check is at x = 2, y = 3 with dx = 1, dy = 0 unless stated, so the
/// expected values are exact in binary and the comparisons can be equalities.
static void test_dual_algebra() {
    std::printf("Dual algebra\n");

    using D = Dual<double>;
    const D x(2.0, 1.0), y(3.0, 0.0);

    check(D(5.0).val == 5.0 && D(5.0).grad == 0.0, "a constant has zero derivative");
    check(D().val == 0.0 && D().grad == 0.0, "default construction is zero");

    const D s = x + y;
    check(s.val == 5.0 && s.grad == 1.0, "sum rule");
    const D d = x - y;
    check(d.val == -1.0 && d.grad == 1.0, "difference rule");
    const D p = x * y;
    check(p.val == 6.0 && p.grad == 3.0, "product rule");
    const D q = x / y;   // d/dx (x/y) = 1/y
    check(std::fabs(q.grad - 1.0 / 3.0) < 1e-16, "quotient rule");

    // The other operand differentiated too: d/dy (x/y) = -x/y^2 = -2/9.
    const D x0(2.0, 0.0), y1(3.0, 1.0);
    const D q2 = x0 / y1;
    check(std::fabs(q2.grad + 2.0 / 9.0) < 1e-16, "quotient rule, denominator");

    const D n = -x;
    check(n.val == -2.0 && n.grad == -1.0, "unary minus");

    // Aliasing: x *= x and x /= x must not read a gradient they have already
    // overwritten. d/dx x^2 = 2x = 4; d/dx (x/x) = 0.
    D sq = x;
    sq *= sq;
    check(sq.val == 4.0 && sq.grad == 4.0, "self-multiplication");
    D one = x;
    one /= one;
    check(one.val == 1.0 && one.grad == 0.0, "self-division");

    // Mixed with plain doubles: the scalar contributes no derivative.
    check((x + 3.0).grad == 1.0 && (x + 3.0).val == 5.0, "dual + scalar");
    check((3.0 + x).val == 5.0, "scalar + dual");
    check((x - 3.0).val == -1.0 && (x - 3.0).grad == 1.0, "dual - scalar");
    check((3.0 - x).val == 1.0 && (3.0 - x).grad == -1.0, "scalar - dual");
    check((x * 3.0).val == 6.0 && (x * 3.0).grad == 3.0, "dual * scalar");
    check((3.0 * x).grad == 3.0, "scalar * dual");
    check((x / 4.0).val == 0.5 && (x / 4.0).grad == 0.25, "dual / scalar");
    check(std::fabs((4.0 / x).val - 2.0) < 1e-16 &&
          std::fabs((4.0 / x).grad + 1.0) < 1e-16, "scalar / dual");

    // exp and log, and the identity log(exp(x)) == x with derivative 1.
    const D ex = exp(x);
    check(std::fabs(ex.val - std::exp(2.0)) < 1e-15 &&
          std::fabs(ex.grad - std::exp(2.0)) < 1e-15, "d/dx exp(x) = exp(x)");
    const D lg = log(x);
    check(std::fabs(lg.val - std::log(2.0)) < 1e-16 &&
          std::fabs(lg.grad - 0.5) < 1e-16, "d/dx log(x) = 1/x");
    const D round_trip = log(exp(x));
    check(std::fabs(round_trip.val - 2.0) < 1e-15 &&
          std::fabs(round_trip.grad - 1.0) < 1e-15, "log(exp(x)) is the identity");

    check((x < y) && (y > x) && (x != y) && (x == D(2.0, 99.0)),
          "comparisons look only at the value");

    // The vector carrier reaches every branch above through the same code, so
    // one composite check on it is enough here -- the objective below is what
    // exercises it in anger. f(a,b) = a*b + exp(a)/b at (2,3):
    // df/da = b + exp(a)/b = 3 + e^2/3, df/db = a - exp(a)/b^2 = 2 - e^2/9.
    using D2 = Dual<GradVec<2>>;
    const D2 a(2.0, GradVec<2>::Unit(0)), b(3.0, GradVec<2>::Unit(1));
    const D2 f = a * b + exp(a) / b;
    const double e2 = std::exp(2.0);
    check(std::fabs(f.val - (6.0 + e2 / 3.0)) < 1e-14, "vector carrier: value");
    check(std::fabs(f.grad[0] - (3.0 + e2 / 3.0)) < 1e-14, "vector carrier: d/da");
    check(std::fabs(f.grad[1] - (2.0 - e2 / 9.0)) < 1e-14, "vector carrier: d/db");
}

// --------------------------------------------------------------------------
// DecayFit23's objective (PRD-010's second AD candidate, N=4). Reuses the
// actual production kernels -- fconv_per_cs_ad (DecayConvolution.h) and
// Wcm_ad/log_m_ext_ad (DecayStatistics.h) -- rather than reimplementing them,
// so this exercises the same templates decay23_gradient (DecayFit23.cpp)
// instantiates. Only the anisotropy glue (Fp/Fs/r/rho, the harmonic mean) is
// reimplemented, mirroring DecayFit23.cpp's anonymous-namespace helpers of
// the same names -- kept local for the same reason the ImageLocalization
// objective above is: the production versions are anonymous-namespace and
// not linkable from here.
// --------------------------------------------------------------------------

namespace decay23 {

constexpr int FN = 32;         // channels per polarization
constexpr double DT = 0.5;
constexpr double kMinTau = 1.0e-3, kMinRho = 1.0e-6;
constexpr double kMinGamma = 0.0, kMaxGamma = 0.999;

// Local transcriptions of DecayFit.h's soft_floor_ad/clamp_value_ad (not
// pulled in directly: DecayFit.h drags in DecayFitContext.h/nlohmann json,
// which this file's standalone build command does not set up an include path
// for). Same two functions, same shape.
template <typename T>
T soft_floor_ad(const T& v, double floor) {
    using std::exp;
    if (v < floor) return floor * exp((v - floor) / floor);
    return v;
}

template <typename T>
T clamp_value_ad(const T& v, double lower, double upper) {
    if (v < lower) return T(lower);
    if (v > upper) return T(upper);
    return v;
}

template <typename T>
T harmonic_mean(const T& a, const T& b) {
    if (!(b > 0.)) return a;
    const T denom = 1. / a + 1. / b;
    if (!(denom > 1e-300)) return a;
    return 1. / denom;
}

template <typename T>
T Fp(double Sp, double Bp, const T& gamma) { return (Sp - gamma * Bp) / (1. - gamma); }

template <typename T>
T Fs(double Ss, double Bs, const T& gamma) { return (Ss - gamma * Bs) / (1. - gamma); }

template <typename T>
T r_value(double Sp, double Ss, double Bp, double Bs, double g, double l1, double l2,
         const T& gamma) {
    const T fp = Fp(Sp, Bp, gamma), fs = Fs(Ss, Bs, gamma);
    return (fp - g * fs) / (fp * (1. - 3. * l2) + (2. - 3. * l1) * g * fs);
}

template <typename T>
T rho_of(const T& tau, const T& r0, const T& r) {
    const T rh = tau / (r0 / r - 1.);
    if (rh < 1.e-4) return T(1.e-4);
    return rh;
}

struct Fixture {
    std::vector<double> irf, bg;
    int* counts;   // Jordi layout, 2*FN
    double Sp, Ss, Bp, Bs;
    double corrections[5];  // period, g, l1, l2, conv_stop
};

static std::vector<int> make_counts() {
    // A synthetic anisotropy decay: enough counts to keep Wcm_ad away from
    // its C1-continuation floor almost everywhere, which is what makes a
    // central-difference cross-check meaningful (the floor's derivative is a
    // fixed 1/kModelFloor, not something a wrong formula would visibly miss).
    std::vector<int> c(2 * FN);
    for (int i = 0; i < FN; ++i) {
        const double t = i * DT;
        const double vv = 400.0 * std::exp(-t / 2.3) * (1.0 + 0.3 * std::exp(-t / 1.1));
        const double vh = 130.0 * std::exp(-t / 2.3) * (1.0 - 0.15 * std::exp(-t / 1.1));
        c[i] = (int)std::floor(vv) + (i % 3);
        c[FN + i] = (int)std::floor(vh) + (i % 2);
    }
    return c;
}

/// Same shape as DecayFit23.cpp's decay23_cost: tau/gamma/r0/rho in, 2I*-style
/// Poisson MLE cost out, in one pass under whatever T the caller seeds.
template <typename T>
T cost(const T param[4], bool fixedrho, const Fixture& fx, const int* counts) {
    const T tau = soft_floor_ad(param[0], kMinTau);
    const T gamma = clamp_value_ad(param[1], kMinGamma, kMaxGamma);
    const T r0 = param[2];

    const double period = fx.corrections[0], g = fx.corrections[1],
                l1 = fx.corrections[2], l2 = fx.corrections[3];
    const int conv_stop = (int)fx.corrections[4];

    T rho;
    if (fixedrho) {
        rho = soft_floor_ad(param[3], kMinRho);
    } else {
        const T r = r_value(fx.Sp, fx.Ss, fx.Bp, fx.Bs, g, l1, l2, gamma);
        rho = soft_floor_ad(rho_of(tau, r0, r), kMinRho);
    }
    const T taurho = harmonic_mean(tau, rho);

    T x_vv[4] = {T(1.), tau, r0 * (2. - 3. * l1), taurho};
    T x_vh[4] = {T(1. / g), tau, T(1. / g) * r0 * (-1. + 3. * l2), taurho};

    std::vector<T> model(2 * FN);
    fconv_per_cs_ad(model.data(), x_vv, fx.irf.data(), 2, FN - 1, FN, period, conv_stop, DT);
    fconv_per_cs_ad(model.data() + FN, x_vh, fx.irf.data() + FN, 2, FN - 1, FN, period, conv_stop, DT);

    T sum_m(0.);
    for (int i = 0; i < 2 * FN; ++i) sum_m += model[i];
    if (!(sum_m > 0.)) {
        for (int i = 0; i < 2 * FN; ++i) model[i] = T(fx.bg[i]) * gamma;
    } else {
        const T scale = (1. - gamma) / sum_m;
        for (int i = 0; i < 2 * FN; ++i) model[i] = model[i] * scale + T(fx.bg[i]) * gamma;
    }
    const double Sexp = fx.Sp + fx.Ss;
    for (int i = 0; i < 2 * FN; ++i) model[i] *= Sexp;

    return Wcm_ad(counts, model.data(), FN) / double(FN);
}

static void run() {
    std::printf("DecayFit23 objective: vectorized dual vs central differences\n");

    static std::vector<double> irf(2 * FN, 0.0), bg(2 * FN, 0.0);
    for (int half = 0; half < 2; ++half)
        for (int i = 0; i < FN; ++i)
            irf[half * FN + i] = std::exp(-((i - 6.0) * (i - 6.0)) / (2 * 0.6 * 0.6));

    static std::vector<int> counts_v = make_counts();

    Fixture fx;
    fx.irf = irf;
    fx.bg = bg;
    fx.corrections[0] = 2.0 * FN;  // period
    fx.corrections[1] = 1.0;       // g
    fx.corrections[2] = 0.1;       // l1
    fx.corrections[3] = 0.1;       // l2
    fx.corrections[4] = FN / 2 - 1;  // conv_stop

    // Sp/Ss/Bp/Bs as compute_signal_and_background would produce them, for an
    // interior point away from the fit's optimum (where a wrong gradient and
    // a right one both look small).
    fx.Sp = 0.0; fx.Ss = 0.0; fx.Bp = 0.0; fx.Bs = 0.0;
    for (int i = 0; i < FN; ++i) { fx.Sp += counts_v[i]; fx.Ss += counts_v[FN + i]; }

    using Grad4 = GradVec<4>;
    using D4 = Dual<Grad4>;

    // Two points: rho derived (the common case) and rho fixed (the other
    // branch decay23_cost takes).
    const double points[2][4] = {
        {2.0, 0.15, 0.38, 1.2},
        {0.9, 0.05, 0.38, 1.2},
    };
    for (bool fixedrho : {false, true}) {
        for (const auto& u : points) {
            D4 ud[4];
            for (int j = 0; j < 4; ++j) ud[j] = D4(u[j], Grad4::Unit(j));
            const D4 rv = cost<D4>(ud, fixedrho, fx, counts_v.data());

            const double fd0 = cost<double>(u, fixedrho, fx, counts_v.data());
            char label[64];
            std::snprintf(label, sizeof(label), "value (fixedrho=%d)", (int)fixedrho);
            report(std::fabs(rv.val - fd0) < 1e-12, label, std::fabs(rv.val - fd0), 1e-12);

            double gc[4];
            const double eps13 = std::cbrt(2.220446049250313e-16);
            double x[4] = {u[0], u[1], u[2], u[3]};
            for (int j = 0; j < 4; ++j) {
                const double h = eps13 * std::max(std::fabs(u[j]), 1.0);
                x[j] = u[j] + h;
                const double fp = cost<double>(x, fixedrho, fx, counts_v.data());
                x[j] = u[j] - h;
                const double fm = cost<double>(x, fixedrho, fx, counts_v.data());
                x[j] = u[j];
                gc[j] = (fp - fm) / (2.0 * h);
            }

            double max_rel = 0.0;
            for (int j = 0; j < 4; ++j) {
                const double scale = std::max(std::fabs(gc[j]), 1e-8);
                max_rel = std::max(max_rel, std::fabs(rv.grad[j] - gc[j]) / scale);
            }
            std::snprintf(label, sizeof(label), "gradient vs central differences (fixedrho=%d)",
                         (int)fixedrho);
            report(max_rel < 1e-6, label, max_rel, 1e-6);
        }
    }
}

}  // namespace decay23

// --------------------------------------------------------------------------
// DecayFit24's objective (PRD-010's other AD candidate, N=5) -- kept as a
// record that the approach was TESTED and correct, even though it was not
// shipped. An exact gradient this shape was implemented in DecayFit24.cpp and
// measured against central differences: no clear win (see the note above
// DecayFit24::modelf and PRD-010's Phase 7), so it was declined and removed
// from production, the same call already made for DecayFit26 at N=1. This
// section still reuses the real production kernels (fconv_per_cs_ad, Wcm_ad)
// and reimplements only the model-specific glue (correct_input's clamps), so
// it keeps proving the math was right independent of whether it is used.
// --------------------------------------------------------------------------

namespace decay24 {

constexpr int FN = 32;
constexpr double DT = 0.5;
constexpr double kMinTau = 0.001;

/// Same shape as DecayFit24::correct_input + modelf + normM_p2s + Wcm.
/// param is [tau1, gamma, tau2, A2, offset]. A2/gamma reproduce
/// correct_input's three-way clamp exactly, including the gamma upper-bound
/// test referencing `0.999 - A2` while the clamped value is the flat
/// constant `0.999` -- that looks like a latent bug in correct_input (see the
/// note above DecayFit24::modelf in DecayFit24.cpp), and reproducing it
/// exactly, not fixing it, is the point of this cross-check.
template <typename T>
T cost(const T param[5], double Sp, double Ss, const double* irf,
      const double* bg, const int* counts) {
    const T tau1 = decay23::soft_floor_ad(param[0], kMinTau);
    const T tau2 = decay23::soft_floor_ad(param[2], kMinTau);

    T A2;
    if (param[3] < 0.) A2 = T(0.);
    else if (param[3] > 0.999) A2 = T(0.999);
    else A2 = param[3];

    T gamma;
    if (param[1] < 0.) gamma = T(0.);
    else if (param[1] > 0.999 - A2) gamma = T(0.999);
    else gamma = param[1];

    T offset = (param[4] < 0.) ? T(0.) : param[4];
    offset = offset / double(FN);

    const double period = 2.0 * FN, dt = DT;
    const int conv_stop = FN / 2 - 1;

    T spectrum[4] = {T(1.) - A2, tau1, A2, tau2};
    std::vector<T> model(2 * FN);
    fconv_per_cs_ad(model.data(), spectrum, irf, 2, FN - 1, FN, period, conv_stop, dt);
    fconv_per_cs_ad(model.data() + FN, spectrum, irf + FN, 2, FN - 1, FN, period, conv_stop, dt);

    T sum_m(0.);
    double sum_s = 0.;
    for (int i = 0; i < 2 * FN; ++i) { sum_m += model[i]; sum_s += bg[i]; }
    for (int i = 0; i < 2 * FN; ++i)
        model[i] = model[i] * (1. - gamma) / sum_m + (bg[i] * gamma) / sum_s + offset;

    T s1(0.);
    for (int i = 0; i < FN; ++i) s1 += model[i];
    if (s1 > 0.) { const T scale = Sp / s1; for (int i = 0; i < FN; ++i) model[i] *= scale; }
    T s2(0.);
    for (int i = FN; i < 2 * FN; ++i) s2 += model[i];
    if (s2 > 0.) { const T scale = Ss / s2; for (int i = FN; i < 2 * FN; ++i) model[i] *= scale; }

    return Wcm_ad(counts, model.data(), FN) / double(FN);
}

static void run() {
    std::printf("DecayFit24 objective: vectorized dual vs central differences\n");

    static std::vector<double> irf(2 * FN, 0.0), bg(2 * FN, 0.2);
    for (int half = 0; half < 2; ++half)
        for (int i = 0; i < FN; ++i)
            irf[half * FN + i] = std::exp(-((i - 6.0) * (i - 6.0)) / (2 * 0.6 * 0.6));

    std::vector<int> counts(2 * FN);
    double Sp = 0.0, Ss = 0.0;
    for (int i = 0; i < FN; ++i) {
        const double t = i * DT;
        const double vv = 400.0 * (0.7 * std::exp(-t / 3.8) + 0.3 * std::exp(-t / 0.8)) + 20.0;
        const double vh = 380.0 * (0.7 * std::exp(-t / 3.8) + 0.3 * std::exp(-t / 0.8)) + 18.0;
        counts[i] = (int)std::floor(vv) + (i % 3);
        counts[FN + i] = (int)std::floor(vh) + (i % 2);
        Sp += counts[i];
        Ss += counts[FN + i];
    }

    using Grad5 = GradVec<5>;
    using D5 = Dual<Grad5>;

    const double u[5] = {3.8, 0.02, 0.4, 0.8, 1.0};
    D5 ud[5];
    for (int j = 0; j < 5; ++j) ud[j] = D5(u[j], Grad5::Unit(j));
    const D5 rv = cost<D5>(ud, Sp, Ss, irf.data(), bg.data(), counts.data());

    const double fd0 = cost<double>(u, Sp, Ss, irf.data(), bg.data(), counts.data());
    report(std::fabs(rv.val - fd0) < 1e-12, "value", std::fabs(rv.val - fd0), 1e-12);

    double gc[5];
    const double eps13 = std::cbrt(2.220446049250313e-16);
    double x[5] = {u[0], u[1], u[2], u[3], u[4]};
    for (int j = 0; j < 5; ++j) {
        const double h = eps13 * std::max(std::fabs(u[j]), 1.0);
        x[j] = u[j] + h;
        const double fp = cost<double>(x, Sp, Ss, irf.data(), bg.data(), counts.data());
        x[j] = u[j] - h;
        const double fm = cost<double>(x, Sp, Ss, irf.data(), bg.data(), counts.data());
        x[j] = u[j];
        gc[j] = (fp - fm) / (2.0 * h);
    }

    double max_rel = 0.0;
    for (int j = 0; j < 5; ++j) {
        const double scale = std::max(std::fabs(gc[j]), 1e-8);
        max_rel = std::max(max_rel, std::fabs(rv.grad[j] - gc[j]) / scale);
    }
    report(max_rel < 1e-6, "gradient vs central differences", max_rel, 1e-6);
}

}  // namespace decay24

int main() {
    const std::vector<double> data = make_data();

    // An interior point of the unconstrained space, away from any symmetry.
    const double u[NP] = {
        0.31, -0.22, 55.0, std::log(1.4), std::log(1.1), std::log(2.5),
        -0.7,  0.45, 12.0, 0.9, -1.3, 7.0
    };

    test_gradvec_algebra();
    test_dual_algebra();

    std::printf("vectorized dual vs scalar dual\n");
    double gv[NP], gs[NP], gl[NP], gc[NP];
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
    // than rediscovering. `Dual::operator*=` computes `grad*other.val +
    // val*aux`, and whether the compiler contracts that into an FMA depends on
    // the shape it is inlined into: it does for the scalar `grad`, and not
    // always for the loop over GradVec's array. Compiling this file with
    // `-ffp-contract=off` makes the two agree to the last bit; at clang's
    // default (`on`) one component can differ by 1 ulp.
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

    // Against long double. Scaled by the largest component, because a gradient
    // is consumed as a vector: a component passing through zero is not a
    // precision problem, and dividing by it would invent one.
    std::printf("vectorized dual vs long double\n");
    grad_long_double(u, data.data(), gl);
    double ginf = 0.0;
    for (int j = 0; j < NP; ++j) ginf = std::max(ginf, std::fabs(gl[j]));
    double max_ld = 0.0;
    for (int j = 0; j < NP; ++j) max_ld = std::max(max_ld, std::fabs(gv[j] - gl[j]) / ginf);
    report(max_ld < 1e-14, "gradient: vectorized vs long-double dual", max_ld, 1e-14);

    std::printf("vectorized dual vs central differences\n");
    grad_central(u, data.data(), gc);
    double max_rel = 0.0;
    for (int j = 0; j < NP; ++j) {
        const double scale = std::max(std::fabs(gc[j]), 1e-8);
        max_rel = std::max(max_rel, std::fabs(gv[j] - gc[j]) / scale);
    }
    report(max_rel < 1e-6, "gradient: vectorized vs central differences", max_rel, 1e-6);

    decay23::run();
    decay24::run();

    if (g_failures) std::printf("\n%d check(s) FAILED\n", g_failures);
    else            std::printf("\nall checks passed\n");
    return g_failures;
}
