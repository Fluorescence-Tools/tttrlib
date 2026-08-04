"""Digamma and log-gamma, without SciPy.

The variational updates need ``psi``, and the ELBO needs Dirichlet KL
divergences, which need both ``psi`` and ``lgamma``.  SciPy is not available --
tttrlib takes no new dependencies, and the C++ core is standard-library only,
where ``std::lgamma`` exists but ``psi`` does not.  So ``digamma`` has to be
written out anyway for the port; writing it here first means the C++ version is
a transcription of something already tested.

``digamma`` uses the standard recurrence up to the asymptotic region followed by
the Stirling-type series

    psi(x) ~ ln x - 1/(2x) - 1/(12x^2) + 1/(120x^4) - 1/(252x^6) + ...

which is accurate to ~1e-14 once ``x >= 6``.
"""
from __future__ import annotations

from math import lgamma as _lgamma

import numpy as np

__all__ = ["digamma", "gammaln", "dirichlet_kl", "gammainc_upper", "chi2_sf"]


_C = np.array([1.0 / 12.0, 1.0 / 120.0, 1.0 / 252.0, 1.0 / 240.0, 1.0 / 132.0])


def digamma(x):
    """``psi(x)`` for strictly positive ``x``, elementwise."""
    x = np.asarray(x, dtype=np.float64)
    if np.any(x <= 0):
        raise ValueError("digamma requires x > 0")
    x = x.copy()
    out = np.zeros_like(x)
    # psi(x) = psi(x + 1) - 1/x, applied until the series converges quickly
    small = x < 6.0
    while np.any(small):
        out[small] -= 1.0 / x[small]
        x[small] += 1.0
        small = x < 6.0
    f = 1.0 / (x * x)
    series = _C[0] - f * (_C[1] - f * (_C[2] - f * (_C[3] - f * _C[4])))
    out += np.log(x) - 0.5 / x - f * series
    return out


def gammaln(x):
    """``log |Gamma(x)|``, elementwise, via the standard library."""
    x = np.asarray(x, dtype=np.float64)
    flat = x.ravel()
    out = np.empty_like(flat)
    for i in range(flat.size):
        out[i] = _lgamma(flat[i])
    return out.reshape(x.shape)


def _gamma_series(a, x, max_iter=1000, tol=1e-16):
    """Regularised lower incomplete gamma ``P(a, x)`` by series; use for x < a+1."""
    ap = a
    term = 1.0 / a
    total = term
    for _ in range(max_iter):
        ap += 1.0
        term *= x / ap
        total += term
        if abs(term) < abs(total) * tol:
            break
    return total * np.exp(-x + a * np.log(x) - _lgamma(a))


def _gamma_cf(a, x, max_iter=1000, tol=1e-16):
    """Regularised upper incomplete gamma ``Q(a, x)`` by continued fraction; x >= a+1."""
    tiny = 1e-300
    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / b
    h = d
    for i in range(1, max_iter):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        if abs(d) < tiny:
            d = tiny
        c = b + an / c
        if abs(c) < tiny:
            c = tiny
        d = 1.0 / d
        delta = d * c
        h *= delta
        if abs(delta - 1.0) < tol:
            break
    return np.exp(-x + a * np.log(x) - _lgamma(a)) * h


def gammainc_upper(a, x):
    """Regularised upper incomplete gamma ``Q(a, x) = 1 - P(a, x)``.

    Written out for the same reason as ``digamma``: no SciPy here, and no
    incomplete gamma in the C++ standard library either, so the port needs it.
    """
    a = float(a)
    x = float(x)
    if a <= 0.0:
        raise ValueError("gammainc_upper requires a > 0")
    if x < 0.0:
        raise ValueError("gammainc_upper requires x >= 0")
    if x == 0.0:
        return 1.0
    if x < a + 1.0:
        return 1.0 - _gamma_series(a, x)
    return _gamma_cf(a, x)


def chi2_sf(x, df):
    """Upper tail of the chi-square distribution: ``P(X > x)`` with ``df``."""
    if x <= 0.0:
        return 1.0
    return gammainc_upper(0.5 * df, 0.5 * x)


def dirichlet_kl(a, b):
    """``KL(Dir(a) || Dir(b))``, summed over the last axis.

    Both arguments broadcast over leading axes, so a whole transition matrix's
    rows are handled in one call.
    """
    a = np.atleast_2d(np.asarray(a, dtype=np.float64))
    b = np.atleast_2d(np.broadcast_to(np.asarray(b, dtype=np.float64), a.shape))
    a0 = a.sum(axis=-1)
    b0 = b.sum(axis=-1)
    return (gammaln(a0) - gammaln(a).sum(axis=-1)
            - gammaln(b0) + gammaln(b).sum(axis=-1)
            + ((a - b) * (digamma(a) - digamma(a0)[..., None])).sum(axis=-1))
