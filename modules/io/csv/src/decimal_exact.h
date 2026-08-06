// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_CSV_DECIMAL_EXACT_H
#define TTTRLIB_IO_CSV_DECIMAL_EXACT_H

/*!
 * \file decimal_exact.h
 * \brief Deciding, exactly, whether a decimal and a double are the same number.
 *
 * Internal to the CSV module and shared by both directions, which is the point:
 * the reader and the writer have to agree on what `0.1` means down to the last
 * bit, or a table that goes out does not come back. One set of rules, used by
 * both, is how that is guaranteed rather than hoped for.
 *
 * There are two tools here and they answer the same question at different
 * costs:
 *
 * - **Clinger's conditions** (\ref kPow10). When the decimal mantissa is at
 *   most 2^53 and the power of ten is at most 10^22, both are doubles exactly,
 *   so a single multiply or divide is the only rounding in the whole
 *   conversion and lands on the double every correct parser gives. One
 *   instruction, and it covers everything with fifteen significant digits or
 *   fewer -- which is most measured data.
 *
 * - **The integer comparison** (\ref decimal_reads_back). Past fifteen digits
 *   the mantissa is no longer a double, and floating-point cannot be used to
 *   check floating-point. But a double is `M * 2^-F` and a decimal is
 *   `m * 10^-k`, both with integer parts, so "are these the same number after
 *   rounding" is `|m * 2^F - M * 10^k|` against a half-ulp of `10^k / 2` --
 *   integers throughout, 128 bits wide, no table beyond the powers of ten that
 *   fit in a machine word.
 *
 * Neither is an approximation and neither has a tolerance. When the magnitudes
 * fall outside what they can represent they say "cannot decide", and the caller
 * falls back to the platform's strtod or snprintf, which are correct and slow.
 * Being unable to decide is always a legal answer here. Being wrong is not.
 */

#include <cmath>

namespace tttrlib {
namespace io {
namespace decimal {

/// The powers of ten that are doubles exactly -- 10^23 is the first that is
/// not. std::pow would give the same values on a good libm, but "would" is the
/// wrong word in a function whose correctness turns on them being exact.
const double kPow10[23] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

/// The powers of ten that are 64-bit integers exactly. 10^20 does not fit,
/// which is what bounds the integer comparison below.
const unsigned long long kPow10Int[20] = {
    1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL, 1000000ULL, 10000000ULL,
    100000000ULL, 1000000000ULL, 10000000000ULL, 100000000000ULL,
    1000000000000ULL, 10000000000000ULL, 100000000000000ULL, 1000000000000000ULL,
    10000000000000000ULL, 100000000000000000ULL, 1000000000000000000ULL,
    10000000000000000000ULL,
};

/// Whether `m * 10^-k` is inside what Clinger's conditions can settle with one
/// floating-point operation. \see kPow10.
inline bool clinger_applies(unsigned long long m, int k) {
    return m <= (1ULL << 53) && k >= -22 && k <= 22;
}

/// The double for `m * 10^-k`, correctly rounded. Only call it when
/// \ref clinger_applies says so -- outside those conditions this is a rounding
/// too many and lands a ulp away, or at the ends of the range on zero.
inline double clinger_value(unsigned long long m, int k) {
    const double d = static_cast<double>(m);
    if (k > 0) return d / kPow10[k];
    if (k < 0) return d * kPow10[-k];
    return d;
}

#if defined(__SIZEOF_INT128__)
#define TTTRLIB_CSV_HAVE_INT128 1
using u128 = unsigned __int128;

/*!
 * Split `v` into `M * 2^-F`, with M in [2^52, 2^53).
 *
 * False when \ref decimal_reads_back cannot be run on the result: a value
 * whose exponent would overflow the 128-bit product, and a power of two, whose
 * ulp is not the same on both sides of it -- so "half an ulp" is not one
 * number and the comparison below would be asking the wrong question.
 */
inline bool split_double(double v, unsigned long long& M, int& F) {
    int e = 0;
    const double frac = std::frexp(v, &e);          // v == frac * 2^e, frac in [.5, 1)
    M = static_cast<unsigned long long>(std::ldexp(frac, 53));
    F = 53 - e;
    return F > 0 && F <= 66 && M != (1ULL << 52);
}

/*!
 * \brief Does the decimal `m * 10^-k` parse back to the double `M * 2^-F`?
 *
 * Exact. False also means "outside the range this can settle", so a false is
 * a reason to fall back rather than a proof of difference.
 */
inline bool reads_back(unsigned long long m, int k, unsigned long long M, int F) {
    if (k < 0 || k > 19 || m >= (1ULL << 57)) return false;
    const u128 p10 = static_cast<u128>(kPow10Int[k]);
    const u128 lhs = static_cast<u128>(m) << F;
    const u128 rhs = static_cast<u128>(M) * p10;
    const u128 diff = lhs > rhs ? lhs - rhs : rhs - lhs;
    const u128 twice = diff << 1;
    if (twice < p10) return true;
    // Exactly half an ulp away: a correct parser rounds to even, so this
    // decimal is that double only when the double's mantissa already is.
    return twice == p10 && (M & 1ULL) == 0ULL;
}

/// \see reads_back, for a caller that has a double rather than its parts.
inline bool reads_back(double v, unsigned long long m, int k) {
    unsigned long long M = 0;
    int F = 0;
    return split_double(v, M, F) && reads_back(m, k, M, F);
}
#endif  // __SIZEOF_INT128__

}  // namespace decimal
}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_CSV_DECIMAL_EXACT_H
