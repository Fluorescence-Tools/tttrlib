#!/usr/bin/env python
"""
BurstFilter.burst_properties structured dtype test
"""
import os
import sys

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

def test_burstfilter_structured_empty():
    import numpy as np
    import tttrlib

    # Empty TTTR / filter
    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    props = bf.burst_properties

    assert isinstance(props, np.ndarray)
    assert props.shape == (0,)
    assert props.dtype.names == ('start', 'stop', 'size', 'duration', 'count_rate')
    assert props.dtype['start'] == np.int64
    assert props.dtype['stop'] == np.int64
    assert props.dtype['size'] == np.int64
    assert props.dtype['duration'] == np.float64
    assert props.dtype['count_rate'] == np.float64

if __name__ == '__main__':
    import pytest
    raise SystemExit(pytest.main([__file__]))
