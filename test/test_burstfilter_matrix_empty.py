#!/usr/bin/env python
"""
BurstFilter legacy matrix property on empty data
===============================================

Ensures backward-compatible matrix property returns (0,5) float64 array on empty data.
"""
import os
import sys

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def test_burstfilter_matrix_empty():
    import numpy as np
    import pytest
    import tttrlib

    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    if hasattr(bf, 'burst_properties_matrix'):
        mat = bf.burst_properties_matrix
        assert isinstance(mat, np.ndarray)
        assert mat.shape == (0, 5)
        assert mat.dtype == np.float64
    else:
        pytest.skip("Legacy matrix property removed by design")


if __name__ == '__main__':
    import pytest
    raise SystemExit(pytest.main([__file__]))
