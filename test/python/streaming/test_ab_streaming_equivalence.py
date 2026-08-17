# SPDX-License-Identifier: BSD-3-Clause
"""Streaming kernels against numpy references of the batch definitions,
under arbitrary chunking.

The streaming classes must produce, at any chunk boundary, exactly what the
batch computation produces on the photons pushed so far. The batch definitions
are transcribed in numpy here (not taken from tttrlib), and the streams are
cut at random points so no chunk boundary coincides with a bin boundary.

Already pinned elsewhere and not repeated: streaming vs batch tttrlib calls
for the intensity trace (test_streaming_intensity_trace.py), the decay
histogram + phasor (test_streaming_decay_and_phasor.py), the CLSM image on a
real HT3 scan against `CLSMImage` frame by frame (test_streaming_clsm_image.py),
the correlator (test_streaming_correlator.py) and the burst detector
(test_streaming_burst_detector.py).

Register: okf/testing/algorithm-validation.md
"""
import numpy as np
import pytest

import tttrlib


def _stream(n, seed, rate_per_clock=1e-3):
    rng = np.random.default_rng(seed)
    return np.cumsum(rng.exponential(1.0 / rate_per_clock, n)).astype(np.uint64)


def _random_cuts(n, k, seed):
    rng = np.random.default_rng(seed)
    cuts = np.sort(rng.integers(1, n, k))
    return np.concatenate([[0], cuts, [n]])


@pytest.mark.parametrize("seed", [1, 2, 3])
def test_intensity_trace_equals_numpy_bincount_at_every_cut(seed):
    mt = _stream(30_000, seed)
    tw, res = 1e-3, 50e-9
    cpb = max(1, int(np.floor(tw / res)))
    trace = tttrlib.StreamingIntensityTrace(tw, res)
    cuts = _random_cuts(mt.size, 25, seed)
    for a, b in zip(cuts[:-1], cuts[1:]):
        trace.push_np(mt[a:b])
        so_far = mt[:b]
        ref = np.bincount((so_far // cpb).astype(np.int64),
                          minlength=int(so_far[-1] // cpb) + 1)
        got = np.asarray(trace.y, dtype=np.int64)
        np.testing.assert_array_equal(got, ref)


@pytest.mark.parametrize("seed", [4, 5])
def test_decay_histogram_equals_numpy_bincount_per_channel_at_every_cut(seed):
    rng = np.random.default_rng(seed)
    n_bins, n_ch = 1024, 3
    mic = np.minimum(rng.exponential(200.0, 40_000), n_bins - 1).astype(np.uint16)
    ch = rng.integers(0, n_ch, mic.size).astype(np.int8)
    hist = tttrlib.StreamingDecayHistogram(n_bins, n_ch)
    cuts = _random_cuts(mic.size, 20, seed)
    for a, b in zip(cuts[:-1], cuts[1:]):
        for v, c in zip(mic[a:b], ch[a:b]):
            hist.push_photon(int(v), int(c))
        for c in range(n_ch):
            ref = np.bincount(mic[:b][ch[:b] == c].astype(np.int64), minlength=n_bins)
            np.testing.assert_array_equal(np.asarray(hist.get_histogram(c)), ref)
        assert hist.total_count() == b


def test_decay_histogram_push_arrays_equals_push_photon():
    rng = np.random.default_rng(6)
    n_bins = 512
    mic = rng.integers(0, n_bins, 20_000).astype(np.uint16)
    ch = rng.integers(0, 2, mic.size).astype(np.int8)
    a = tttrlib.StreamingDecayHistogram(n_bins, 2)
    b = tttrlib.StreamingDecayHistogram(n_bins, 2)
    a.push_np(mic, ch)
    for v, c in zip(mic, ch):
        b.push_photon(int(v), int(c))
    for c in range(2):
        np.testing.assert_array_equal(np.asarray(a.get_histogram(c)),
                                      np.asarray(b.get_histogram(c)))
        np.testing.assert_array_equal(np.asarray(a.get_histogram(c)),
                                      np.bincount(mic[ch == c].astype(np.int64), minlength=n_bins))
