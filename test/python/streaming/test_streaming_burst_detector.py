"""StreamingBurstDetector against `TTTR::burst_search_sliding_window`.

The two implement the same criterion — a window of m photons spanning no more
than T — so on the same photons they must return the same burst boundaries, not
merely a similar number of bursts. Boundaries are compared index for index.
"""
import numpy as np
import pytest

import tttrlib


def as_tttr(times, resolution=1.0):
    t = np.asarray(times, dtype=np.uint64)
    tt = tttrlib.TTTR()
    tt.append_events(
        macro_times=t,
        micro_times=np.zeros(len(t), dtype=np.uint16),
        routing_channels=np.zeros(len(t), dtype=np.int8),
        event_types=np.zeros(len(t), dtype=np.int8),
    )
    tt.header.set_macro_time_resolution(resolution)
    return tt


def batch_bursts(times, L, m, T, resolution=1.0):
    tt = as_tttr(times, resolution)
    return np.asarray(tt.burst_search_sliding_window(L, m, T),
                      dtype=np.int64).reshape(-1, 2)


def stream_bursts(times, L, m, T, resolution=1.0, chunks=1):
    det = tttrlib.StreamingBurstDetector(m, T, resolution)
    det.set_min_photons(L)
    for chunk in np.array_split(np.asarray(times, dtype=np.uint64), chunks):
        for v in chunk:
            det.push_photon(int(v))
    det.flush()
    return np.asarray(det.get_burst_indices(), dtype=np.int64).reshape(-1, 2)


def bursty_stream(seed, n_bg=4000, span=200_000, centres=(20_000, 70_000, 130_000),
                  burst_width=250, burst_n=90):
    """Poisson background with a few bright bursts on top. Integer times over a
    short span mean coincident macro times occur, which is the case that used to
    be inverted."""
    rng = np.random.default_rng(seed)
    bg = rng.integers(0, span, size=n_bg)
    bursts = np.concatenate([rng.integers(c, c + burst_width, size=burst_n)
                             for c in centres])
    return np.sort(np.concatenate([bg, bursts])).astype(np.uint64)


# ---------------------------------------------------------------------------

@pytest.mark.parametrize("seed", [1, 2, 3])
@pytest.mark.parametrize("L,m,T", [(20, 10, 30.0), (5, 5, 15.0), (50, 20, 120.0)])
def test_boundaries_match_batch(seed, L, m, T):
    t = bursty_stream(seed)
    batch = batch_bursts(t, L, m, T)
    stream = stream_bursts(t, L, m, T)
    assert len(stream) == len(batch), (
        f"{len(stream)} streaming bursts vs {len(batch)} batch")
    np.testing.assert_array_equal(stream, batch)


def test_coincident_photons_are_a_burst():
    """m photons in one macro-time tick is the highest count rate the detector
    can see. Computing a rate as m/span and guarding span == 0 with rate := 0
    turns that into no burst at all."""
    t = np.array([0] * 8 + [5000, 10000, 15000], dtype=np.uint64)
    batch = batch_bursts(t, 1, 5, 30.0)
    stream = stream_bursts(t, 1, 5, 30.0)
    assert len(batch) == 1, "the batch search finds it"
    np.testing.assert_array_equal(stream, batch)


def test_burst_ends_where_the_batch_ends_it():
    """The window that fails ends the burst at the previous window's last
    photon, not at the photon just pushed."""
    t = np.concatenate([np.arange(0, 10), np.arange(1000, 11000, 1000)]).astype(np.uint64)
    batch = batch_bursts(t, 1, 5, 30.0)
    stream = stream_bursts(t, 1, 5, 30.0)
    np.testing.assert_array_equal(stream, batch)
    assert stream[0].tolist() == [0, 9]


def test_chunked_delivery_is_identical():
    t = bursty_stream(7)
    whole = stream_bursts(t, 20, 10, 30.0, chunks=1)
    chunked = stream_bursts(t, 20, 10, 30.0, chunks=23)
    np.testing.assert_array_equal(chunked, whole)


def test_open_burst_is_closed_by_flush():
    t = np.arange(0, 40, dtype=np.uint64)          # uniformly dense to the end
    batch = batch_bursts(t, 1, 5, 30.0)
    det = tttrlib.StreamingBurstDetector(5, 30.0, 1.0)
    det.set_min_photons(1)
    for v in t:
        det.push_photon(int(v))
    assert det.burst_count == 0, "the burst is still open before flush()"
    det.flush()
    np.testing.assert_array_equal(
        np.asarray(det.get_burst_indices(), dtype=np.int64).reshape(-1, 2), batch)


def test_memory_is_bounded_by_the_window():
    """A live acquisition is unbounded; keeping every macro time is not an
    option. Nothing here can measure the allocation, so this pins the behaviour
    that depends on it: photons far past the window are still handled."""
    t = np.arange(0, 2_000_000, 7, dtype=np.uint64)
    det = tttrlib.StreamingBurstDetector(10, 30.0, 1.0)
    det.set_min_photons(1)
    for v in t:
        det.push_photon(int(v))
    det.flush()
    assert det.photon_count() == len(t)
    np.testing.assert_array_equal(
        np.asarray(det.get_burst_indices(), dtype=np.int64).reshape(-1, 2),
        batch_bursts(t, 1, 10, 30.0))


def test_push_returns_true_only_when_a_burst_closes():
    t = np.concatenate([np.arange(0, 10), np.arange(1000, 4000, 1000)]).astype(np.uint64)
    det = tttrlib.StreamingBurstDetector(5, 30.0, 1.0)
    det.set_min_photons(1)
    flags = [det.push_photon(int(v)) for v in t]
    assert sum(flags) == 1, f"exactly one burst closes here, got {sum(flags)}"
    assert flags[10] is True or flags[10] == 1, "it closes on the first sparse photon"


@pytest.mark.parametrize("bad", [
    dict(window_photons=10, window_time=5.0, macro_time_resolution=-1.0),
    dict(window_photons=10, window_time=0.0, macro_time_resolution=1.0),
    dict(window_photons=0, window_time=5.0, macro_time_resolution=1.0),
])
def test_meaningless_parameters_raise(bad):
    """A default-constructed TTTR reports a macro-time resolution of -1, and a
    negative resolution silently made the whole stream one burst."""
    with pytest.raises(Exception):
        tttrlib.StreamingBurstDetector(bad["window_photons"], bad["window_time"],
                                       bad["macro_time_resolution"])
