#!/usr/bin/env python
"""
BurstFilter sequence protocol on empty data
==========================================

Ensures __len__, __iter__, and __getitem__ behave correctly on empty filters.
"""
import os
import sys
import pytest

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def test_burstfilter_len_and_iter_empty():
    import tttrlib

    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    assert len(bf) == 0
    assert list(iter(bf)) == []


def test_burstfilter_getitem_raises_indexerror():
    import tttrlib

    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    with pytest.raises(IndexError):
        _ = bf[0]
