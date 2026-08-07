/*!
 * \file SimInjection.h
 * \brief Boundary-flux mathematics for the open-volume simulator.
 *
 * The open volume is an absorbing ellipsoid held in steady state by injecting molecules
 * across its surface. These are the distributions that flux obeys, split out of
 * `SimEngine.cpp` because they are needed from more than one place: the window engine
 * injects per window, the independent-molecule engine draws a whole horizon's worth of
 * births up front, and both must sample the *same* surface distribution. They did not,
 * once — the independent path kept a diffusive-only copy after the window path was made
 * advection-aware, and the resulting bias was invisible in the molecule count.
 *
 * Everything here is inline or a template, so there is no call across a translation-unit
 * boundary in the step/injection path.
 */
#ifndef TTTRLIB_SIMINJECTION_H
#define TTTRLIB_SIMINJECTION_H

#include <cmath>

namespace tttrlib {
namespace sim_detail {

constexpr double kInjPi = 3.14159265358979;

/// Integral of the standard normal from x to infinity (ccmath qnorm), for random_erfc.
inline double qnorm(double x) {
    double y, ro, f, t; int k, nf;
    if (x < 0.) { x = -x; nf = 0; } else nf = 1;
    y = x * x; ro = std::exp(-y / 2.) / 2.506628274631;
    if (x < 3.) { f = t = 1.;
        for (k = 1; t > 1.e-14;) { t *= y / (k += 2); f += t; }
        f = .5 - x * ro * f; }
    else { f = x; k = int(std::ceil(250. / y)); if (k < 3) k = 3;
        for (; k > 0;) f = x + (k--) / f;
        f = ro / f; }
    return nf ? f : 1. - f;
}

/// Random number with p(x) ~ erf(x/sqrt(2)); the legacy surface penetration depth.
/// This is the zero-drift special case of `random_entry_depth` and is kept as its own
/// sampler so that a no-flow run consumes exactly the random numbers it always did.
template <class Rng>
double random_erfc(Rng& rng) {
    const double sqrt_pi_half = 1.2533141373155;
    double v, x, yv;
    do {
        v = rng.random0e1e();
        x = -std::log(v) * sqrt_pi_half;
        yv = v * rng.random0i1e();
    } while (yv > 2. * qnorm(x));
    return x;
}

/// Standard-normal PDF.
inline double std_phi(double u) {
    return std::exp(-0.5 * u * u) / std::sqrt(2. * kInjPi);
}

/// Standard-normal CDF.
inline double std_Phi(double u) { return 0.5 * std::erfc(-u / std::sqrt(2.)); }

/*!
 * \brief Per-area influx weight across a surface, `E[s+]` for `s ~ N(mu, sigma^2)`.
 *
 * A molecule at depth `y > 0` outside the surface enters if its inward normal
 * displacement `s` exceeds `y`, so the influx per unit area is
 * `integral_0^inf P(s > y) dy = E[s+] = mu*Phi(mu/sigma) + sigma*phi(mu/sigma)`,
 * with `mu = -(v . n_out)*dt` (positive on the upstream face) and
 * `sigma = sqrt(2*D*dt)`.
 *
 * At `mu = 0` this is `sigma/sqrt(2*pi)` — exactly the diffusion-only formula the
 * simulator used before flow existed.
 */
inline double influx_weight(double mu, double sigma) {
    if (sigma <= 0.0) return (mu > 0.0) ? mu : 0.0;
    const double u = mu / sigma;
    return mu * std_Phi(u) + sigma * std_phi(u);
}

/*!
 * \brief Sample the entry depth `h >= 0` with density proportional to `Q((h-mu)/sigma)`.
 *
 * Rejection against a uniform envelope on `[0, max(mu,0) + 8*sigma]`; the truncation
 * error is `Q(8) ~ 6e-16`. At `mu = 0` callers use `random_erfc` instead, so the no-flow
 * path keeps its historical random-number stream.
 */
template <class Rng>
double random_entry_depth(Rng& rng, double mu, double sigma) {
    const double hi = ((mu > 0.0) ? mu : 0.0) + 8.0 * sigma;
    for (int k = 0; k < 1000; ++k) {
        const double h = rng.random0i1e() * hi;
        const double q = 0.5 * std::erfc((h - mu) / (sigma * std::sqrt(2.)));
        if (rng.random0i1e() < q) return h;
    }
    return 0.0;
}

} // namespace sim_detail
} // namespace tttrlib

#endif // TTTRLIB_SIMINJECTION_H
