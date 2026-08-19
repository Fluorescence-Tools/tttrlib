// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DUAL_H
#define TTTRLIB_DUAL_H

// Validation: A/B-TESTED 2026-08-19 -- vs autodiff (A/B before autodiff was deleted), a long-double dual and central
//   differences on the localization objective; tanh/sin/cos/sqrt/pow/min/max vs hand derivatives and central
//   differences. test/cpp/test_ad_gradient.cpp, test/cpp/test_mlp_core.cpp.
//   Register: okf/testing/math-kernel-validation.md

#include <cmath>

/// Forward-mode dual number: a value and the derivative that travels with it.
///
/// `Dual<G>` is `val + eps*grad` with `eps^2 = 0`. Every operation carries the
/// chain rule along, so evaluating a templated objective at `Dual` arguments
/// yields the objective *and* its derivative in one pass. The derivative slot is
/// a template parameter: `Dual<double>` is one directional derivative, while
/// `Dual<GradVec<N>>` seeded with the N basis vectors returns the whole gradient
/// from a single evaluation -- the reason the localization fit costs one pass
/// instead of the 2N a central-difference gradient needs.
///
/// This replaces `autodiff::detail::Dual<double, G>`. autodiff was a vendored
/// header-only package of ~10k lines (forward dual, forward real, reverse var,
/// four Eigen bridges, Taylor series) of which this library used one class
/// template and two elementary functions, and it only worked at all because the
/// undocumented `NumberTraits` hook let a vector sit in a slot the documentation
/// describes as a scalar. That hook was the dependency: an upstream bump could
/// keep compiling and silently propagate wrong derivatives, which is why
/// test/cpp/test_ad_gradient.cpp existed. The contract is now ours, stated
/// below, and the test checks an implementation instead of guarding a guess
/// about someone else's.
///
/// What `G` must provide: copy, `+= -=` with `G`, `*= /=` with a `double`,
/// unary minus, and `double * G` (a proxy is fine -- see GradVec's
/// ScaledGradVec). `double` satisfies all of it, so `Dual<double>` is the scalar
/// reference the vectorized path is tested against.
///
/// Numerics: where the divisor is a *dual* -- `a/b`, `s/a`, and the `1/x` in
/// `log` -- this takes the reciprocal once and multiplies, which is both the
/// cheaper form and what autodiff did, so the conversion could be checked
/// against it as a numerical A/B rather than only as a "still converges" test.
/// Dividing by a plain scalar is a true division instead: there the reciprocal
/// saves nothing (one divisor, N+1 numbers) and would cost a last-place
/// disagreement with the scalar reference. See GradVec's `operator/=`.
///
/// Only the operators the objectives use are defined: `+ - * /` in every
/// dual/scalar combination, unary minus, compound assignment, comparison on the
/// value, `exp`, `log`, and -- for the network activations in MlpCore.h --
/// `tanh`, `sin`, `cos`, `sqrt`, `pow(dual, double)`, `min`, `max`. Nothing
/// speculative -- an operator with no consumer has no test that would notice
/// it being wrong, which is the trap this header is replacing.

namespace tttrlib {

/// How to make a zero derivative for a given carrier.
///
/// `GradVec<N>(0.0)` broadcasts, and so does `double(0.0)`, which covers both
/// carriers the library uses. Carriers that reject a scalar constructor --
/// `Eigen::Array<double, N, 1>` reads it as a size -- specialize this;
/// benchmarks/bench_gradvec.cpp does exactly that to keep measuring against
/// Eigen without the library depending on it.
template <typename G>
struct DualGradTraits {
    static G zero() { return G(0.0); }
};

template <typename G>
struct Dual {
    double val;
    G grad;

    Dual() : val(0.0), grad(DualGradTraits<G>::zero()) {}

    /// Explicit: a constant enters the computation with a zero derivative, and
    /// `T(0.5)` in a templated objective should be visibly a conversion.
    explicit Dual(double v) : val(v), grad(DualGradTraits<G>::zero()) {}

    Dual(double v, const G& g) : val(v), grad(g) {}

    /// Seed for parameter j of a gradient computation.
    static Dual variable(double v, const G& seed) { return Dual(v, seed); }

    Dual& operator+=(const Dual& o) {
        val += o.val;
        grad += o.grad;
        return *this;
    }

    Dual& operator-=(const Dual& o) {
        val -= o.val;
        grad -= o.grad;
        return *this;
    }

    /// Product rule. `aux` is taken before `grad` is touched so `x *= x` is
    /// correct, and `val` is updated last because the second term needs the old
    /// one.
    Dual& operator*=(const Dual& o) {
        const G aux = o.grad;
        grad *= o.val;
        grad += val * aux;
        val *= o.val;
        return *this;
    }

    /// Quotient rule, written so that `x /= x` is correct too.
    Dual& operator/=(const Dual& o) {
        const double inv = 1.0 / o.val;
        val *= inv;
        grad -= val * o.grad;
        grad *= inv;
        return *this;
    }

    Dual& operator+=(double s) { val += s; return *this; }
    Dual& operator-=(double s) { val -= s; return *this; }

    Dual& operator*=(double s) {
        val *= s;
        grad *= s;
        return *this;
    }

    /// True division by a scalar, not a reciprocal multiply: this one is a
    /// single divisor applied to N+1 numbers, so the shortcut buys nothing and
    /// would cost a last-place disagreement with the scalar reference.
    Dual& operator/=(double s) {
        val /= s;
        grad /= s;
        return *this;
    }

    Dual operator-() const { return Dual(-val, -grad); }
};

template <typename G>
inline Dual<G> operator+(Dual<G> a, const Dual<G>& b) { a += b; return a; }

template <typename G>
inline Dual<G> operator-(Dual<G> a, const Dual<G>& b) { a -= b; return a; }

template <typename G>
inline Dual<G> operator*(Dual<G> a, const Dual<G>& b) { a *= b; return a; }

template <typename G>
inline Dual<G> operator/(Dual<G> a, const Dual<G>& b) { a /= b; return a; }

template <typename G>
inline Dual<G> operator+(Dual<G> a, double s) { a += s; return a; }

template <typename G>
inline Dual<G> operator+(double s, Dual<G> a) { a += s; return a; }

template <typename G>
inline Dual<G> operator-(Dual<G> a, double s) { a -= s; return a; }

template <typename G>
inline Dual<G> operator-(double s, const Dual<G>& a) { return Dual<G>(s - a.val, -a.grad); }

template <typename G>
inline Dual<G> operator*(Dual<G> a, double s) { a *= s; return a; }

template <typename G>
inline Dual<G> operator*(double s, Dual<G> a) { a *= s; return a; }

template <typename G>
inline Dual<G> operator/(Dual<G> a, double s) { a /= s; return a; }

/// `s / a`: the scalar has no derivative, so this is the reciprocal rule.
template <typename G>
inline Dual<G> operator/(double s, const Dual<G>& a) {
    const double inv = 1.0 / a.val;
    const double v = s * inv;
    Dual<G> r(v, a.grad);
    r.grad *= -v * inv;
    return r;
}

/// Comparisons look at the value only -- ordering a derivative is meaningless,
/// and every comparison in the objectives is a data/branch test.
template <typename G> inline bool operator<(const Dual<G>& a, const Dual<G>& b) { return a.val < b.val; }
template <typename G> inline bool operator>(const Dual<G>& a, const Dual<G>& b) { return a.val > b.val; }
template <typename G> inline bool operator<=(const Dual<G>& a, const Dual<G>& b) { return a.val <= b.val; }
template <typename G> inline bool operator>=(const Dual<G>& a, const Dual<G>& b) { return a.val >= b.val; }
template <typename G> inline bool operator==(const Dual<G>& a, const Dual<G>& b) { return a.val == b.val; }
template <typename G> inline bool operator!=(const Dual<G>& a, const Dual<G>& b) { return a.val != b.val; }
template <typename G> inline bool operator<(const Dual<G>& a, double s) { return a.val < s; }
template <typename G> inline bool operator>(const Dual<G>& a, double s) { return a.val > s; }
template <typename G> inline bool operator<=(const Dual<G>& a, double s) { return a.val <= s; }
template <typename G> inline bool operator>=(const Dual<G>& a, double s) { return a.val >= s; }
template <typename G> inline bool operator==(const Dual<G>& a, double s) { return a.val == s; }
template <typename G> inline bool operator!=(const Dual<G>& a, double s) { return a.val != s; }
template <typename G> inline bool operator<(double s, const Dual<G>& a) { return s < a.val; }
template <typename G> inline bool operator>(double s, const Dual<G>& a) { return s > a.val; }
template <typename G> inline bool operator<=(double s, const Dual<G>& a) { return s <= a.val; }
template <typename G> inline bool operator>=(double s, const Dual<G>& a) { return s >= a.val; }
template <typename G> inline bool operator==(double s, const Dual<G>& a) { return s == a.val; }
template <typename G> inline bool operator!=(double s, const Dual<G>& a) { return s != a.val; }

/// `min`/`max` select by value and carry the winner's derivative: the
/// subgradient at a tie is the second argument's, which is what `a < b ? a : b`
/// gives and what a branch in a templated objective would give too.
template <typename G> inline Dual<G> min(const Dual<G>& a, const Dual<G>& b) { return a.val < b.val ? a : b; }
template <typename G> inline Dual<G> max(const Dual<G>& a, const Dual<G>& b) { return a.val > b.val ? a : b; }

/// d/dx exp(x) = exp(x) -- the value is the multiplier, so it is computed first.
template <typename G>
inline Dual<G> exp(const Dual<G>& a) {
    Dual<G> r(std::exp(a.val), a.grad);
    r.grad *= r.val;
    return r;
}

/// d/dx log(x) = 1/x.
template <typename G>
inline Dual<G> log(const Dual<G>& a) {
    const double inv = 1.0 / a.val;
    Dual<G> r(std::log(a.val), a.grad);
    r.grad *= inv;
    return r;
}

/// d/dx tanh(x) = 1 - tanh(x)^2.
template <typename G>
inline Dual<G> tanh(const Dual<G>& a) {
    const double t = std::tanh(a.val);
    Dual<G> r(t, a.grad);
    r.grad *= 1.0 - t * t;
    return r;
}

/// d/dx sin(x) = cos(x).
template <typename G>
inline Dual<G> sin(const Dual<G>& a) {
    Dual<G> r(std::sin(a.val), a.grad);
    r.grad *= std::cos(a.val);
    return r;
}

/// d/dx cos(x) = -sin(x).
template <typename G>
inline Dual<G> cos(const Dual<G>& a) {
    Dual<G> r(std::cos(a.val), a.grad);
    r.grad *= -std::sin(a.val);
    return r;
}

/// d/dx sqrt(x) = 1 / (2 sqrt(x)).
template <typename G>
inline Dual<G> sqrt(const Dual<G>& a) {
    const double s = std::sqrt(a.val);
    Dual<G> r(s, a.grad);
    r.grad *= 0.5 / s;
    return r;
}

/// d/dx x^p = p x^(p-1), for a constant exponent.
template <typename G>
inline Dual<G> pow(const Dual<G>& a, double p) {
    Dual<G> r(std::pow(a.val, p), a.grad);
    r.grad *= p * std::pow(a.val, p - 1.0);
    return r;
}

}  // namespace tttrlib

#endif  // TTTRLIB_DUAL_H
