"""PDA_DEFAULT vs PDA_OPTIMIZED, and the parameter surface of Pda.

Replaces three files (test_pda_basic, test_pda_optimized, test_pda_impl_simple)
that walked these same scenarios and then asserted `True`. The scenarios are
kept; the assertions are real.
"""
from __future__ import division

import numpy as np
import pytest

import tttrlib

from test_pda_reference import poisson_pf

DEFAULT, OPTIMIZED = 0, 1


#: lam is kept well below 34 so that pF[0] = exp(-lam) stays above 1e-15. Past
#: that threshold PDA_OPTIMIZED switches on its multi-molecule correction and
#: legitimately stops matching PDA_DEFAULT -- pinned separately below.
DEFAULT_LAMBDA = 10.0


def build(nmax, bg1, bg2, species, implementation, lam=DEFAULT_LAMBDA):
    pf = poisson_pf(lam, nmax)
    pda = tttrlib.Pda(nmax, max(1, nmax // 10), bg1, bg2, pf.tolist(),
                      implementation)
    for a, p in species:
        pda.append(a, p)
    return pda


TWO_SPECIES = [(0.5, 0.3), (0.5, 0.7)]


@pytest.mark.parametrize("bg1, bg2", [(0.0, 0.0), (0.5, 0.5), (2.0, 2.0),
                                      (5.0, 5.0), (20.0, 4.0)])
def test_implementations_agree_across_backgrounds(bg1, bg2):
    a = build(60, bg1, bg2, TWO_SPECIES, DEFAULT).s1s2
    b = build(60, bg1, bg2, TWO_SPECIES, OPTIMIZED).s1s2
    np.testing.assert_allclose(b, a, rtol=0, atol=1e-15)


@pytest.mark.parametrize("n_species", [1, 2, 3, 5, 10])
def test_implementations_agree_across_species_counts(n_species):
    species = [(1.0 / n_species, 0.2 + 0.6 * i / max(1, n_species - 1))
               for i in range(n_species)]
    a = build(50, 0.5, 0.5, species, DEFAULT).s1s2
    b = build(50, 0.5, 0.5, species, OPTIMIZED).s1s2
    np.testing.assert_allclose(b, a, rtol=0, atol=1e-15)


@pytest.mark.parametrize("nmax", [20, 50, 100, 200])
def test_implementations_agree_across_nmax(nmax):
    a = build(nmax, 0.5, 0.5, TWO_SPECIES, DEFAULT).s1s2
    b = build(nmax, 0.5, 0.5, TWO_SPECIES, OPTIMIZED).s1s2
    assert a.shape == (nmax + 1, nmax + 1)
    np.testing.assert_allclose(b, a, rtol=0, atol=1e-15)


@pytest.mark.parametrize("p_ch1", [0.01, 0.1, 0.5, 0.9, 0.99])
def test_extreme_channel_probabilities(p_ch1):
    """Near-degenerate splits stay finite, non-negative and normalized."""
    pda = build(50, 0.5, 0.5, [(1.0, p_ch1)], OPTIMIZED)
    s = pda.s1s2
    assert np.isfinite(s).all()
    assert (s >= 0.0).all()
    assert s.sum() == pytest.approx(1.0, abs=1e-6)
    # the bright channel is still the one that gets the photons
    row, col = np.unravel_index(s.argmax(), s.shape)
    assert (row > col) == (p_ch1 > 0.5) or row == col


def test_switching_implementation_reevaluates_to_the_same_matrix():
    pda = build(50, 0.5, 0.5, TWO_SPECIES, DEFAULT)
    first = pda.s1s2.copy()
    assert pda.get_implementation() == DEFAULT

    pda.set_implementation(OPTIMIZED)
    assert pda.get_implementation() == OPTIMIZED
    assert not pda.hist2d_valid, "changing the implementation must invalidate"
    np.testing.assert_allclose(pda.s1s2, first, rtol=0, atol=1e-15)


def test_zero_photon_pf_does_not_trip_the_multimolecule_branch():
    """pF[0] == 0 gives log(0) = -inf; the (int) cast of that is undefined.

    The optimized path must stay finite and equal to the default path.
    """
    nmax = 40
    pf = poisson_pf(10.0, nmax)
    pf[0] = 0.0
    pf /= pf.sum()

    out = []
    for impl in (DEFAULT, OPTIMIZED):
        pda = tttrlib.Pda(nmax, 5, 0.5, 0.5, pf.tolist(), impl)
        for a, p in TWO_SPECIES:
            pda.append(a, p)
        s = pda.s1s2
        assert np.isfinite(s).all()
        out.append(s)
    np.testing.assert_allclose(out[1], out[0], rtol=0, atol=1e-15)


def test_parameter_setters_invalidate_the_matrix():
    pda = build(50, 0.5, 0.5, TWO_SPECIES, DEFAULT)
    for attr, value in [("background_ch1", 1.0), ("background_ch2", 1.0),
                        ("hist2d_nmin", 10), ("hist2d_nmax", 60)]:
        pda.evaluate()
        assert pda.hist2d_valid
        setattr(pda, attr, value)
        assert getattr(pda, attr) == value
        assert not pda.hist2d_valid, "%s must invalidate the S1S2 matrix" % attr


def test_setting_max_number_of_photons_resizes_the_matrix():
    pda = build(50, 0.0, 0.0, TWO_SPECIES, DEFAULT)
    assert pda.s1s2.shape == (51, 51)
    pda.hist2d_nmax = 70
    assert pda.s1s2.shape == (71, 71)


def test_short_pf_is_zero_padded_not_crashed(capfd):
    """pF shorter than nmax+1 is padded, and the warning stays off stdout."""
    pda = tttrlib.Pda(30, 5, 0.0, 0.0, poisson_pf(5.0, 10).tolist(), DEFAULT)
    pda.append(1.0, 0.5)
    s = pda.s1s2
    assert np.isfinite(s).all()
    assert capfd.readouterr().out == ""


def test_species_accessors_round_trip():
    pda = tttrlib.Pda(30, 5, 0.0, 0.0, poisson_pf(10.0, 30).tolist(), DEFAULT)
    pda.append(0.4, 0.25)
    pda.append(0.6, 0.75)
    np.testing.assert_allclose(pda.species_amplitudes, [0.4, 0.6])
    np.testing.assert_allclose(pda.probabilities_ch1, [0.25, 0.75])
    np.testing.assert_allclose(pda.spectrum_ch1, [0.4, 0.25, 0.6, 0.75])

    pda.clear_probability_ch1()
    assert len(pda.species_amplitudes) == 0

    pda.spectrum_ch1 = np.array([0.3, 0.1, 0.7, 0.9])
    np.testing.assert_allclose(pda.species_amplitudes, [0.3, 0.7])
    np.testing.assert_allclose(pda.probabilities_ch1, [0.1, 0.9])


def test_repr_and_str_do_not_raise():
    """str(pda) raised TypeError on every line it built."""
    pda = build(30, 0.5, 0.5, TWO_SPECIES, DEFAULT)
    assert "n_species=2" in repr(pda)
    text = str(pda)
    assert "Number of species: 2" in text
    assert "Background ch1" in text


def test_multimolecule_correction_makes_the_paths_diverge():
    """Pins the one regime where PDA_OPTIMIZED is not just a faster path.

    pF[0] < 1e-15 turns on the FFT multi-molecule correction, which changes the
    model. Documented in doc/pda-guide.rst; asserted here so it cannot start or
    stop happening unnoticed.
    """
    nmax, lam = 60, 40.0                      # pF[0] = exp(-40) = 4e-18
    pf = poisson_pf(lam, nmax)
    assert pf[0] < 1e-15

    a = build(nmax, 0.5, 0.5, TWO_SPECIES, DEFAULT, lam=lam).s1s2
    b = build(nmax, 0.5, 0.5, TWO_SPECIES, OPTIMIZED, lam=lam).s1s2
    assert np.isfinite(b).all()
    assert np.abs(b - a).max() > 1e-6, "correction is expected to fire here"


def test_callback_can_be_replaced_repeatedly():
    """Each set_callback used to leak the callback it replaced."""
    nmax = 120
    pda = build(nmax, 0.0, 0.0, [(1.0, 0.6)], DEFAULT, lam=30.0)
    for divisor in (1.0, 2.0):
        pda.histogram_function = \
            lambda ch1, ch2, d=divisor: (ch1 / d) / (ch1 + ch2) if ch1 + ch2 else 0.0
        x, y = pda.get_1dhistogram(x_min=0.005, x_max=0.995, n_bins=200,
                                   log_x=False)
        assert y.sum() > 0
        # weighted mean is stable where a coarse argmax is not
        assert np.average(x, weights=y) == pytest.approx(0.6 / divisor, abs=0.05)
