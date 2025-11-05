#!/usr/bin/env python
"""
BurstFeatureExtractor NumPy accessors smoke test
================================================

Verifies that the Python conveniences exposed via SWIG return NumPy arrays
with the expected shapes and dtypes on empty/dummy data. This keeps the
Python API stable and NumPy-friendly, as requested.
"""
import os
import sys

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

def test_burstfeature_numpy_empty_shapes():
    import numpy as np
    import tttrlib

    # Create dummy TTTR and BurstFilter
    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())

    # Instantiate feature extractor from filter
    bfe = tttrlib.BurstFeatureExtractor(bf)

    # NumPy-friendly accessors/properties (without _np suffix)
    props = bfe.burst_properties
    frets = bfe.fret_efficiencies
    ch_ph = bfe.channel_photons()
    ch_idx = bfe.channel_indices()

    # Expect empty structured array with correct dtype
    assert isinstance(props, np.ndarray)
    assert props.shape == (0,)
    assert props.dtype.names == ('start', 'stop', 'size', 'duration', 'count_rate')
    assert props.dtype['start'] == np.int64
    assert props.dtype['stop'] == np.int64
    assert props.dtype['size'] == np.int64
    assert props.dtype['duration'] == np.float64
    assert props.dtype['count_rate'] == np.float64

    assert isinstance(frets, np.ndarray)
    assert frets.shape == (0,)
    assert frets.dtype == float

    # Channel photons: empty dict on dummy data
    assert isinstance(ch_ph, dict)
    assert len(ch_ph) == 0

    # Channel indices: empty mapping (ragged lists)
    assert isinstance(ch_idx, dict)
    assert len(ch_idx) == 0

if __name__ == '__main__':
    ok = True
    try:
        test_burstfeature_numpy_empty_shapes()
    except Exception as e:
        ok = False
        import traceback
        traceback.print_exc()
    sys.exit(0 if ok else 1)
