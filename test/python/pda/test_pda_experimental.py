"""Exactness tests for Pda.compute_experimental_histograms.

The input is a synthetic photon stream whose per-time-window channel counts are
known by construction, so every output cell has a single correct value.
"""
from __future__ import division

import numpy as np
import pytest

import tttrlib

CH1, CH2 = 0, 1
WINDOW_GAP = 1000       # macro-time ticks between windows
TW_LENGTH = 100.0       # < WINDOW_GAP, so each burst is its own window


def make_tttr(counts):
    """A TTTR whose w-th well-separated window holds counts[w] == (n1, n2)."""
    macro, routing = [], []
    for w, (n1, n2) in enumerate(counts):
        base = w * WINDOW_GAP
        for k in range(n1):
            macro.append(base + k)
            routing.append(CH1)
        for k in range(n2):
            macro.append(base + n1 + k)
            routing.append(CH2)
    tttr = tttrlib.TTTR()
    tttr.append_events(
        np.asarray(macro, dtype=np.uint64),
        np.zeros(len(macro), dtype=np.uint16),
        np.asarray(routing, dtype=np.int8),
        np.zeros(len(macro), dtype=np.int8),
    )
    return tttr


def compute(tttr, nmin, nmax):
    return tttrlib.Pda.compute_experimental_histograms(
        tttr_data=tttr, channels_1=[CH1], channels_2=[CH2],
        maximum_number_of_photons=nmax, minimum_number_of_photons=nmin,
        minimum_time_window_length=TW_LENGTH,
    )


def test_every_time_window_is_counted_exactly_once():
    """Regression: the interleaved start/stop array was walked one index at a
    time, so half the windows came out empty and the rest were never reached."""
    counts = [(1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (2, 3), (4, 4)]
    s1s2, ps, _ = compute(make_tttr(counts), nmin=1, nmax=12)

    expected = np.zeros_like(s1s2)
    for n1, n2 in counts:
        expected[n1, n2] += 1.0
    np.testing.assert_array_equal(s1s2, expected)
    assert s1s2.sum() == len(counts)


def test_matrix_row_is_channel_one():
    """An asymmetric window pins the orientation against the model matrix."""
    s1s2, _, _ = compute(make_tttr([(7, 2)]), nmin=1, nmax=12)
    assert s1s2[7, 2] == 1.0
    assert s1s2[2, 7] == 0.0


def test_ps_is_the_total_count_histogram():
    counts = [(3, 1), (2, 2), (5, 2), (1, 3)]
    _, ps, _ = compute(make_tttr(counts), nmin=1, nmax=12)
    expected = np.zeros_like(ps)
    for n1, n2 in counts:
        expected[n1 + n2] += 1.0
    np.testing.assert_array_equal(ps, expected)


def test_indices_are_start_stop_pairs_covering_the_counted_photons():
    counts = [(2, 1), (3, 2), (4, 1)]
    tttr = make_tttr(counts)
    s1s2, _, idx = compute(tttr, nmin=1, nmax=12)

    assert len(idx) % 2 == 0
    assert len(idx) == 2 * int(s1s2.sum())
    starts, stops = idx[0::2], idx[1::2]
    assert (stops > starts).all()
    # the ranges tile the stream and recover the counts they were built from
    assert sum(stops - starts) == len(tttr)
    for (n1, n2), start, stop in zip(counts, starts, stops):
        routing = np.asarray(tttr.routing_channels)[start:stop]
        assert (routing == CH1).sum() == n1
        assert (routing == CH2).sum() == n2


def test_photon_count_bounds_are_respected():
    counts = [(1, 1), (5, 5), (9, 9)]     # totals 2, 10, 18
    s1s2, _, _ = compute(make_tttr(counts), nmin=4, nmax=12)
    assert s1s2.sum() == 1                # only the 10-photon window survives
    assert s1s2[5, 5] == 1.0


def test_in_memory_tttr_does_not_hang():
    """A header-less TTTR reports a macro-time resolution of -1; the negative
    tick count that produced used to spin get_time_window_ranges forever."""
    tttr = make_tttr([(2, 2), (3, 3)])
    ranges = tttr.get_time_window_ranges(TW_LENGTH, 1)
    assert len(ranges) == 4
    s1s2, _, _ = compute(tttr, nmin=1, nmax=12)
    assert s1s2.sum() == 2


def test_experimental_matrix_feeds_get_1dhistogram_directly():
    """The experimental and model matrices share one layout, so a projection
    of the data and of the model land on the same axis."""
    counts = [(8, 2)] * 5 + [(2, 8)] * 1
    s1s2, ps, _ = compute(make_tttr(counts), nmin=4, nmax=20)

    pda = tttrlib.Pda(hist2d_nmax=20, hist2d_nmin=4, pF=ps.tolist())
    pda.histogram_function = \
        lambda ch1, ch2: ch1 / (ch1 + ch2) if ch1 + ch2 else 0.0
    x, y = pda.get_1dhistogram(
        s1s2=s1s2.flatten(), x_min=0.005, x_max=0.995, n_bins=100, log_x=False,
    )
    assert y.sum() == len(counts)
    # the dominant population is 8/(8+2) = 0.8, not its mirror 0.2
    assert x[np.argmax(y)] == pytest.approx(0.8, abs=0.03)
