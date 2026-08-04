"""Continuous-time Markov kinetics on its own, without a photon simulation."""
import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")

if not hasattr(tttrlib, "sim_occupation_fractions"):
    pytest.skip("tttrlib built without the kinetics sampler", allow_module_level=True)


def _two_state(k_total, window, n_samples, seed=7):
    """Return occupancies of state 0 for a symmetric two-state chain."""
    k = k_total / 2.0
    flat = tttrlib.sim_occupation_fractions(
        [0.0, k, k, 0.0], 2, window, n_samples, [0.5, 0.5], seed
    )
    return np.asarray(flat).reshape(-1, 2)


@pytest.mark.parametrize("k_total", [50.0, 200.0, 1000.0, 5000.0])
def test_occupancy_matches_the_telegraph_closed_form(k_total):
    """The exact mean and variance of a two-state occupancy, over four decades of k*T.

    The indicator of state 0 is a telegraph process with covariance
    ``pi0 pi1 exp(-k|dt|)``; integrating it twice over the window gives

        Var(f) = 2 pi0 pi1 [ 1/(kT) - (1 - e^-kT)/(kT)^2 ]

    with the static mixture variance as kT -> 0 and zero as kT -> infinity. Both
    limits are in the range covered here, which is the point: a sampler that only
    works in the middle is the one that quietly fails at the ends.
    """
    window = 2.0e-3
    fractions = _two_state(k_total, window, 40_000)
    x = k_total * window
    expected_var = 2.0 * 0.25 * (1.0 / x - (1.0 - np.exp(-x)) / (x * x))

    assert fractions[:, 0].mean() == pytest.approx(0.5, abs=0.01)
    assert fractions[:, 0].var() == pytest.approx(expected_var, rel=0.05)
    assert np.allclose(fractions.sum(axis=1), 1.0)


def test_a_scheme_with_no_exchange_never_leaves_its_state():
    """Zero rates are static, not an error and not a slow drift."""
    flat = tttrlib.sim_occupation_fractions([0.0] * 9, 3, 1.0, 500, [0.2, 0.3, 0.5], 1)
    fractions = np.asarray(flat).reshape(-1, 3)
    # Every window is entirely in one state, and the states appear in proportion.
    assert set(np.unique(fractions)) == {0.0, 1.0}
    assert fractions[:, 2].mean() == pytest.approx(0.5, abs=0.06)


def test_the_initial_distribution_is_honoured():
    """A chain started off-equilibrium and given no time stays where it was put."""
    flat = tttrlib.sim_occupation_fractions(
        [0.0, 1e-9, 1e-9, 0.0], 2, 1.0, 400, [1.0, 0.0], 3
    )
    assert np.asarray(flat).reshape(-1, 2)[:, 0].mean() == pytest.approx(1.0, abs=1e-6)


def test_the_same_seed_gives_the_same_windows():
    """A fit that moves a rate must not also be moving the sampling noise."""
    a = _two_state(1000.0, 2.0e-3, 500, seed=11)
    b = _two_state(1000.0, 2.0e-3, 500, seed=11)
    c = _two_state(1000.0, 2.0e-3, 500, seed=12)
    assert np.array_equal(a, b)
    assert not np.array_equal(a, c)


@pytest.mark.parametrize("bad,message", [
    (dict(k=[0.0, 1.0], n=2), "n_states"),
    (dict(k=[0.0] * 4, n=2, window=0.0), "window"),
    (dict(k=[0.0] * 4, n=2, p0=[1.0]), "p0"),
])
def test_a_malformed_request_is_refused(bad, message):
    """Shape mistakes raise rather than producing a plausible array."""
    with pytest.raises(ValueError, match=message):
        tttrlib.sim_occupation_fractions(
            bad["k"], bad["n"], bad.get("window", 1.0), 10,
            bad.get("p0", []), 1,
        )
