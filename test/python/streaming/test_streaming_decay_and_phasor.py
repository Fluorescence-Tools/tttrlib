"""StreamingDecayHistogram and StreamingPhasor against their batch equivalents.

These two were recorded as "correct and usable" while the correlator beside
them was not. That was an untested claim, and the correlator's cascades 0 and 1
were "correct" in the same sense — so it is checked here rather than asserted:
the histogram against `TTTR.get_microtime_histogram`, the phasor against
`DecayPhasor.compute_phasor`, both to machine precision on the same photons.
"""
import numpy as np
import pytest

import tttrlib

N_BINS = 4096


def exp_microtimes(tau_bins, n, seed, n_bins=N_BINS):
    """Microtimes from a single-exponential decay, truncated to the TCSPC window."""
    rng = np.random.default_rng(seed)
    mt = rng.exponential(tau_bins, size=int(n * 1.3))
    mt = mt[mt < n_bins][:n]
    return mt.astype(np.uint16)


def as_tttr(micro_times, channels=None):
    n = len(micro_times)
    tt = tttrlib.TTTR()
    tt.append_events(
        macro_times=np.arange(n, dtype=np.uint64),
        micro_times=np.asarray(micro_times, dtype=np.uint16),
        routing_channels=(np.zeros(n, dtype=np.int8) if channels is None
                          else np.asarray(channels, dtype=np.int8)),
        event_types=np.zeros(n, dtype=np.int8),
    )
    # A hand-built TTTR has no micro-time channel count, and the batch
    # histogram silently comes back length 1 without it.
    tt.header.set_number_of_micro_time_channels(N_BINS)
    return tt


# --------------------------------------------------------------- histogram --

def test_histogram_matches_numpy_bincount():
    mt = exp_microtimes(220.0, 60_000, seed=1)
    hist = tttrlib.StreamingDecayHistogram(N_BINS, 1)
    for v in mt:
        hist.push_photon(int(v))
    got = np.asarray(hist.get_histogram(0))
    expected = np.bincount(mt.astype(np.int64), minlength=N_BINS).astype(float)
    np.testing.assert_array_equal(got, expected)
    assert hist.total_count() == len(mt)


def test_histogram_matches_the_batch_decay():
    mt = exp_microtimes(180.0, 40_000, seed=2)
    tt = as_tttr(mt)
    batch = np.asarray(tt.get_microtime_histogram(1)[0], dtype=float)

    hist = tttrlib.StreamingDecayHistogram(len(batch), 1)
    for v in mt:
        hist.push_photon(int(v))
    got = np.asarray(hist.get_histogram(0))
    np.testing.assert_array_equal(got[:len(batch)], batch)


def test_histogram_separates_channels():
    rng = np.random.default_rng(3)
    mt = exp_microtimes(150.0, 30_000, seed=3)
    ch = (rng.random(len(mt)) < 0.4).astype(np.int8)

    hist = tttrlib.StreamingDecayHistogram(N_BINS, 2)
    for v, c in zip(mt, ch):
        hist.push_photon(int(v), int(c))
    for c in (0, 1):
        expected = np.bincount(mt[ch == c].astype(np.int64), minlength=N_BINS).astype(float)
        np.testing.assert_array_equal(np.asarray(hist.get_histogram(c)), expected)
        assert hist.get_count(c) == int((ch == c).sum())


def test_histogram_drops_out_of_range_rather_than_corrupting():
    hist = tttrlib.StreamingDecayHistogram(16, 2)
    for v in (0, 5, 15, 16, 100):        # 16 and 100 are past the last bin
        hist.push_photon(int(v))
    hist.push_photon(3, 7)               # channel 7 does not exist
    got = np.asarray(hist.get_histogram(0))
    assert got.sum() == 3
    assert hist.total_count() == 3


# ------------------------------------------------------------------ phasor --

@pytest.mark.parametrize("tau_bins", [80.0, 220.0, 600.0])
def test_phasor_matches_the_batch(tau_bins):
    """Same photons, same (g, s). The batch's default IRF phasor (1, 0) is the
    identity correction, which is what the streaming class computes."""
    mt = exp_microtimes(tau_bins, 50_000, seed=4)
    freq = 1.0 / N_BINS                 # cycles per microtime bin
    # `compute_phasor_bincounts` is the same sum written over a histogram, and
    # is the binding the rest of the suite uses.
    counts = np.bincount(mt.astype(np.int64), minlength=N_BINS)
    batch = np.asarray(tttrlib.DecayPhasor.compute_phasor_bincounts(
        tttrlib.VectorInt32(counts.tolist()), freq, 1, 1.0, 0.0), dtype=float)

    # StreamingPhasor takes (frequency_MHz, n_bins, resolution) and forms
    # 2*pi*f_MHz*1e6*resolution; with resolution = 1 "bin", f_MHz = freq/1e6
    # makes the phase per bin identical to the batch's 2*pi*frequency.
    ph = tttrlib.StreamingPhasor(freq / 1e6, N_BINS, 1.0)
    for v in mt:
        ph.push_photon(int(v))
    g, s, n = np.asarray(ph.get_phasor(), dtype=float)

    assert n == len(mt)
    np.testing.assert_allclose([g, s], batch, rtol=0, atol=1e-12)


def test_phasor_lands_on_the_universal_semicircle():
    """A single exponential must sit on g^2 + s^2 = g, and the lifetime read off
    the phase must be the one that was simulated."""
    tau_bins = 300.0
    mt = exp_microtimes(tau_bins, 400_000, seed=5)
    freq = 1.0 / N_BINS
    ph = tttrlib.StreamingPhasor(freq / 1e6, N_BINS, 1.0)
    for v in mt:
        ph.push_photon(int(v))
    g, s, _ = np.asarray(ph.get_phasor(), dtype=float)

    # Distance from the semicircle centre (0.5, 0) must be the radius 0.5.
    r = np.hypot(g - 0.5, s)
    assert abs(r - 0.5) < 5e-3, f"off the semicircle: r = {r:.4f}"

    omega = 2.0 * np.pi * freq
    tau_phase = (s / g) / omega
    assert abs(tau_phase - tau_bins) / tau_bins < 0.02, (
        f"phase lifetime {tau_phase:.1f} vs simulated {tau_bins}")


def test_phasor_is_incremental():
    mt = exp_microtimes(250.0, 20_000, seed=6)
    freq = 1.0 / N_BINS
    whole = tttrlib.StreamingPhasor(freq / 1e6, N_BINS, 1.0)
    for v in mt:
        whole.push_photon(int(v))

    parts = tttrlib.StreamingPhasor(freq / 1e6, N_BINS, 1.0)
    for chunk in np.array_split(mt, 17):
        for v in chunk:
            parts.push_photon(int(v))

    np.testing.assert_array_equal(np.asarray(parts.get_phasor()),
                                  np.asarray(whole.get_phasor()))


def test_phasor_of_no_photons_is_not_a_nan():
    ph = tttrlib.StreamingPhasor(1.0 / N_BINS / 1e6, N_BINS, 1.0)
    g, s, n = np.asarray(ph.get_phasor(), dtype=float)
    assert n == 0
    assert np.isfinite(g) and np.isfinite(s)
