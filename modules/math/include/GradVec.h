// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_GRADVEC_H
#define TTTRLIB_GRADVEC_H

/// Fixed-size vector of doubles used as the *derivative part* of a
/// vectorized forward-mode dual number.
///
/// Forward-mode AD carries one derivative alongside each value. Seeding the
/// derivative with a whole N-vector instead -- e_j in slot j -- propagates all
/// N partial derivatives through a single evaluation of the objective, which is
/// what makes forward mode cheaper than the 2N objective evaluations a central
/// difference gradient costs.
///
/// The operator set below is not general-purpose: it is exactly what
/// `autodiff::detail::Dual<double, G>` calls on its `grad` member -- copy, `=`
/// from a scalar zero, unary minus, `+= -= *= /=`, and scalar-by-vector
/// products in both orders. Nothing else is provided on purpose; an operation
/// autodiff does not use has no test that would notice it being wrong.
///
/// This replaces `Eigen::Array<double, N, 1>`, which was the only use of Eigen
/// left in the library and made a header-only third-party package a hard
/// `REQUIRED` of the whole build -- CI packages on four platforms, a vcpkg port,
/// a wheel-builder image -- for one struct member in one file. Measured against
/// it in `benchmarks/bench_gradvec.cpp`: parity at N=9, 13-16% behind at N=6 and
/// N=12. Not free, and not rounded away; see PERF.md. Two ideas that did *not*
/// help are recorded there too, so they are not retried: `alignas(32)` (slower,
/// it inflates every Dual) and padding N to the SIMD width.
///
/// A `NumberTraits<GradVec<N>>` specialization is required before
/// `Dual<double, GradVec<N>>` will compile -- autodiff cannot otherwise deduce
/// the underlying floating-point type. It lives with the consumer rather than
/// here, so this header stays free of any autodiff include, and it is
/// undocumented upstream: `test/cpp/test_ad_gradient.cpp` is what stands between
/// an autodiff bump and a silently wrong derivative.

namespace tttrlib {

template <int N>
struct GradVec;

/// `scalar * grad`, not evaluated yet.
///
/// autodiff never uses that product on its own -- every occurrence is
/// immediately accumulated: `grad += val * aux` in the product rule,
/// `grad -= val * other.grad` in the quotient rule. Returning a plain GradVec
/// would materialise an N-double temporary per operation and then run a second
/// loop to add it. This proxy is the smallest thing that lets the multiply and
/// the accumulate fuse into one pass, which is the part of Eigen's
/// expression-template machinery that actually matters here. It holds a
/// reference and must not outlive the expression it appears in -- which is
/// exactly the guarantee an operand of `+=` has.
template <int N>
struct ScaledGradVec {
    double s;
    const GradVec<N>& g;
};

template <int N>
struct GradVec {
    double v[N];

    GradVec() = default;

    /// Broadcast. autodiff assigns `Zero<G>()`, which is a plain `double`.
    GradVec(double s) {
        for (int i = 0; i < N; ++i) v[i] = s;
    }

    GradVec& operator=(double s) {
        for (int i = 0; i < N; ++i) v[i] = s;
        return *this;
    }

    double& operator[](int i) { return v[i]; }
    const double& operator[](int i) const { return v[i]; }

    static GradVec Zero() { return GradVec(0.0); }

    /// The j-th canonical basis vector -- the seed for parameter j.
    static GradVec Unit(int j) {
        GradVec g(0.0);
        g.v[j] = 1.0;
        return g;
    }

    GradVec& operator+=(const GradVec& o) {
        for (int i = 0; i < N; ++i) v[i] += o.v[i];
        return *this;
    }

    GradVec& operator-=(const GradVec& o) {
        for (int i = 0; i < N; ++i) v[i] -= o.v[i];
        return *this;
    }

    /// Fused `this += s * g`, `this -= s * g`, `this = s * g` -- see
    /// ScaledGradVec. One pass, no temporary.
    GradVec& operator+=(const ScaledGradVec<N>& e) {
        for (int i = 0; i < N; ++i) v[i] += e.s * e.g.v[i];
        return *this;
    }

    GradVec& operator-=(const ScaledGradVec<N>& e) {
        for (int i = 0; i < N; ++i) v[i] -= e.s * e.g.v[i];
        return *this;
    }

    GradVec& operator=(const ScaledGradVec<N>& e) {
        for (int i = 0; i < N; ++i) v[i] = e.s * e.g.v[i];
        return *this;
    }

    GradVec(const ScaledGradVec<N>& e) {
        for (int i = 0; i < N; ++i) v[i] = e.s * e.g.v[i];
    }

    GradVec& operator*=(double s) {
        for (int i = 0; i < N; ++i) v[i] *= s;
        return *this;
    }

    /// True division, not a reciprocal multiply. `x / s` and `x * (1/s)` differ
    /// in the last place, and autodiff's scalar `Dual<double, double>` divides
    /// -- so the shortcut would make the vectorized gradient disagree with the
    /// reference it is tested against by 1 ulp, for nothing. The divisor is a
    /// loop invariant either way and this runs once per objective evaluation.
    GradVec& operator/=(double s) {
        for (int i = 0; i < N; ++i) v[i] /= s;
        return *this;
    }

    GradVec operator-() const {
        GradVec r;
        for (int i = 0; i < N; ++i) r.v[i] = -v[i];
        return r;
    }
};

template <int N>
inline ScaledGradVec<N> operator*(double s, const GradVec<N>& g) {
    return ScaledGradVec<N>{s, g};
}

template <int N>
inline ScaledGradVec<N> operator*(const GradVec<N>& g, double s) {
    return ScaledGradVec<N>{s, g};
}

template <int N>
inline GradVec<N> operator/(const GradVec<N>& g, double s) {
    GradVec<N> r;
    for (int i = 0; i < N; ++i) r.v[i] = g.v[i] / s;
    return r;
}

template <int N>
inline GradVec<N> operator+(const GradVec<N>& a, const GradVec<N>& b) {
    GradVec<N> r = a;
    r += b;
    return r;
}

template <int N>
inline GradVec<N> operator-(const GradVec<N>& a, const GradVec<N>& b) {
    GradVec<N> r = a;
    r -= b;
    return r;
}

}  // namespace tttrlib

#endif  // TTTRLIB_GRADVEC_H
