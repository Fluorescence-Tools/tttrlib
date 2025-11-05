#!/usr/bin/env python
"""
BurstFilter helper methods on empty data
=======================================

Verifies that the BurstFilter Python convenience methods that return Python lists
behave well on empty/dummy data (no TTTR loaded): they should return empty lists.
"""
import os
import sys

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def test_burstfilter_helpers_empty_lists():
    import tttrlib

    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    assert bf.find_bursts_as_list() == []
    assert bf.filter_by_size_as_list(10) == []
    assert bf.filter_by_duration_as_list(1e-4) == []
    assert bf.filter_by_background_as_list(5.0) == []
    assert bf.merge_bursts_as_list(5) == []
    assert bf.get_bursts_as_list() == []


if __name__ == '__main__':
    import pytest
    raise SystemExit(pytest.main([__file__]))
