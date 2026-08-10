"""StreamingCorrelator against the batch Wahl correlator.

The streaming correlator has one job: given the same photons, produce what
`Correlator` produces. It bins to the same absolute grid and publishes the same
multi-tau axis, so "close enough" is not the bar — the two must agree lag for
lag, and the test asserts that per cascade rather than in aggregate, because
the defect this replaced was confined to cascade 2 and up and an aggregate
tolerance hid it.
"""
import numpy as np
import pytest

import tttrlib


N_BINS = 16
N_CASC = 12


def poisson_stream(rate, duration, seed):
    """Uncorrelated photons: G(tau) must be flat at 1."""
    rng = np.random.default_rng(seed)
    n = rng.poisson(rate * duration)
    t = np.sort(rng.integers(0, duration, size=n))
    return t.astype(np.uint64)


def diffusion_stream(duration, tau_d, seed, mean_rate=0.02, contrast=3.0):
    """Photons whose rate follows a correlated (Ornstein-Uhlenbeck) intensity,
    so G(tau) actually decays and a lag misassignment shows up as an error."""
    rng = np.random.default_rng(seed)
    n_steps = int(duration)
    # OU process with correlation time tau_d, exponentiated to keep it positive.
    theta = 1.0 / tau_d
    x = np.zeros(n_steps)
    noise = rng.normal(size=n_steps)
    a = np.exp(-theta)
    s = np.sqrt(1.0 - a * a)
    for i in range(1, n_steps):
        x[i] = a * x[i - 1] + s * noise[i]
    rate = mean_rate * np.exp(contrast * x - 0.5 * contrast ** 2)
    counts = rng.poisson(rate)
    times = np.repeat(np.arange(n_steps, dtype=np.uint64), counts)
    return times


def batch_correlation(t1, t2):
    c = tttrlib.Correlator()
    c.method = "wahl"
    c.n_bins = N_BINS
    c.n_casc = N_CASC
    c.set_macrotimes(t1, t2)
    c.set_weights(np.ones(len(t1)), np.ones(len(t2)))
    return np.asarray(c.x_axis, dtype=float), np.asarray(c.get_corr_normalized(), dtype=float)


def streaming_correlation(t1, t2=None):
    sc = tttrlib.StreamingCorrelator(N_BINS, N_CASC, 1.0)
    if t2 is None:
        for t in t1:
            sc.push_photon(int(t))
    else:
        # One merged, time-ordered stream, as a live acquisition would deliver.
        merged = np.concatenate([
            np.stack([t1, np.zeros(len(t1), dtype=np.uint64)]),
            np.stack([t2, np.ones(len(t2), dtype=np.uint64)]),
        ], axis=1)
        merged = merged[:, np.argsort(merged[0], kind="stable")]
        for t, ch in zip(merged[0], merged[1]):
            sc.push_photon(int(t), 1.0, int(ch))
    sc.flush()
    return np.asarray(sc.get_x_axis(), dtype=float), \
        np.asarray(sc.get_correlation_normalized(), dtype=float)


def test_axis_matches_batch():
    """The two must publish the same lags before their values can be compared."""
    t = poisson_stream(0.01, 200_000, seed=1)
    x_batch, _ = batch_correlation(t, t)
    x_stream, _ = streaming_correlation(t)
    assert len(x_stream) == len(x_batch)
    np.testing.assert_array_equal(x_stream, x_batch)


def test_poisson_is_flat_at_one():
    """An uncorrelated stream has no structure: if this is not flat, the
    correlation kernel is wrong and no normalisation will rescue it."""
    t = poisson_stream(0.05, 2_000_000, seed=2)
    x, g = streaming_correlation(t)
    # Skip lag 0 (never filled) and the last cascade, where the overlap T-tau
    # leaves too few pairs for the mean to have settled.
    lo, hi = 1, N_BINS * (N_CASC - 3)
    body = g[lo:hi]
    assert np.all(np.isfinite(body))
    assert abs(body.mean() - 1.0) < 0.02, f"mean G = {body.mean():.4f}"
    assert body.std() < 0.05, f"std G = {body.std():.4f}"


@pytest.mark.parametrize("seed,tau_d", [(3, 200), (4, 1500)])
def test_matches_batch_on_correlated_stream(seed, tau_d):
    t = diffusion_stream(600_000, tau_d, seed=seed)
    assert len(t) > 5000
    x_batch, g_batch = batch_correlation(t, t)
    _, g_stream = streaming_correlation(t)

    # Per cascade, so a defect confined to the coarse levels cannot average out.
    for casc in range(N_CASC - 3):
        lo = casc * N_BINS + 1
        hi = lo + N_BINS
        b, s = g_batch[lo:hi], g_stream[lo:hi]
        keep = np.isfinite(b) & np.isfinite(s) & (b > 1e-6)
        assert keep.sum() > N_BINS // 2, f"cascade {casc}: too few usable lags"
        ratio = s[keep] / b[keep]
        assert np.allclose(ratio, 1.0, atol=0.05), (
            f"cascade {casc} (lags {x_batch[lo]:.0f}-{x_batch[hi - 1]:.0f}): "
            f"stream/batch = {np.round(ratio, 3)}"
        )


def test_cross_correlation_matches_batch():
    """Two channels, and the lag runs channel 0 -> channel 1, as in the batch."""
    base = diffusion_stream(400_000, 400, seed=5)
    rng = np.random.default_rng(6)
    # Split the same intensity fluctuation over two detectors: the cross
    # correlation keeps the fluctuation and drops the shot-noise self term.
    pick = rng.random(len(base)) < 0.5
    t1 = base[pick]
    t2 = base[~pick]
    assert len(t1) > 2000 and len(t2) > 2000

    _, g_batch = batch_correlation(t1, t2)
    _, g_stream = streaming_correlation(t1, t2)

    lo, hi = 1, N_BINS * (N_CASC - 3)
    b, s = g_batch[lo:hi], g_stream[lo:hi]
    keep = np.isfinite(b) & np.isfinite(s) & (b > 1e-6)
    assert keep.sum() > 0.5 * (hi - lo)
    np.testing.assert_allclose(s[keep], b[keep], rtol=0.05)


def test_flush_is_idempotent():
    t = poisson_stream(0.05, 100_000, seed=7)
    sc = tttrlib.StreamingCorrelator(N_BINS, N_CASC, 1.0)
    for v in t:
        sc.push_photon(int(v))
    sc.flush()
    first = np.asarray(sc.get_correlation())
    sc.flush()
    np.testing.assert_array_equal(np.asarray(sc.get_correlation()), first)


def test_push_after_flush_raises():
    sc = tttrlib.StreamingCorrelator(N_BINS, N_CASC, 1.0)
    for v in (0, 5, 9, 40):
        sc.push_photon(v)
    sc.flush()
    with pytest.raises(Exception):
        sc.push_photon(100)


def test_incremental_matches_all_at_once():
    """Chunked delivery is the whole point of a streaming consumer."""
    t = diffusion_stream(200_000, 300, seed=8)
    _, g_all = streaming_correlation(t)

    sc = tttrlib.StreamingCorrelator(N_BINS, N_CASC, 1.0)
    for chunk in np.array_split(t, 37):
        for v in chunk:
            sc.push_photon(int(v))
    sc.flush()
    g_chunked = np.asarray(sc.get_correlation_normalized())
    np.testing.assert_array_equal(g_chunked, g_all)
