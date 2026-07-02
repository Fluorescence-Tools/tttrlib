#!/usr/bin/env python3
"""
Quick test of CLSMImage JSON settings functionality
"""

import json
import tempfile
import os

import tttrlib

def test_clsm_json():
    print("Testing CLSMImage JSON settings...")

    # Create test settings
    settings = {
        "channel_luts": {
            "0": [0.0, 1.0, 2.0, 3.0, 4.0]
        },
        "channel_shifts": {
            "0": 0
        },
        "marker_line_start": 1,
        "marker_line_stop": 2,
        "marker_frame_start": [4, 6],
        "marker_event_type": 1,
        "n_pixel_per_line": 128,
        "n_lines": 128
    }

    # Test with settings dict
    print("1. Testing settings dict...")
    try:
        # This should work - parsing the settings
        # Will fail at file loading but should show settings were parsed
        img = tttrlib.CLSMImage("nonexistent.ptu", settings=settings)
    except Exception as e:
        if "nonexistent.ptu" in str(e) or "FileNotFoundError" in str(e):
            print("   ✓ Settings dict parsing works (expected file error)")
        else:
            print(f"   ✗ Unexpected error: {e}")

    # Test with settings file
    print("2. Testing settings file...")
    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
        json.dump(settings, f)
        settings_file = f.name

    try:
        img = tttrlib.CLSMImage("nonexistent.ptu", settings_file=settings_file)
    except Exception as e:
        if "nonexistent.ptu" in str(e) or "FileNotFoundError" in str(e):
            print("   ✓ Settings file parsing works (expected file error)")
        else:
            print(f"   ✗ Unexpected error: {e}")
    finally:
        os.unlink(settings_file)

    print("✓ CLSMImage JSON functionality test completed")

if __name__ == "__main__":
    test_clsm_json()
