"""K-channel burst-wise PDA likelihood.

The oracle is a brute-force evaluation of the defining nested sum, written
straight from the formula and independent of every optimization in the C++ path
(the factorization, the background box, the peak shifts, the exact/box switch).
"""
from __future__ import division

import itertools
from math import exp, lgamma, log

import numpy as np
import pytest

import tttrlib

NEG_INF = -np.inf


def brute_force_log_likelihood(counts, p, background=None, pn=None):
    """L(F|p,B) = sum_{b<=F} [prod_c Pois(b_c;B_c)] P(n) Multinom(F-b;p).

    The definition, summed over every background assignment. Exponential in the
    counts, so only usable on tiny inputs -- which is the point: it shares no
    machinery with the implementation.
    """
    counts = np.asarray(counts, dtype=int)
    K = len(counts)
    background = np.zeros(K) if background is None else np.asarray(background, float)

    def pois(b, lam):
        if lam <= 0.0:
            return 1.0 if b == 0 else 0.0
        return exp(b * log(lam) - lam - lgamma(b + 1.0))

    total = 0.0
    for b in itertools.product(*[range(f + 1) for f in counts]):
        w = 1.0
        for c in range(K):
            w *= pois(b[c], background[c])
        if w == 0.0:
            continue
        signal = counts - np.asarray(b)
        n = int(signal.sum())
        if pn is not None:
            w *= pn[n] if n < len(pn) else 0.0
            if w == 0.0:
                continue
        # multinomial of the signal photons
        m = lgamma(n + 1.0)
        for c in range(K):
            m -= lgamma(signal[c] + 1.0)
            if p[c] > 0.0:
                m += signal[c] * log(p[c])
            elif signal[c] > 0:
                m = NEG_INF
                break
        if m == NEG_INF:
            continue
        total += w * exp(m)
    return log(total) if total > 0.0 else NEG_INF


def make(counts, background=None, pn=None, tol=1e-12):
    return tttrlib.PdaBurstLikelihood(
        np.asarray(counts, dtype=np.int32),
        [] if background is None else list(background),
        [] if pn is None else list(pn),
        tol,
    )


@pytest.mark.parametrize("background", [None, [0.5, 0.5, 0.5], [2.0, 0.0, 1.0]])
def test_matches_the_defining_nested_sum(background):
    counts = [[3, 2, 1], [4, 0, 2], [0, 0, 5]]
    p = np.array([[0.5, 0.3, 0.2], [0.2, 0.2, 0.6]])
    got = make(counts, background).log_likelihood_grid(p)
    for i, pi in enumerate(p):
        for j, cj in enumerate(counts):
            ref = brute_force_log_likelihood(cj, pi, background)
            assert got[i, j] == pytest.approx(ref, rel=1e-10, abs=1e-12)


def test_matches_the_nested_sum_with_a_photon_number_distribution():
    counts = [[3, 2, 1], [2, 2, 2]]
    p = np.array([[0.4, 0.35, 0.25]])
    pn = np.zeros(20)
    lam = 5.0
    pn[0] = exp(-lam)
    for i in range(1, 20):
        pn[i] = pn[i - 1] * lam / i
    pn /= pn.sum()
    got = make(counts, [1.0, 1.0, 1.0], pn).log_likelihood_grid(p)
    for j, cj in enumerate(counts):
        ref = brute_force_log_likelihood(cj, p[0], [1.0, 1.0, 1.0], pn)
        assert got[0, j] == pytest.approx(ref, rel=1e-10, abs=1e-12)


def test_two_channels_is_just_the_binomial_case():
    counts = [[4, 2], [1, 5]]
    p = np.array([[0.6, 0.4]])
    got = make(counts, [0.7, 0.3]).log_likelihood_grid(p)
    for j, cj in enumerate(counts):
        ref = brute_force_log_likelihood(cj, p[0], [0.7, 0.3])
        assert got[0, j] == pytest.approx(ref, rel=1e-10, abs=1e-12)


def test_no_background_is_the_plain_multinomial():
    counts = np.array([[5, 3, 2], [10, 0, 1]])
    p = np.array([0.5, 0.3, 0.2])
    got = make(counts).log_likelihood(p)
    for j, cj in enumerate(counts):
        n = cj.sum()
        ref = lgamma(n + 1.0) - sum(lgamma(c + 1.0) for c in cj) \
              + sum(c * log(q) for c, q in zip(cj, p))
        assert got[j] == pytest.approx(ref, rel=0, abs=1e-12)


def test_exact_and_box_paths_agree():
    """The implementation switches between a truncated box and the exact
    convolution on a cost estimate. Both must give the same number."""
    counts = np.array([[12, 9, 4], [20, 3, 7], [2, 2, 25]], dtype=np.int32)
    p = np.array([[0.45, 0.35, 0.20], [0.6, 0.25, 0.15]])
    fast = make(counts, [1.5, 1.0, 0.8]).log_likelihood_grid(p)
    for i, pi in enumerate(p):
        for j, cj in enumerate(counts):
            exact = tttrlib.PdaBurstLikelihood.log_background_correction(
                cj.astype(float), [1.5, 1.0, 0.8], pi)
            mult = tttrlib.PdaBurstLikelihood.log_multinomial_pmf(
                cj.astype(float), pi)
            assert fast[i, j] == pytest.approx(mult + exact, rel=1e-11, abs=1e-11)


def test_a_near_impossible_channel_does_not_score_a_perfect_fit():
    """The model half of the factorisation is an unscaled prod p_c^-b_c that
    overflows to inf, and log(inf) reads as a +inf log-likelihood -- a perfect
    fit. Reachable whenever a short-distance node puts p ~ 1e-12 on a channel
    that collected photons."""
    counts = np.array([[5, 5, 3], [10, 10, 2]], dtype=np.int32)
    p = np.array([[0.5, 0.5 - 1e-12, 1e-12], [0.499999, 0.5, 1e-6]])
    got = make(counts, [1.0, 1.0, 1.0]).log_likelihood_grid(p)
    assert np.isfinite(got).all()
    assert (got <= 0.0).all(), "a log-likelihood above zero is not a probability"
    for i, pi in enumerate(p):
        for j, cj in enumerate(counts):
            exact = tttrlib.PdaBurstLikelihood.log_background_correction(
                cj.astype(float), [1.0, 1.0, 1.0], pi)
            mult = tttrlib.PdaBurstLikelihood.log_multinomial_pmf(
                cj.astype(float), pi)
            assert got[i, j] == pytest.approx(mult + exact, rel=1e-10, abs=1e-10)


def test_background_box_covers_an_over_bright_channel():
    """Truncating the series on Poisson tail mass alone discards the dominant
    terms wherever a channel collected far more photons than the model allows --
    and there the background explanation is the whole likelihood."""
    counts = np.array([[2, 2, 40]], dtype=np.int32)
    p = np.array([[0.49, 0.49, 0.02]])
    obj = make(counts, [1.0, 1.0, 3.0])
    got = obj.log_likelihood_grid(p)
    exact = tttrlib.PdaBurstLikelihood.log_background_correction(
        counts[0].astype(float), [1.0, 1.0, 3.0], p[0])
    mult = tttrlib.PdaBurstLikelihood.log_multinomial_pmf(
        counts[0].astype(float), p[0])
    assert got[0, 0] == pytest.approx(mult + exact, rel=1e-10, abs=1e-10)
    assert obj.get_boxes()[2] > 1, "channel 3's box must not collapse"


def test_total_log_likelihood_is_the_row_sum():
    counts = np.array([[6, 4, 2], [3, 3, 3], [9, 1, 1]], dtype=np.int32)
    p = np.array([[0.5, 0.3, 0.2], [0.4, 0.4, 0.2], [0.7, 0.2, 0.1]])
    obj = make(counts, [1.0, 0.5, 0.5])
    grid = obj.log_likelihood_grid(p)
    np.testing.assert_allclose(obj.total_log_likelihood(p), grid.sum(axis=1),
                               rtol=1e-12, atol=0)


def test_photons_in_a_zero_probability_channel_are_impossible_without_background():
    counts = np.array([[1, 0, 2]], dtype=np.int32)
    p = np.array([[0.5, 0.5, 0.0]])
    assert make(counts).log_likelihood_grid(p)[0, 0] == NEG_INF
    # with background in that channel the burst becomes possible again
    assert np.isfinite(make(counts, [0.0, 0.0, 1.0]).log_likelihood_grid(p)[0, 0])


def test_shape_and_argument_validation():
    counts = np.array([[1, 2, 3]], dtype=np.int32)
    obj = make(counts)
    assert obj.get_n_bursts() == 1
    assert obj.get_n_channels() == 3
    with pytest.raises(Exception):
        obj.log_likelihood(np.array([0.5, 0.5]))          # wrong channel count
    with pytest.raises(Exception):
        make(np.array([[1, -2, 3]], dtype=np.int32))      # negative counts
