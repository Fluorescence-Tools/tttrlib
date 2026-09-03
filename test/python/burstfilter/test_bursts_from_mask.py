"""`BurstFilter.bursts_from_mask` against the NumPy reference it replaces.

The kernel is the mask-level twin of the burst searches: a photon filter
produces a per-photon selection mask, and this converts it to inclusive
``[start, stop]`` pairs with gap merging. It was ported from chisurf's
``core.math.signal.find_bursts`` (owner placement ruling 2026-09-03: the
burst compute belongs to tttrlib), and the reference is transcribed below
whole so the port stays pinned to the exact behaviour every existing burst
table was built with -- including the reference's own off-by-one: at
``max_gap = g`` it bridges unselected stretches of up to ``g + 1`` photons
(its doctest merges a 2-gap at ``max_gap=1``).
"""
import numpy as np
import pytest
import tttrlib


def _numpy_reference(arr, max_gap=0):
    """chisurf ``find_bursts``, transcribed whole as the frozen reference."""
    if len(arr) == 0 or np.all(arr == 0):
        return np.empty((0, 2), dtype=int)
    is_burst = np.diff(arr, prepend=0, append=0)
    starts = np.where(is_burst == 1)[0]
    stops = np.where(is_burst == -1)[0]
    if len(starts) == 0 or len(stops) == 0:
        return np.empty((0, 2), dtype=int)
    if max_gap > 0:
        merged_starts = [starts[0]]
        merged_stops = []
        for i in range(1, len(starts)):
            if starts[i] - stops[i - 1] - 1 <= max_gap:
                continue
            merged_stops.append(stops[i - 1])
            merged_starts.append(starts[i])
        merged_stops.append(stops[-1])
        starts = np.array(merged_starts)
        stops = np.array(merged_stops)
    return np.column_stack((starts, stops - 1))


def test_the_reference_doctest_cases():
    arr = np.array([0, 1, 1, 0, 0, 1, 1, 1, 0], dtype=np.uint8)
    assert tttrlib.BurstFilter.bursts_from_mask(arr, 0).tolist() == [[1, 2], [5, 7]]
    assert tttrlib.BurstFilter.bursts_from_mask(arr, 1).tolist() == [[1, 7]]


def test_edges():
    empty = np.zeros(0, dtype=np.uint8)
    assert tttrlib.BurstFilter.bursts_from_mask(empty, 0).shape == (0, 2)
    zeros = np.zeros(7, dtype=np.uint8)
    assert tttrlib.BurstFilter.bursts_from_mask(zeros, 3).shape == (0, 2)
    ones = np.ones(4, dtype=np.uint8)
    assert tttrlib.BurstFilter.bursts_from_mask(ones, 0).tolist() == [[0, 3]]
    single = np.array([1], dtype=np.uint8)
    assert tttrlib.BurstFilter.bursts_from_mask(single, 0).tolist() == [[0, 0]]
    tail = np.array([0, 0, 1], dtype=np.uint8)
    assert tttrlib.BurstFilter.bursts_from_mask(tail, 0).tolist() == [[2, 2]]


@pytest.mark.parametrize("max_gap", [0, 1, 2, 5, 17])
@pytest.mark.parametrize("seed", [0, 1, 2, 3])
def test_random_masks_match_the_reference(max_gap, seed):
    rng = np.random.default_rng(seed)
    for density in (0.05, 0.5, 0.95):
        mask = (rng.random(4096) < density).astype(np.uint8)
        got = tttrlib.BurstFilter.bursts_from_mask(mask, max_gap)
        want = _numpy_reference(mask, max_gap)
        np.testing.assert_array_equal(got, want)
