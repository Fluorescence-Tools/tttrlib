import os

import numpy as np
import pytest

import tttrlib


def _component_probability(tau, irf, dt, period):
    n_bins = irf.size
    jordi_irf = np.concatenate((irf, irf))
    model = np.zeros(2 * n_bins, dtype=np.float64)
    tttrlib.DecayFit23.modelf(
        np.array([tau, 0.0, 0.0, 1.0]),
        jordi_irf,
        np.zeros_like(jordi_irf),
        dt,
        np.array([period, 1.0, 0.0, 0.0, n_bins - 1]),
        model,
    )
    pooled = model[:n_bins] + model[n_bins:]
    return pooled / pooled.sum()


def test_monoexponential_poisson_recovery():
    n_bins = 128
    dt = 0.1
    true_tau = 2.5
    irf = np.zeros(n_bins, dtype=np.float64)
    irf[7] = 1.0
    probability = _component_probability(true_tau, irf, dt, n_bins * dt)
    data = np.random.default_rng(12).multinomial(100_000, probability)

    fitter = tttrlib.FitNExp(
        dt=dt,
        irf=irf,
        period=n_bins * dt,
        convolution_stop=n_bins - 1,
        tau_min=0.2,
        tau_max=8.0,
    )
    result = fitter(data, initial_lifetimes=[1.5], include_model=True)

    assert result["converged"]
    np.testing.assert_allclose(result["lifetimes"], [true_tau], atol=0.05)
    np.testing.assert_allclose(result["amplitudes"], [1.0], atol=1e-9)
    np.testing.assert_allclose(result["model"].sum(), data.sum(), rtol=1e-12)
    np.testing.assert_array_equal(fitter.data, data)
    np.testing.assert_array_equal(fitter.irf, irf)
    np.testing.assert_array_equal(fitter.model, result["model"])


def test_fixed_multiexponential_amplitudes():
    n_bins = 128
    dt = 0.1
    lifetimes = np.array([0.8, 3.4])
    amplitudes = np.array([0.3, 0.7])
    irf = np.zeros(n_bins, dtype=np.float64)
    irf[5] = 1.0
    components = np.stack([
        _component_probability(tau, irf, dt, n_bins * dt)
        for tau in lifetimes
    ])
    probability = amplitudes @ components
    data = np.random.default_rng(21).multinomial(200_000, probability)

    fitter = tttrlib.FitNExp(
        dt=dt,
        irf=irf,
        period=n_bins * dt,
        convolution_stop=n_bins - 1,
    )
    result = fitter.fit_fixed_lifetimes(
        data,
        lifetimes=lifetimes,
        initial_amplitudes=[0.5, 0.5],
    )

    assert result["converged"]
    np.testing.assert_allclose(result["lifetimes"], lifetimes, atol=0.0)
    np.testing.assert_allclose(result["amplitudes"], amplitudes, atol=0.015)
    np.testing.assert_allclose(result["amplitudes"].sum(), 1.0, atol=1e-12)
    assert "model" not in result
    assert fitter.model.size == 0


def test_free_biexponential_with_background():
    n_bins = 128
    dt = 0.1
    true_lifetimes = np.array([0.8, 3.4])
    true_weights = np.array([0.30, 0.65, 0.05])
    irf = np.zeros(n_bins, dtype=np.float64)
    irf[5] = 1.0
    background = np.ones(n_bins, dtype=np.float64)
    background /= background.sum()
    components = np.stack([
        _component_probability(tau, irf, dt, n_bins * dt)
        for tau in true_lifetimes
    ])
    probability = (
        true_weights[0] * components[0]
        + true_weights[1] * components[1]
        + true_weights[2] * background
    )
    data = np.random.default_rng(42).multinomial(300_000, probability)

    fitter = tttrlib.FitNExp(
        dt=dt,
        irf=irf,
        background=background,
        period=n_bins * dt,
        convolution_stop=n_bins - 1,
        tau_min=0.2,
        tau_max=8.0,
        initial_background_fraction=0.05,
    )
    result = fitter(
        data,
        initial_lifetimes=[1.0, 3.0],
        initial_amplitudes=[0.4, 0.6],
    )

    assert result["converged"]
    order = np.argsort(result["lifetimes"])
    np.testing.assert_allclose(
        result["lifetimes"][order], true_lifetimes, atol=0.12
    )
    np.testing.assert_allclose(
        result["amplitudes"][order], true_weights[:2], atol=0.025
    )
    np.testing.assert_allclose(
        result["background_amplitude"], true_weights[2], atol=0.015
    )


def test_multiexponential_search_does_not_false_converge():
    n_bins = 64
    dt = 0.1
    irf = np.zeros(n_bins, dtype=np.float64)
    irf[4] = 1.0
    period = n_bins * dt
    probability = (
        0.3 * _component_probability(1.0, irf, dt, period)
        + 0.7 * _component_probability(1.5, irf, dt, period)
    )
    data = np.rint(probability * 1_000_000).astype(np.float64)
    fitter = tttrlib.FitNExp(
        dt=dt,
        irf=irf,
        period=period,
        convolution_stop=n_bins - 1,
        tau_min=0.1,
        tau_max=8.0,
        max_outer_iterations=1,
    )

    result = fitter(
        data,
        initial_lifetimes=[2.0, 3.0],
        initial_amplitudes=[0.5, 0.5],
    )
    collapsed = fitter.fit_fixed_lifetimes(
        data,
        lifetimes=[2.0, 7.2020577],
        initial_amplitudes=[0.5, 0.5],
    )

    assert not result["converged"]
    assert (
        collapsed["negative_log_likelihood"]
        - result["negative_log_likelihood"]
        > 10_000
    )


def test_fixed_lifetime_outside_bounds_is_rejected():
    fitter = tttrlib.FitNExp(
        dt=0.1,
        irf=np.ones(16),
        tau_min=0.2,
        tau_max=8.0,
    )

    with pytest.raises(ValueError, match="fixed lifetimes.*tau bounds"):
        fitter.fit_fixed_lifetimes(
            np.ones(16),
            lifetimes=[20.0],
        )


def test_zero_photon_data_is_rejected():
    fitter = tttrlib.FitNExp(dt=0.1, irf=np.ones(16))

    with pytest.raises(ValueError, match="positive finite photon count"):
        fitter(np.zeros(16), initial_lifetimes=[2.0])


def test_native_batch_is_thread_invariant():
    n_bins = 64
    dt = 0.2
    irf = np.zeros(n_bins, dtype=np.float64)
    irf[4] = 1.0
    probability = _component_probability(2.0, irf, dt, n_bins * dt)
    rng = np.random.default_rng(7)
    matrix = rng.multinomial(2_000, probability, size=160)
    fitter = tttrlib.FitNExp(
        dt=dt,
        irf=irf,
        period=n_bins * dt,
        convolution_stop=n_bins - 1,
    )

    previous = os.environ.get("TTTRLIB_NUM_THREADS")
    try:
        os.environ["TTTRLIB_NUM_THREADS"] = "1"
        serial = fitter.fit_many(
            matrix,
            initial_lifetimes=[2.0],
            fixed=[1],
        )
        os.environ["TTTRLIB_NUM_THREADS"] = "4"
        parallel = fitter.fit_many(
            matrix,
            initial_lifetimes=[2.0],
            fixed=[1],
        )
    finally:
        if previous is None:
            os.environ.pop("TTTRLIB_NUM_THREADS", None)
        else:
            os.environ["TTTRLIB_NUM_THREADS"] = previous

    np.testing.assert_array_equal(parallel, serial)

    maps = fitter.fit_map(
        matrix[:6].reshape(2, 3, n_bins),
        initial_lifetimes=[2.0],
        fixed=[1],
        minimum_photons=1,
    )
    assert maps["lifetimes"].shape == (2, 3, 1)
    assert maps["amplitudes"].shape == (2, 3, 1)
    assert np.all(maps["valid"])
    np.testing.assert_allclose(maps["lifetimes"], 2.0)
