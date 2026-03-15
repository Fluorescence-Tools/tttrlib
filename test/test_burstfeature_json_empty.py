#!/usr/bin/env python
"""
BurstFeatureExtractor JSON on empty data
=======================================

Ensures to_json_string returns a JSON object with expected top-level keys
when instantiated from an empty BurstFilter.
"""
import os
import sys
import json

# Ensure repository root is importable if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))


def test_burstfeature_json_empty_keys():
    import tttrlib

    tttr = tttrlib.TTTR()
    bf = tttrlib.BurstFilter(tttr.Get())
    bfe = tttrlib.BurstFeatureExtractor(bf)

    s = bfe.to_json_string()
    obj = json.loads(s)

    # Expected keys exist even if arrays are empty
    for key in [
        "burst_properties",
        "channel_photons",
        "channel_indices",
        "fret_efficiencies",
    ]:
        assert key in obj, f"Missing key in JSON: {key}"
