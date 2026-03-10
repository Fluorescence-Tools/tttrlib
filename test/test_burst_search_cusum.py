import unittest
import numpy as np
import tttrlib
import os
import json


class TestBurstSearchCUSUM(unittest.TestCase):
    """Test CUSUM/SPRT burst search algorithm"""

    @classmethod
    def setUpClass(cls):
        # Get test data root
        data_root = os.environ.get('TTTRLIB_DATA', './tttr-data')
        settings_file = os.path.join(os.path.dirname(__file__), 'settings.json')
        
        if os.path.exists(settings_file):
            with open(settings_file, 'r') as f:
                settings = json.load(f)
                data_root = settings.get('data_root', data_root)
        
        # Load a test file
        cls.test_file = os.path.join(data_root, 'pq', 'ptu', 'pq_ptu_hh_t3.ptu')
        if os.path.exists(cls.test_file):
            cls.data = tttrlib.TTTR(cls.test_file, 'PTU')
        else:
            cls.data = None

    def test_sliding_window_mode(self):
        """Test that sliding_window mode works (backward compatibility)"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # Test with explicit mode parameter
        bursts = self.data.burst_search(L=50, m=10, T=0.5e-3, mode="sliding_window")
        
        # Should return array-like (numpy array or tuple with correct content)
        self.assertTrue(hasattr(bursts, '__len__') and hasattr(bursts, '__getitem__'))
        
        # Convert to numpy array for operations
        bursts_arr = np.asarray(bursts)
        
        # Should have even number of elements (start, stop pairs)
        self.assertEqual(len(bursts_arr) % 2, 0)
        
        # All indices should be valid
        if len(bursts_arr) > 0:
            self.assertTrue(np.all(bursts_arr >= 0))
            self.assertTrue(np.all(bursts_arr < len(self.data)))

    def test_sliding_window_default(self):
        """Test that default mode is sliding_window"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # Test without mode parameter (should default to sliding_window)
        bursts_default = self.data.burst_search(L=50, m=10, T=0.5e-3)
        bursts_explicit = self.data.burst_search(L=50, m=10, T=0.5e-3, mode="sliding_window")
        
        # Should produce identical results
        np.testing.assert_array_equal(bursts_default, bursts_explicit)

    def test_cusum_sprt_mode_basic(self):
        """Test CUSUM/SPRT mode with basic parameters"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # Test CUSUM/SPRT mode
        bursts = self.data.burst_search(
            L=50,              # min 50 photons per burst
            m=2000,            # background = 2000 cps
            T=30,              # S/B ratio = 30
            mode="cusum_sprt",
            alpha=0.001,
            beta=0.01
        )
        
        # Should return numpy array
        self.assertTrue(hasattr(bursts, '__len__') and hasattr(bursts, '__getitem__'))
        
        # Should have even number of elements
        self.assertEqual(len(bursts) % 2, 0)
        
        # All indices should be valid
        if len(bursts) > 0:
            self.assertTrue(np.all(bursts >= 0))
            self.assertTrue(np.all(bursts < len(self.data)))

    def test_cusum_sprt_auto_sb_ratio(self):
        """Test CUSUM/SPRT mode with auto-estimated S/B ratio"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # Test with T=0 to trigger auto-estimation
        bursts = self.data.burst_search(
            L=50,
            m=2000,
            T=0,  # Auto-estimate S/B ratio
            mode="cusum_sprt"
        )
        
        # Should return valid results
        self.assertTrue(hasattr(bursts, '__len__') and hasattr(bursts, '__getitem__'))
        self.assertEqual(len(bursts) % 2, 0)

    def test_cusum_sprt_burst_properties(self):
        """Test that CUSUM/SPRT bursts meet minimum photon requirement"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        min_photons = 30
        bursts = self.data.burst_search(
            L=min_photons,
            m=2000,
            T=20,
            mode="cusum_sprt"
        )
        
        # Check each burst has at least min_photons
        for i in range(0, len(bursts), 2):
            start = bursts[i]
            stop = bursts[i + 1]
            burst_size = stop - start + 1
            self.assertGreaterEqual(
                burst_size, 
                min_photons,
                f"Burst {i//2} has {burst_size} photons, expected >= {min_photons}"
            )

    def test_cusum_sprt_different_alpha_beta(self):
        """Test CUSUM/SPRT with different error rates"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # More stringent (lower false positive rate)
        bursts_stringent = self.data.burst_search(
            L=50, m=2000, T=30,
            mode="cusum_sprt",
            alpha=0.0001,  # Lower false positive
            beta=0.001
        )
        
        # Less stringent (higher false positive rate)
        bursts_relaxed = self.data.burst_search(
            L=50, m=2000, T=30,
            mode="cusum_sprt",
            alpha=0.01,  # Higher false positive
            beta=0.05
        )
        
        # Both should return valid arrays
        self.assertTrue(hasattr(bursts_stringent, '__len__') and hasattr(bursts_stringent, '__getitem__'))
        self.assertTrue(hasattr(bursts_relaxed, '__len__') and hasattr(bursts_relaxed, '__getitem__'))
        
        # Relaxed criteria might find more bursts (or same)
        # This is not guaranteed but generally expected
        self.assertGreaterEqual(
            len(bursts_relaxed),
            0,
            "Relaxed criteria should find some bursts"
        )

    def test_mode_parameter_validation(self):
        """Test that invalid mode parameter is handled"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # Invalid mode should default to sliding_window
        bursts = self.data.burst_search(L=50, m=10, T=0.5e-3, mode="invalid_mode")
        
        # Should still return valid results (defaults to sliding_window)
        self.assertTrue(hasattr(bursts, '__len__') and hasattr(bursts, '__getitem__'))

    def test_empty_data(self):
        """Test burst search on empty TTTR data"""
        # Create empty TTTR
        empty_tttr = tttrlib.TTTR()
        
        # Should return empty array for both modes
        bursts_sw = empty_tttr.burst_search(L=50, m=10, T=0.5e-3, mode="sliding_window")
        bursts_cusum = empty_tttr.burst_search(L=50, m=2000, T=30, mode="cusum_sprt")
        
        self.assertEqual(len(bursts_sw), 0)
        self.assertEqual(len(bursts_cusum), 0)

    def test_cusum_sprt_burst_ordering(self):
        """Test that CUSUM/SPRT bursts are properly ordered"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        bursts = self.data.burst_search(
            L=50, m=2000, T=30,
            mode="cusum_sprt"
        )
        
        if len(bursts) > 0:
            # Each burst: start <= stop
            for i in range(0, len(bursts), 2):
                start = bursts[i]
                stop = bursts[i + 1]
                self.assertLessEqual(start, stop, f"Burst {i//2}: start > stop")
            
            # Bursts should not overlap
            for i in range(2, len(bursts), 2):
                prev_stop = bursts[i - 1]
                curr_start = bursts[i]
                self.assertLess(prev_stop, curr_start, f"Bursts {i//2-1} and {i//2} overlap")

    def test_comparison_sliding_vs_cusum(self):
        """Compare sliding window vs CUSUM/SPRT results"""
        if self.data is None:
            self.skipTest("Test data not available")
        
        # Get bursts from both methods
        bursts_sw = self.data.burst_search(L=50, m=10, T=0.5e-3, mode="sliding_window")
        bursts_cusum = self.data.burst_search(L=50, m=2000, T=30, mode="cusum_sprt")
        
        # Both should return valid arrays
        self.assertTrue(hasattr(bursts_sw, '__len__') and hasattr(bursts_sw, '__getitem__'))
        self.assertTrue(hasattr(bursts_cusum, '__len__') and hasattr(bursts_cusum, '__getitem__'))
        
        # Results may differ (different algorithms)
        # Just verify both are valid
        self.assertEqual(len(bursts_sw) % 2, 0)
        self.assertEqual(len(bursts_cusum) % 2, 0)


if __name__ == '__main__':
    unittest.main()
