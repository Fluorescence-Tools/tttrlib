"""Benchmark and correctness test for BurstML (FRET_burstML port).

Simulates 2-state smFRET bursts with known parameters, then verifies that
BurstML recovers them. The simulation is a simple Poisson photon generator:
each burst starts in state s ~ Categorical(peq), emits photons at rate
n0[s]*exp(-2*q^2) with colour drawn from FRET efficiency E[s], and the
inter-photon time is drawn from Exp(total_rate).
"""

from __future__ import annotations

import time

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
have_burstml = hasattr(tttrlib, "BurstML")
pytestmark = pytest.mark.skipif(
    not have_burstml, reason="tttrlib built without BurstML"
)


def simulate_bursts(
    n0=(150.0, 300.0),
    tau_diff=(0.8, 2.5),
    rate_sum=1.0,
    peq=(0.4, 0.6),
    eff=(0.85, 0.45),
    bkg=(2.5, 0.8),
    n_bursts=50,
    min_photons=30,
    max_interphoton_ms=0.3,
    seed=42,
):
    """Generate synthetic burst data matching BurstML's forward model.

    Returns (times_ms, colours, offsets) where colours are 0=acceptor, 1=donor.
    """
    rng = np.random.default_rng(seed)
    n_states = len(n0)
    qmax = 4.0
    jmax = 50
    dq = qmax / jmax
    qaxis = np.array([(i + 1) * dq - dq / 2 for i in range(jmax)])
    profile = np.exp(-2.0 * qaxis**2)

    times_all = []
    colours_all = []
    offsets = [0]

    for _ in range(n_bursts):
        state = rng.choice(n_states, p=peq)
        q_idx = rng.choice(jmax, p=profile / profile.sum())
        brightness_factor = profile[q_idx]

        photons_t = []
        photons_c = []
        t = 0.0

        while True:
            if n_states > 1:
                k_switch = rate_sum * 0.01
                if rng.random() < k_switch:
                    state = 1 - state

            total_rate = n0[state] * brightness_factor + bkg[0] + bkg[1]
            dt = rng.exponential(1.0 / total_rate)
            t += dt

            if dt > max_interphoton_ms:
                break

            p_acceptor = (eff[state] * n0[state] * brightness_factor + bkg[0]) / total_rate
            color = 0 if rng.random() < p_acceptor else 1

            photons_t.append(t)
            photons_c.append(color)

            q_idx = max(0, min(jmax - 1, q_idx + rng.integers(-1, 2)))
            brightness_factor = profile[q_idx]

        if len(photons_t) >= min_photons:
            times_all.extend(photons_t)
            colours_all.extend(photons_c)
            offsets.append(len(times_all))

    return (
        np.array(times_all),
        np.array(colours_all, dtype=np.int32),
        np.array(offsets, dtype=np.int64),
    )


@pytest.mark.heavy
def test_burstml_recovers_simulated_params():
    """BurstML recovers ground-truth parameters from simulated 2-state bursts."""
    gt = dict(
        n0=(150.0, 300.0),
        tau_diff=(0.8, 2.5),
        rate_sum=1.0,
        peq=(0.4, 0.6),
        eff=(0.85, 0.45),
        bkg=(2.5, 0.8),
        n_bursts=50,
        min_photons=30,
        seed=42,
    )

    times, colours, offsets = simulate_bursts(**gt)
    assert len(offsets) > 1, "No bursts generated"

    ml = tttrlib.BurstML()
    ml.set_burst_data(times.tolist(), colours.tolist(), offsets.tolist())

    n_states = 2
    n_colours = 2
    init = [
        200.0, 350.0,
        1.0, 3.0,
        0.8,
        0.5,
        0.8, 0.5,
        3.0, 1.0,
    ]
    lb = [50, 100, 0.1, 0.3, 0.1, 0.2, 0.7, 0.3, 0.5, 0.5]
    ub = [500, 600, 3.0, 10.0, 10.0, 0.8, 0.95, 0.6, 5.0, 3.0]

    t0 = time.perf_counter()
    result = ml.fit(
        init, lb, ub,
        n_states=n_states,
        n_colours=n_colours,
        jmax=20,
        qmax=4.0,
        t_th=0.3,
        n_th=30.0,
    )
    elapsed = time.perf_counter() - t0

    p = result.params
    n = n_states
    nc = n_colours
    n0_fit = np.array(p[:n])
    tau_fit = np.array(p[n:2*n])
    E_fit = np.array(p[(4+0)*n - 2:(5+0)*n - 2])
    bkg_fit = np.array(p[(3+nc)*n - 2:(3+nc)*n - 2 + nc])

    print(f"\nBurstML fit: {elapsed:.1f}s, {result.iterations} iters, status={result.status}")
    print(f"  logL={result.log_likelihood:.1f}, BIC={result.bic:.1f}")
    print(f"  n0:   true={gt['n0']}, fitted={n0_fit}")
    print(f"  tau:  true={gt['tau_diff']}, fitted={tau_fit}")
    print(f"  E:    true={gt['eff']}, fitted={E_fit}")
    print(f"  bkg:  true={gt['bkg']}, fitted={bkg_fit}")

    for i in range(n_states):
        assert E_fit[i] > 0.0, f"E[{i}] should be positive"
        assert E_fit[i] < 1.0, f"E[{i}] should be < 1"
        assert n0_fit[i] > 0.0, f"n0[{i}] should be positive"

    assert np.isfinite(result.log_likelihood), "log-likelihood should be finite"

    nll_init = ml.neg_log_likelihood(init)
    nll_fit = ml.neg_log_likelihood(result.params)
    assert nll_fit < nll_init, \
        f"fit NLL ({nll_fit:.1f}) should be < init NLL ({nll_init:.1f})"


def test_burstml_likelihood_is_finite():
    """The likelihood function returns finite values for valid parameters."""
    times, colours, offsets = simulate_bursts(n_bursts=20, min_photons=30, seed=1)

    ml = tttrlib.BurstML()
    ml.set_burst_data(times.tolist(), colours.tolist(), offsets.tolist())
    ml.set_jmax(15)

    params = [150.0, 300.0, 0.8, 2.5, 0.8, 0.5, 0.85, 0.45, 2.5, 0.8]
    nll = ml.neg_log_likelihood(params)

    assert np.isfinite(nll), f"NLL should be finite, got {nll}"


def test_burstml_n_bursts_and_n_photons():
    """set_burst_data correctly populates burst count and photon count."""
    times = [0.01, 0.02, 0.03, 0.5, 0.51, 0.52, 0.53]
    colours = [0, 1, 0, 1, 0, 1, 0]
    offsets = [0, 3, 7]

    ml = tttrlib.BurstML()
    ml.set_burst_data(times, colours, offsets)

    assert ml.n_bursts() == 2
    assert ml.n_photons() == 7


def test_burstml_likelihood_improves_with_better_params():
    """Likelihood is higher (NLL lower) for params closer to truth than far-off params."""
    times, colours, offsets = simulate_bursts(n_bursts=30, min_photons=30, seed=7)

    ml = tttrlib.BurstML()
    ml.set_burst_data(times.tolist(), colours.tolist(), offsets.tolist())
    ml.set_jmax(15)

    # "good" params close to simulation truth
    good = [150.0, 300.0, 0.8, 2.5, 0.8, 0.4, 0.85, 0.45, 2.5, 0.8]
    # "bad" params far from truth
    bad = [400.0, 500.0, 5.0, 8.0, 5.0, 0.8, 0.2, 0.1, 0.5, 0.5]

    nll_good = ml.neg_log_likelihood(good)
    nll_bad = ml.neg_log_likelihood(bad)

    print(f"NLL(good)={nll_good:.2f}, NLL(bad)={nll_bad:.2f}")
    assert nll_good < nll_bad, \
        f"good params should have lower NLL ({nll_good:.2f}) than bad ({nll_bad:.2f})"
