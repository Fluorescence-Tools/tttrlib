#!/usr/bin/env python

"""
Simple test for BurstFilter JSON functionality
=============================================

This test verifies that the BurstFilter class correctly handles JSON serialization
and deserialization using to_json_string and from_json_string methods.
"""

import json
import sys
import os

# Add the parent directory to the path to import tttrlib
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

def test_burstfilter_json():
    """Test BurstFilter JSON functionality."""
    import tttrlib

    # Create a dummy TTTR object
    tttr_data = tttrlib.TTTR()

    # Create BurstFilter
    burst_filter = tttrlib.BurstFilter(tttr_data.Get())

    # Set some parameters
    burst_filter.set_burst_parameters(20, 10, 1.0e-3)
    burst_filter.set_background_parameters(10.0, 0.0)

    # Test to_json_string
    json_string = burst_filter.to_json_string()

    # Verify it's valid JSON
    parsed = json.loads(json_string)

    # Check expected structure
    assert "burst_parameters" in parsed, "Missing burst_parameters"
    assert "background_parameters" in parsed, "Missing background_parameters"

    # Create a new filter and load parameters
    burst_filter2 = tttrlib.BurstFilter(tttr_data.Get())
    burst_filter2.from_json_string(json_string)

    # Verify parameters were loaded correctly — round-trip must be identical
    json_string2 = burst_filter2.to_json_string()
    assert json_string == json_string2, "JSON strings don't match after round-trip"


if __name__ == "__main__":
    test_burstfilter_json()
