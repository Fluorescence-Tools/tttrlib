"""StreamingIntensityTrace against the batch ``compute_intensity_trace``.

The batch function bins photons on a grid aligned to macro time 0, so a
streaming version that keeps the same grid is not an approximation of it — it
is the same histogram, filled in a different order. The tests hold it to that:
equality bin for bin, whole-stream and chunked, with chunk boundaries that
deliberately fall *inside* bins, since a binner that resets its partial bin at
a chunk boundary is the failure mode this class exists to avoid.
"""
import numpy as np
import pytest

import tttrlib


def photon_stream(n, rate_per_clock, seed):
    """Non-decreasing macro times, as any TTTR stream delivers them."""
    rng = np.random.default_rng(seed)
    gaps = rng.exponential(1.0 / rate_per_clock, size=n)
    return np.cumsum(gaps).astype(np.uint64)


def batch_trace(macro_times, time_window, resolution):
    return np.asarray(
        tttrlib.compute_intensity_trace(
            np.ascontiguousarray(macro_times, dtype=np.uint64),
            time_window_length=time_window,
            macro_time_resolution=resolution,
        ),
        dtype=float,
    )


def test_whole_stream_equals_batch():
    mt = photon_stream(50_000, 1e-3, seed=1)
    trace = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    trace.push_np(mt)
    np.testing.assert_array_equal(trace.y, batch_trace(mt, 1e-3, 50e-9))


@pytest.mark.parametrize("chunk", [1, 7, 1000, 49_999])
def test_chunked_equals_whole(chunk):
    """Chunk boundaries do not fall on bin boundaries, and must not matter."""
    mt = photon_stream(50_000, 1e-3, seed=2)
    trace = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    for i in range(0, len(mt), chunk):
        trace.push_np(mt[i:i + chunk])
    np.testing.assert_array_equal(trace.y, batch_trace(mt, 1e-3, 50e-9))


def test_silent_gaps_are_zero_bins_not_missing_bins():
    """A stream that goes dark keeps its place on the time axis."""
    mt = np.array([0, 1, 2, 1_000_000, 1_000_001], dtype=np.uint64)
    trace = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)  # 20_000 clocks/bin
    trace.push_np(mt)
    y = trace.y
    assert len(y) == 51
    assert y[0] == 3
    assert y[50] == 2
    assert y[1:50].sum() == 0
    np.testing.assert_array_equal(y, batch_trace(mt, 1e-3, 50e-9))


def test_bin_width_matches_the_batch_definition():
    """floor(window / resolution) clocks per bin, at least one."""
    assert tttrlib.StreamingIntensityTrace(1e-3, 50e-9).clocks_per_bin() == 20_000
    assert tttrlib.StreamingIntensityTrace(1e-3, 3e-9).clocks_per_bin() == 333_333
    # A window narrower than one clock still bins one clock, as in the batch.
    assert tttrlib.StreamingIntensityTrace(1e-12, 50e-9).clocks_per_bin() == 1


def test_rolling_window_keeps_the_tail_and_says_where_it_starts():
    """The bounded mode is what makes a live display O(1) in run length."""
    mt = photon_stream(50_000, 1e-3, seed=3)
    full = batch_trace(mt, 1e-3, 50e-9)

    trace = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    trace.set_max_bins(100)
    trace.push_np(mt)

    y = trace.y
    assert len(y) == 100
    np.testing.assert_array_equal(y, full[-100:])
    # The dropped bins are counted, not forgotten: the retained window still
    # knows its absolute position on the time axis.
    assert trace.first_bin_index() == len(full) - 100
    np.testing.assert_allclose(
        trace.x, np.arange(len(full) - 100, len(full)) * trace.bin_width()
    )
    assert trace.photon_count() == len(mt)


def test_out_of_order_photon_raises_rather_than_landing_in_the_wrong_bin():
    trace = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    trace.set_max_bins(2)
    trace.push_np(np.array([0, 10_000_000], dtype=np.uint64))
    # Inside the retained window: countable, and counted.
    trace.push_np(np.array([9_999_999], dtype=np.uint64))
    with pytest.raises(RuntimeError):
        trace.push_np(np.array([0], dtype=np.uint64))


def test_weights_are_summed():
    mt = np.array([0, 0, 20_000], dtype=np.uint64)
    trace = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    trace.push_np(mt, weights=np.array([0.5, 2.0, 3.0]))
    np.testing.assert_allclose(trace.y, [2.5, 3.0])


def test_a_chunk_crosses_the_boundary_once():
    """The array push is one call, not a Python loop over push_photon.

    A regression here is not a crash but a live acquisition that cannot keep
    up, so it is asserted rather than left to a benchmark nobody runs. The
    margin is deliberately loose (5x): the point is the shape of the cost, not
    a number that will drift with the machine.
    """
    import time

    mt = photon_stream(200_000, 1e-3, seed=4)

    a = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    t0 = time.perf_counter()
    a.push_np(mt)
    array_push = time.perf_counter() - t0

    b = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)
    n_loop = 20_000
    t0 = time.perf_counter()
    for t in mt[:n_loop].tolist():
        b.push_photon(t)
    per_photon_loop = (time.perf_counter() - t0) / n_loop

    assert array_push / len(mt) < per_photon_loop / 5.0
