"""Vectorised 32-bit RNG and normals for the propagation kernel (SimSimd.h).

This is a primitive, not yet wired into the step -- see SimSimd.h for why (it cannot
reproduce the scalar ziggurat's stream, because a ziggurat is a table lookup and neither
NEON nor baseline SSE2 has a gather). So it is validated statistically, which is the only
thing that can be asserted about it.
"""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "SimRandomV"):
    pytest.skip("tttrlib built without the SIMD primitive", allow_module_level=True)


def _draw(n, seed=4242):
    rng = tttrlib.SimRandomV()
    rng.seed(seed)
    return np.asarray(rng.normals(n), dtype=float)


def test_backend_is_reported():
    """The build must say which backend it compiled, and use a sane lane count."""
    backend = tttrlib.sim_simd_backend()
    lanes = tttrlib.sim_simd_lanes()
    assert backend in ("neon", "avx2", "sse2", "scalar"), backend
    assert lanes in (4, 8), lanes
    assert (lanes == 8) == (backend == "avx2"), (
        f"only the AVX2 backend is 8 lanes, got {backend} with {lanes}")


def test_normals_are_standard_normal():
    """Mean, variance and shape of the generated normals.

    Tolerances are set from the sample size: with n = 2e6 the standard error on the mean is
    1/sqrt(n) = 7e-4, so 0.01 is ~14 sigma and cannot flake, while a real defect (a wrong
    scale, a missing sign) moves these by 0.1 or more.
    """
    x = _draw(2_000_000)
    assert x.size >= 1_999_000
    assert abs(x.mean()) < 0.01, f"mean {x.mean():+.4f}"
    assert abs(x.std() - 1.0) < 0.01, f"sd {x.std():.4f}"
    from scipy import stats
    assert abs(stats.skew(x)) < 0.02, f"skew {stats.skew(x):+.4f}"
    assert abs(stats.kurtosis(x)) < 0.05, f"excess kurtosis {stats.kurtosis(x):+.4f}"


def test_both_signs_are_produced():
    """A sign bug is invisible in |z| but obvious here.

    The AVX2 backend originally folded the top bit of the 32-bit draw back into the float by
    testing the extracted sign bit *as a float* -- but that bit pattern is -0.0, and IEEE
    says -0.0 == 0.0, so the test never fired and the uniform was confined to [0, 0.5).
    Every normal then came out negative while `P(|z| > t)` stayed perfect at every t. Mean
    and variance catch it; the tail fractions do not.
    """
    x = _draw(200_000)
    frac_pos = float((x > 0).mean())
    assert 0.48 < frac_pos < 0.52, f"sign balance is broken: {frac_pos:.3f} positive"


@pytest.mark.parametrize("t,exact", [(1.0, 0.317311), (2.0, 0.045500),
                                     (3.0, 0.002700), (4.0, 0.0000633)])
def test_tails_are_not_truncated(t, exact):
    """The tail must survive, which the central quantile branch alone does not manage.

    With only Acklam's central rational form the distribution is hard-truncated at
    |z| = 3.22: measured P(|z|>3) was 0.00184 against 0.00270 and P(|z|>3.5) was exactly
    zero. That is invisible in the mean, the variance and a KS test, and it would silently
    delete every large diffusion step -- which is exactly what a barrier's thickness rule
    is stated against. The tail branch fixes it; this pins the fix.
    """
    x = _draw(2_000_000)
    observed = float((np.abs(x) > t).mean())
    # Poisson counting error on the tail count, widened to 4 sigma, floored so the
    # rarest bin does not demand more samples than we draw.
    n_expect = exact * x.size
    tol = max(4.0 * np.sqrt(max(n_expect, 1.0)) / x.size, 0.15 * exact)
    assert abs(observed - exact) < tol, (
        f"P(|z|>{t}) = {observed:.6f}, expected {exact:.6f} +/- {tol:.6f}")


def test_lanes_are_independent():
    """Lanes are separate generators, so they must not correlate.

    Seeding them from one key with a weak mixer would show up here as structure between
    lanes -- which matters because the intended use keys a lane per molecule.
    """
    lanes = tttrlib.sim_simd_lanes()
    x = _draw(400_000)
    m = x[:x.size // lanes * lanes].reshape(-1, lanes)
    c = np.corrcoef(m.T)
    off = np.abs(c - np.eye(lanes)).max()
    assert off < 0.02, f"lanes correlate: max |off-diagonal| = {off:.4f}"


def test_seeding_is_deterministic_and_seed_dependent():
    a1, a2 = _draw(4096, seed=7), _draw(4096, seed=7)
    b = _draw(4096, seed=8)
    assert np.array_equal(a1, a2), "same seed must give the same stream"
    assert not np.array_equal(a1, b), "different seeds must give different streams"
