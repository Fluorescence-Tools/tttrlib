#!/usr/bin/env python3
"""
Test script for TTTR merge method
"""

import sys
import os
import unittest
import tttrlib
from pathlib import Path

# Use TTTRLIB_DATA environment variable or test/settings.json pattern
from test_settings import DATA_ROOT, DATA_AVAILABLE

# Resolve data paths using TTTRLIB_DATA env var or settings
HDF5_FILE = DATA_ROOT / "hdf" / "1a_1b_Mix.hdf5" if DATA_AVAILABLE else None
BH_FILE = DATA_ROOT / "bh" / "bh_spc132.spc" if DATA_AVAILABLE else None

class TestTTTRMerge(unittest.TestCase):

    def test_merge_hdf5(self):
        """Test merging Photon-HDF5 files"""
        if not HDF5_FILE or not HDF5_FILE.exists():
            self.skipTest(f"HDF5 test file not found or TTTRLIB_DATA not set")
        
        try:
            t1 = tttrlib.TTTR(HDF5_FILE, "PHOTON-HDF5")
            t2 = tttrlib.TTTR(HDF5_FILE, "PHOTON-HDF5")
            
            n1 = len(t1)
            n2 = len(t2)
            
            print(f"Merging HDF5: {n1} + {n2}")
            # Try passing arguments without strategy first to see if it's the 4th arg causing issues
            t1.merge(t2, 0, 0) # stack
            
            self.assertEqual(len(t1), n1 + n2)
            # Access data to ensure no crash
            _ = t1.macro_times
        except Exception as e:
            if "truncated" in str(e).lower():
                self.skipTest("HDF5 file truncated")
            raise

    def test_merge_bh(self):
        """Test merging BH files (as fallback/additional check)"""
        if not BH_FILE or not BH_FILE.exists():
            self.skipTest(f"BH test file not found or TTTRLIB_DATA not set")
            
        t1 = tttrlib.TTTR(BH_FILE, "SPC-130")
        t2 = tttrlib.TTTR(BH_FILE, "SPC-130")
        
        n1 = len(t1)
        n2 = len(t2)
        
        print(f"Merging BH: {n1} + {n2}")
        t1.merge(t2, 0, 1) # stack with channel offset
        
        self.assertEqual(len(t1), n1 + n2)
        # Verify channel offset
        channels = t1.routing_channels
        unique_channels = set(channels)
        self.assertTrue(any(c > 0 for c in unique_channels))

    def test_interleave_merge(self):
        """Test interleaving merge (chronological merge)"""
        if not BH_FILE or not BH_FILE.exists():
            self.skipTest("Test file not found or TTTRLIB_DATA not set")
            
        t1 = tttrlib.TTTR(BH_FILE, "SPC-130")
        t2 = tttrlib.TTTR(BH_FILE, "SPC-130")
        
        # Interleave merge
        # Strategy 1 is interleave
        t1.merge(t2, 0, 1, 1)
        
        # Verify chronological order
        mt = t1.macro_times
        for i in range(len(mt) - 1):
            if mt[i] > mt[i+1]:
                self.fail(f"Not chronological at index {i}: {mt[i]} > {mt[i+1]}")

if __name__ == "__main__":
    unittest.main()
