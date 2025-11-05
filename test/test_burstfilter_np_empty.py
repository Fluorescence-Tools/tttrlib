#!/usr/bin/env python
"""
BurstFilter NumPy/flat helpers on empty data
==========================================

Verifies that the NumPy-friendly and flat-output helper methods return
empty arrays with correct shapes/dtypes on empty TTTR data.
"""
import os
import sys

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

def test_burstfilter_np_and_flat_empty():
    import numpy as np
    import tttrlib

    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    # NumPy-friendly pairs -> (n,2) int64
    arr = bf.find_bursts()
    assert isinstance(arr, np.ndarray)
    assert arr.shape == (0, 2)
    assert arr.dtype == np.int64

    # Flat interleaved [s0,e0,...]
    flat = bf.get_bursts_flat()
    assert isinstance(flat, list) or hasattr(flat, '__iter__')
    flat_list = list(flat)
    assert flat_list == []

if __name__ == '__main__':
    import pytest
    raise SystemExit(pytest.main([__file__]))
