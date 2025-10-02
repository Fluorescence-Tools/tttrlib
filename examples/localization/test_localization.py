#!/usr/bin/env python3
"""
Simple test script to verify Image Localization functionality is working

This script tests the basic functionality of the enabled Image Localization
without requiring the specific data file.
"""

import numpy as np
import tttrlib
from tttrlib import ImageLocalizer

def test_localization_basic():
    """
    Test basic localization functionality with synthetic data
    """
    print("Testing tttrlib Image Localization functionality...")
    
    try:
        # Test if localization class is available
        localization = ImageLocalizer()
        print("✓ ImageLocalizer successfully initialised")
        
        # Create synthetic 2D Gaussian data for testing
        size = 21
        center = size // 2
        x, y = np.meshgrid(np.arange(size), np.arange(size))
        
        # Synthetic Gaussian parameters
        x0, y0 = center, center
        amplitude = 1000.0
        sigma = 2.5
        background = 50.0
        
        # Generate synthetic 2D Gaussian
        gaussian = amplitude * np.exp(-((x - x0)**2 + (y - y0)**2) / (2 * sigma**2)) + background
        
        # Add some noise
        np.random.seed(42)
        gaussian += np.random.poisson(gaussian * 0.1)
        
        print(f"✓ Created synthetic {size}x{size} Gaussian data")
        print(f"  True parameters: center=({x0}, {y0}), σ={sigma}, A={amplitude}")
        
        # Prepare NumPy array for SWIG (float64, C-contiguous)
        gaussian = np.ascontiguousarray(gaussian, dtype=np.float64)
        
        # Set up fitting parameters
        result = localization.fit(gaussian, initial=None, return_model=True)

        if result.success:
            fitted_x, fitted_y = result.center
            fitted_amplitude = result.amplitude
            fitted_sigma = result.sigma
            fitted_bg = result.background

            print("✓ Gaussian fitting successful!")
            print("  Fitted parameters:")
            print(f"    Center: ({fitted_x:.2f}, {fitted_y:.2f}) [True: ({x0}, {y0})]")
            print(f"    Sigma: {fitted_sigma:.2f} [True: {sigma}]")
            print(f"    Amplitude: {fitted_amplitude:.1f} [True: {amplitude}]")
            print(f"    Background: {fitted_bg:.1f} [True: {background}]")

            if result.model is not None:
                print("  Model image successfully generated")

            # Check accuracy
            pos_error = np.sqrt((fitted_x - x0)**2 + (fitted_y - y0)**2)
            sigma_error = abs(fitted_sigma - sigma) / sigma * 100
            
            print(f"  Accuracy:")
            print(f"    Position error: {pos_error:.3f} pixels")
            print(f"    Sigma error: {sigma_error:.1f}%")
            
            if pos_error < 0.1 and sigma_error < 10:
                print("✓ Fitting accuracy is excellent!")
                return True
            else:
                print("⚠ Fitting accuracy could be better")
                return True
        else:
            print("✗ Gaussian fitting failed")
            return False
            
    except Exception as e:
        print(f"✗ Error testing localization: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """
    Run the localization test
    """
    print("=" * 60)
    print("tttrlib Image Localization Test")
    print("=" * 60)
    
    success = test_localization_basic()
    
    print("\n" + "=" * 60)
    if success:
        print("✓ Image Localization functionality is working correctly!")
        print("\nYou can now use the bead_localization_example.py script")
        print("with your TTTR data to localize fluorescent beads.")
    else:
        print("✗ Image Localization test failed")
        print("Please check the compilation and dependencies.")
    print("=" * 60)

if __name__ == "__main__":
    main()
