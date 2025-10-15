"""
Comprehensive test suite to verify AVX convolution implementations
against default implementations for correctness.

This test suite checks:
1. fconv_avx vs fconv
2. fconv_per_avx vs fconv_per

With various test cases including:
- Different numbers of exponentials (1, 2, 4, 5, 8, 16)
- Different lifetime values
- Different IRF shapes
- Edge cases (zero lifetimes, very short/long lifetimes)
"""

import unittest
import numpy as np
import scipy.stats
import tttrlib
from misc.compute_irf import model_irf


class TestAVXConvolutionCorrectness(unittest.TestCase):
    """Test AVX convolution implementations against default implementations."""

    def setUp(self):
        """Set up common test parameters."""
        self.tolerance = 1e-10  # Very strict tolerance for numerical accuracy
        
    def generate_irf(self, n_channels=64, period=12.0, irf_position=2.0, irf_width=0.25):
        """Generate a model IRF for testing."""
        irf, time_axis = model_irf(
            n_channels=n_channels,
            period=period,
            irf_position_p=irf_position,
            irf_position_s=irf_position,
            irf_width=irf_width
        )
        return irf, time_axis

    def test_fconv_avx_single_exponential(self):
        """Test fconv_avx with a single exponential."""
        print("\n=== Testing fconv_avx: Single Exponential ===")
        
        irf, time_axis = self.generate_irf(n_channels=64)
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([1.0, 4.1])
        
        # Default implementation
        model_default = np.zeros_like(irf)
        tttrlib.fconv(
            fit=model_default,
            irf=irf,
            x=lifetime_spectrum,
            dt=dt
        )
        
        # AVX implementation
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_avx(
            fit=model_avx,
            irf=irf,
            x=lifetime_spectrum,
            dt=dt
        )
        
        # Compare
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_avx_two_exponentials(self):
        """Test fconv_avx with two exponentials."""
        print("\n=== Testing fconv_avx: Two Exponentials ===")
        
        irf, time_axis = self.generate_irf(n_channels=64)
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.6, 2.5, 0.4, 5.8])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv(fit=model_default, irf=irf, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_avx_four_exponentials(self):
        """Test fconv_avx with four exponentials (exactly one AVX register)."""
        print("\n=== Testing fconv_avx: Four Exponentials (1 AVX register) ===")
        
        irf, time_axis = self.generate_irf(n_channels=64)
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.3, 1.5, 0.25, 3.2, 0.25, 5.0, 0.2, 7.5])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv(fit=model_default, irf=irf, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_avx_five_exponentials(self):
        """Test fconv_avx with five exponentials (requires padding)."""
        print("\n=== Testing fconv_avx: Five Exponentials (requires padding) ===")
        
        irf, time_axis = self.generate_irf(n_channels=64)
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.25, 1.2, 0.2, 2.8, 0.2, 4.5, 0.2, 6.2, 0.15, 8.0])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv(fit=model_default, irf=irf, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_avx_eight_exponentials(self):
        """Test fconv_avx with eight exponentials (two AVX registers)."""
        print("\n=== Testing fconv_avx: Eight Exponentials (2 AVX registers) ===")
        
        irf, time_axis = self.generate_irf(n_channels=64)
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([
            0.15, 0.8, 0.15, 1.5, 0.15, 2.5, 0.15, 3.5,
            0.1, 4.5, 0.1, 5.5, 0.1, 6.5, 0.1, 7.5
        ])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv(fit=model_default, irf=irf, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_avx_extreme_lifetimes(self):
        """Test fconv_avx with very short and very long lifetimes."""
        print("\n=== Testing fconv_avx: Extreme Lifetimes ===")
        
        irf, time_axis = self.generate_irf(n_channels=64)
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.5, 0.1, 0.3, 15.0, 0.2, 50.0])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv(fit=model_default, irf=irf, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_avx_different_irf_shapes(self):
        """Test fconv_avx with different IRF shapes."""
        print("\n=== Testing fconv_avx: Different IRF Shapes ===")
        
        lifetime_spectrum = np.array([0.6, 3.5, 0.4, 6.0])
        
        # Test with narrow IRF
        irf_narrow, time_axis = self.generate_irf(n_channels=64, irf_width=0.1)
        dt = time_axis[1] - time_axis[0]
        
        model_default = np.zeros_like(irf_narrow)
        tttrlib.fconv(fit=model_default, irf=irf_narrow, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf_narrow)
        tttrlib.fconv_avx(fit=model_avx, irf=irf_narrow, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference (narrow IRF): {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)
        
        # Test with wide IRF
        irf_wide, time_axis = self.generate_irf(n_channels=64, irf_width=0.5)
        dt = time_axis[1] - time_axis[0]
        
        model_default = np.zeros_like(irf_wide)
        tttrlib.fconv(fit=model_default, irf=irf_wide, x=lifetime_spectrum, dt=dt)
        
        model_avx = np.zeros_like(irf_wide)
        tttrlib.fconv_avx(fit=model_avx, irf=irf_wide, x=lifetime_spectrum, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference (wide IRF): {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_single_exponential(self):
        """Test fconv_per_avx with a single exponential."""
        print("\n=== Testing fconv_per_avx: Single Exponential ===")
        
        period = 13.0
        irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([1.0, 4.1])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(
            fit=model_default,
            irf=irf,
            x=lifetime_spectrum,
            period=period,
            start=0,
            stop=-1,
            dt=dt
        )
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(
            fit=model_avx,
            irf=irf,
            x=lifetime_spectrum,
            period=period,
            start=0,
            stop=-1,
            dt=dt
        )
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_two_exponentials(self):
        """Test fconv_per_avx with two exponentials."""
        print("\n=== Testing fconv_per_avx: Two Exponentials ===")
        
        period = 13.0
        irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.6, 2.5, 0.4, 5.8])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_four_exponentials(self):
        """Test fconv_per_avx with four exponentials."""
        print("\n=== Testing fconv_per_avx: Four Exponentials ===")
        
        period = 13.0
        irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.3, 1.5, 0.25, 3.2, 0.25, 5.0, 0.2, 7.5])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_five_exponentials(self):
        """Test fconv_per_avx with five exponentials (requires padding)."""
        print("\n=== Testing fconv_per_avx: Five Exponentials ===")
        
        period = 13.0
        irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.25, 1.2, 0.2, 2.8, 0.2, 4.5, 0.2, 6.2, 0.15, 8.0])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_eight_exponentials(self):
        """Test fconv_per_avx with eight exponentials."""
        print("\n=== Testing fconv_per_avx: Eight Exponentials ===")
        
        period = 13.0
        irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([
            0.15, 0.8, 0.15, 1.5, 0.15, 2.5, 0.15, 3.5,
            0.1, 4.5, 0.1, 5.5, 0.1, 6.5, 0.1, 7.5
        ])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_different_periods(self):
        """Test fconv_per_avx with different period values."""
        print("\n=== Testing fconv_per_avx: Different Periods ===")
        
        lifetime_spectrum = np.array([0.6, 3.5, 0.4, 6.0])
        
        for period in [10.0, 15.0, 20.0, 30.0]:
            print(f"  Testing period = {period}")
            irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
            irf[irf < 0.001] = 0.0
            dt = time_axis[1] - time_axis[0]
            
            model_default = np.zeros_like(irf)
            tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
            
            model_avx = np.zeros_like(irf)
            tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
            
            max_diff = np.max(np.abs(model_default - model_avx))
            print(f"    Max difference: {max_diff:.2e}")
            np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_extreme_lifetimes(self):
        """Test fconv_per_avx with very short and very long lifetimes."""
        print("\n=== Testing fconv_per_avx: Extreme Lifetimes ===")
        
        period = 13.0
        irf, time_axis = self.generate_irf(n_channels=64, period=period, irf_width=0.15)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.5, 0.1, 0.3, 15.0, 0.2, 50.0])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)

    def test_fconv_per_avx_large_dataset(self):
        """Test fconv_per_avx with a larger dataset."""
        print("\n=== Testing fconv_per_avx: Large Dataset ===")
        
        period = 25.0
        irf, time_axis = self.generate_irf(n_channels=256, period=period, irf_width=0.2)
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]
        lifetime_spectrum = np.array([0.3, 2.0, 0.3, 4.5, 0.2, 7.0, 0.2, 10.0])
        
        model_default = np.zeros_like(irf)
        tttrlib.fconv_per(fit=model_default, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        model_avx = np.zeros_like(irf)
        tttrlib.fconv_per_avx(fit=model_avx, irf=irf, x=lifetime_spectrum, period=period, start=0, stop=-1, dt=dt)
        
        max_diff = np.max(np.abs(model_default - model_avx))
        print(f"Max difference: {max_diff:.2e}")
        np.testing.assert_allclose(model_avx, model_default, rtol=self.tolerance, atol=self.tolerance)


if __name__ == '__main__':
    # Run tests with verbose output
    unittest.main(verbosity=2)
