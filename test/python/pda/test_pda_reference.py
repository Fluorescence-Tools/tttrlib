"""Numerical reference tests for Photon Distribution Analysis.

Every assertion here is checked against an independent NumPy implementation of
the PDA model or against an exactly known synthetic input -- not against a
pinned array copied out of a previous run of this same code.
"""
from __future__ import division

from math import comb, exp, factorial

import numpy as np
import pytest

import tttrlib


def poisson_pf(lam, nmax):
    """Normalized Poisson P(F) over 0..nmax."""
    p = np.zeros(nmax + 1)
    p[0] = exp(-lam)
    for i in range(1, nmax + 1):
        p[i] = p[i - 1] * lam / i
    return p / p.sum()


def reference_s1s2(pf, amplitudes, probabilities_ch1, nmax, bg1, bg2):
    """Direct O(N^4) PDA model, written straight from the definition.

    F photons are split binomially between the two channels, then each channel
    is convolved with its own Poisson background. Row is channel 1.
    """
    n = nmax + 1
    fgfr = np.zeros((n, n))
    for a, p in zip(amplitudes, probabilities_ch1):
        for f in range(n):
            for red in range(f + 1):
                fgfr[f - red, red] += (
                    a * pf[f] * comb(f, red) * p ** (f - red) * (1 - p) ** red
                )
    pg = np.array([exp(-bg1) * bg1 ** i / factorial(i) for i in range(n)])
    pr = np.array([exp(-bg2) * bg2 ** i / factorial(i) for i in range(n)])
    s = np.zeros((n, n))
    for g in range(n):
        for r in range(n - g):
            s[g, r] = sum(
                fgfr[i, j] * pg[g - i] * pr[r - j]
                for i in range(g + 1)
                for j in range(r + 1)
            )
    return s


@pytest.mark.parametrize("bg1, bg2", [(0.0, 0.0), (1.5, 0.8), (3.0, 3.0)])
def test_model_matches_analytic_reference(bg1, bg2):
    """The model matrix equals the textbook definition to machine precision."""
    nmax = 12
    pf = poisson_pf(4.0, nmax)
    amplitudes, probs = [0.6, 0.4], [0.3, 0.85]

    pda = tttrlib.Pda(
        hist2d_nmax=nmax, hist2d_nmin=0,
        background_ch1=bg1, background_ch2=bg2, pF=pf.tolist(),
    )
    for a, p in zip(amplitudes, probs):
        pda.append(a, p)

    ref = reference_s1s2(pf, amplitudes, probs, nmax, bg1, bg2)
    np.testing.assert_allclose(pda.s1s2, ref, rtol=0, atol=1e-14)


def test_outermost_antidiagonal_is_not_truncated():
    """The corner cells of the ch1+ch2 == nmax diagonal carry real weight.

    Regression: the binomial scatter stopped one column short and the Poisson
    background kernel was built one tap short, so the whole outer diagonal came
    out wrong -- invisibly, because it is the least-populated part of the matrix.
    """
    nmax = 10
    pf = np.zeros(nmax + 1)
    pf[nmax] = 1.0  # all weight on exactly nmax photons
    p = 0.75

    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0,
                      background_ch1=0.0, background_ch2=0.0, pF=pf.tolist())
    pda.append(1.0, p)
    got = pda.s1s2

    # with no background the anti-diagonal is exactly the binomial
    expected = np.array([comb(nmax, r) * p ** (nmax - r) * (1 - p) ** r
                         for r in range(nmax + 1)])
    diag = np.array([got[nmax - r, r] for r in range(nmax + 1)])
    np.testing.assert_allclose(diag, expected, rtol=0, atol=1e-15)
    assert diag[nmax] > 0.0        # the all-red corner used to be dropped
    np.testing.assert_allclose(got.sum(), 1.0, rtol=0, atol=1e-14)


def test_s1s2_row_is_channel_one():
    """Row index is channel 1. A transpose here mirrors every fitted E."""
    nmax = 40
    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=5,
                      pF=poisson_pf(15.0, nmax).tolist())
    pda.append(1.0, 0.9)           # 90 % of photons land in channel 1
    row, col = np.unravel_index(pda.s1s2.argmax(), pda.s1s2.shape)
    assert row > col, "brightest cell must sit on the channel-1 side"


def test_1dhistogram_projects_in_the_same_orientation():
    """get_1dhistogram must read the matrix the way evaluate() writes it."""
    nmax, p_ch1 = 60, 0.9
    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=10,
                      pF=poisson_pf(25.0, nmax).tolist())
    pda.append(1.0, p_ch1)
    pda.histogram_function = \
        lambda ch1, ch2: ch1 / (ch1 + ch2) if ch1 + ch2 else 0.0

    x, y = pda.get_1dhistogram(x_min=0.005, x_max=0.995, n_bins=100,
                               log_x=False)
    assert x[np.argmax(y)] == pytest.approx(p_ch1, abs=0.05)


def test_1dhistogram_matches_a_manual_projection():
    """The projection is a plain weighted sum over the visited cells."""
    nmax, n_min, n_bins = 40, 8, 50
    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=n_min,
                      background_ch1=0.7, background_ch2=0.4,
                      pF=poisson_pf(15.0, nmax).tolist())
    pda.append(0.5, 0.35)
    pda.append(0.5, 0.7)
    pda.histogram_function = \
        lambda ch1, ch2: ch2 / (ch1 + ch2) if ch1 + ch2 else 0.0

    x_min, x_max = 0.01, 0.99
    x, y = pda.get_1dhistogram(x_min=x_min, x_max=x_max, n_bins=n_bins,
                               log_x=False)

    s = pda.s1s2
    width = (x_max - x_min) / (n_bins - 1)
    # bins are CENTRED on the reported x values; mirror HistogramBinning's
    # arithmetic exactly, or a value sitting on a bin edge rounds the other way
    offset, inv_width = x_min - 0.5 * width, 1.0 / width
    manual = np.zeros(n_bins)
    for ch1 in range(1, nmax + 1):              # skip_zero_photon is on
        for ch2 in range(max(1, n_min - ch1), nmax - ch1 + 1):
            v = ch2 / (ch1 + ch2)
            b = int(np.floor((v - offset) * inv_width))
            if 0 <= b < n_bins:
                manual[b] += s[ch1, ch2]
    np.testing.assert_allclose(y, manual, rtol=0, atol=1e-15)
    np.testing.assert_allclose(x, x_min + width * np.arange(n_bins))


def test_1dhistogram_reevaluates_a_stale_model():
    """Changing a parameter must not leave get_1dhistogram on the old matrix."""
    nmax = 30
    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=5,
                      pF=poisson_pf(10.0, nmax).tolist())
    pda.append(1.0, 0.2)
    pda.histogram_function = \
        lambda ch1, ch2: ch1 / (ch1 + ch2) if ch1 + ch2 else 0.0
    _, y_low = pda.get_1dhistogram(x_min=0.005, x_max=0.995, n_bins=50,
                                   log_x=False)

    pda.probabilities_ch1 = np.array([0.8])     # invalidates the matrix
    x, y_high = pda.get_1dhistogram(x_min=0.005, x_max=0.995, n_bins=50,
                                    log_x=False)

    assert x[np.argmax(y_low)] < 0.5 < x[np.argmax(y_high)]


def test_per_species_projection_reweights_to_the_full_histogram():
    """rows[i] is species i at amplitude 1, so amplitudes @ rows == the whole
    histogram. This is what lets an amplitude-only fit skip re-evaluating."""
    nmax = 60
    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=10,
                      background_ch1=1.2, background_ch2=0.6,
                      pF=poisson_pf(20.0, nmax).tolist())
    species = [(0.3, 0.25), (0.5, 0.55), (0.2, 0.85)]
    for a, p in species:
        pda.append(a, p)
    pda.histogram_function = \
        lambda ch1, ch2: ch2 / (ch1 + ch2) if ch1 + ch2 else 0.0

    kw = dict(x_min=0.005, x_max=0.995, n_bins=81, log_x=False)
    x, y = pda.get_1dhistogram(**kw)
    x_rows, rows = pda.get_1dhistogram_per_species(**kw)

    assert rows.shape == (len(species), 81)
    np.testing.assert_allclose(x_rows, x)
    amplitudes = np.array([a for a, _ in species])
    np.testing.assert_allclose(amplitudes @ rows, y, rtol=0, atol=1e-15)

    # and it must keep holding for amplitudes the object has never seen
    for weights in ([1.0, 0.0, 0.0], [0.1, 0.2, 0.7], [0.0, 0.5, 0.5]):
        pda.species_amplitudes = np.array(weights)
        _, y_w = pda.get_1dhistogram(**kw)
        np.testing.assert_allclose(np.array(weights) @ rows, y_w,
                                   rtol=0, atol=1e-15)


def test_per_species_projection_is_empty_without_species():
    pda = tttrlib.Pda(hist2d_nmax=30, hist2d_nmin=5,
                      pF=poisson_pf(10.0, 30).tolist())
    x, rows = pda.get_1dhistogram_per_species(x_min=0.005, x_max=0.995,
                                              n_bins=20, log_x=False)
    assert rows.shape == (0, 20)
    assert len(x) == 20


def test_amplitude_zero_species_contributes_nothing():
    nmax = 30
    pf = poisson_pf(10.0, nmax).tolist()
    one = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=5, pF=pf)
    one.append(1.0, 0.4)
    two = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=5, pF=pf)
    two.append(1.0, 0.4)
    two.append(0.0, 0.9)
    np.testing.assert_allclose(two.s1s2, one.s1s2, rtol=0, atol=1e-16)


def test_model_is_a_normalized_distribution():
    """Total probability is conserved through the background convolution."""
    nmax = 80
    pda = tttrlib.Pda(hist2d_nmax=nmax, hist2d_nmin=0,
                      background_ch1=2.0, background_ch2=1.0,
                      pF=poisson_pf(20.0, nmax).tolist())
    pda.append(0.3, 0.25)
    pda.append(0.7, 0.65)
    # mass can only leave through ch1+ch2 > nmax, which a small lambda avoids
    assert pda.s1s2.sum() == pytest.approx(1.0, abs=1e-6)
    assert (pda.s1s2 >= 0.0).all()
