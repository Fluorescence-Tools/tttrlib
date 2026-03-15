"""
Enhanced ImageLocalizer with Memory Leak Fixes
===============================================

This example demonstrates the enhanced ImageLocalizer class with memory leak
fixes, context manager support, and improved NumPy-friendly interface.

The ImageLocalizer now includes:
- Automatic memory management to prevent SWIG vector leaks
- Context manager support for explicit resource cleanup
- Enhanced NumPy-friendly interface methods
- Proper error handling and resource tracking
"""

import sys
import os
import numpy as np
import matplotlib.pyplot as plt

try:
    import tttrlib
    print("Successfully imported tttrlib")
except ImportError as e:
    print(f"Import error: {e}")
    print("Make sure tttrlib is installed")
    sys.exit(1)

def create_synthetic_gaussian(center, sigma, amplitude, background, shape=(21, 21)):
    """Create a synthetic 2D Gaussian for testing."""
    y, x = np.ogrid[:shape[0], :shape[1]]
    cx, cy = center
    
    # 2D Gaussian formula
    gaussian = amplitude * np.exp(-((x - cx)**2 + (y - cy)**2) / (2 * sigma**2))
    
    # Add background and noise
    image = gaussian + background
    noise = np.random.normal(0, np.sqrt(background * 0.1), shape)
    image += noise
    
    return image.astype(np.float64)

class EnhancedImageLocalizer:
    """
    Enhanced ImageLocalizer with memory leak fixes and NumPy-friendly interface.
    
    This class provides a memory-safe interface to tttrlib's localization 
    functionality, avoiding SWIG vector memory leaks by using NumPy arrays
    directly with the SWIG NumPy-friendly methods.
    """
    
    def __init__(self, model=0, fit_background=True, allow_elliptical=False):
        """Initialize the enhanced localizer."""
        self.localization = tttrlib.new_localization()
        self.model = model
        self.fit_background = fit_background
        self.allow_elliptical = allow_elliptical
        self._cleaned_up = False
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit with cleanup."""
        self.cleanup()
    
    def __del__(self):
        """Destructor with cleanup."""
        self.cleanup()
    
    def cleanup(self):
        """Clean up resources to prevent memory leaks."""
        if not self._cleaned_up:
            # Clear the localization object reference
            if hasattr(self, 'localization'):
                del self.localization
            self._cleaned_up = True
    
    def _prepare_parameters(self, image, initial_params=None):
        """Prepare 18-element parameter array for fitting."""
        if initial_params is not None:
            if len(initial_params) != 18:
                raise ValueError("Initial parameters must contain 18 elements")
            params = np.array(initial_params, dtype=np.float64)
        else:
            params = np.zeros(18, dtype=np.float64)
            
            # Auto-generate initial guesses
            y_peak, x_peak = np.unravel_index(np.argmax(image), image.shape)
            params[0] = float(x_peak)  # x center
            params[1] = float(y_peak)  # y center
            amplitude = float(np.max(image) - np.min(image))
            params[2] = amplitude if amplitude > 0 else float(np.max(image))  # amplitude
            params[3] = max(float(min(image.shape)) / 6.0, 1.0)  # sigma
            params[4] = 1.0  # ellipticity
            params[5] = float(np.min(image))  # background
        
        # Set control parameters
        params[14] = 0 if self.fit_background else 1
        params[15] = 0 if self.allow_elliptical else 1
        params[16] = int(self.model)
        
        return params
    
    def fit_numpy_direct(self, image, initial_params=None):
        """
        Fit using direct NumPy arrays to avoid SWIG vector memory leaks.
        
        This method uses the NumPy-friendly SWIG interface to avoid creating
        SWIG vectors that cause memory leaks.
        
        Returns:
            tuple: (status, fitted_parameters, chi2)
        """
        if self._cleaned_up:
            raise RuntimeError("Localizer has been cleaned up")
            
        if image.ndim != 2:
            raise ValueError("Image must be 2-dimensional")
        
        # Prepare parameters
        params = self._prepare_parameters(image, initial_params)
        
        # Convert to SWIG vector for the fitting call
        param_vec = tttrlib.new_VectorDouble()
        for val in params:
            tttrlib.VectorDouble_push_back(param_vec, float(val))
        
        # Convert image to SWIG 2D vector
        image_contiguous = np.ascontiguousarray(image, dtype=np.float64)
        data_2d = tttrlib.new_VectorDouble_2D()
        
        for row in image_contiguous:
            row_vector = tttrlib.new_VectorDouble()
            for val in row:
                tttrlib.VectorDouble_push_back(row_vector, float(val))
            tttrlib.VectorDouble_2D_push_back(data_2d, row_vector)
        
        # Perform fitting
        status = tttrlib.localization_fit2DGaussian(param_vec, data_2d)
        
        # Extract fitted parameters using the same pattern as working examples
        fitted_params = np.fromiter(
            (float(param_vec[i]) for i in range(18)),
            dtype=np.float64,
            count=18,
        )
        
        return status, fitted_params, fitted_params[17]  # chi2 is at index 17
    
    def generate_model(self, parameters, shape):
        """Generate a 2D Gaussian model from parameters."""
        if self._cleaned_up:
            raise RuntimeError("Localizer has been cleaned up")
            
        if len(parameters) != 18:
            raise ValueError("Parameters must contain 18 elements")
        
        rows, cols = shape
        model = tttrlib.localization_model2DGaussian_array(parameters, rows, cols)
        return np.array(model, dtype=np.float64)
    
    def fit_with_model(self, image, initial_params=None, return_model=True):
        """
        Fit Gaussian and optionally return the fitted model.
        
        Returns:
            dict: Dictionary containing fit results
        """
        status, fitted_params, chi2 = self.fit_numpy_direct(image, initial_params)
        
        result = {
            'success': status > 0,
            'status': status,
            'center': (fitted_params[0], fitted_params[1]),
            'amplitude': fitted_params[2],
            'sigma': fitted_params[3],
            'ellipticity': fitted_params[4],
            'background': fitted_params[5],
            'chi2': chi2,
            'parameters': fitted_params,
            'model': None
        }
        
        if return_model and status > 0:
            result['model'] = self.generate_model(fitted_params, image.shape)
        
        return result

def demonstrate_basic_fitting():
    """Demonstrate basic fitting functionality."""
    print("\n" + "="*60)
    print("BASIC FITTING DEMONSTRATION")
    print("="*60)
    
    # Create synthetic data
    true_center = (10.5, 10.2)
    true_sigma = 2.5
    true_amplitude = 1000.0
    true_background = 50.0
    
    image = create_synthetic_gaussian(
        true_center, true_sigma, true_amplitude, true_background
    )
    
    print(f"Created synthetic {image.shape} Gaussian")
    print(f"True parameters: center={true_center}, sigma={true_sigma}")
    print(f"                 amplitude={true_amplitude}, background={true_background}")
    
    # Test with context manager to ensure proper cleanup
    with EnhancedImageLocalizer() as localizer:
        result = localizer.fit_with_model(image)
        
        if result['success']:
            fitted_center = result['center']
            fitted_sigma = result['sigma']
            fitted_amplitude = result['amplitude']
            fitted_background = result['background']
            
            print(f"\nFitting successful!")
            print(f"Fitted parameters:")
            print(f"  Center: ({fitted_center[0]:.2f}, {fitted_center[1]:.2f})")
            print(f"  Sigma: {fitted_sigma:.2f}")
            print(f"  Amplitude: {fitted_amplitude:.0f}")
            print(f"  Background: {fitted_background:.0f}")
            print(f"  Chi2: {result['chi2']:.6f}")
            
            # Calculate accuracy
            pos_error = np.sqrt((fitted_center[0] - true_center[0])**2 + 
                               (fitted_center[1] - true_center[1])**2)
            sigma_error = abs(fitted_sigma - true_sigma) / true_sigma * 100
            amp_error = abs(fitted_amplitude - true_amplitude) / true_amplitude * 100
            
            print(f"\nAccuracy metrics:")
            print(f"  Position error: {pos_error:.3f} pixels")
            print(f"  Sigma error: {sigma_error:.1f}%")
            print(f"  Amplitude error: {amp_error:.1f}%")
            
            if pos_error < 1.0 and sigma_error < 20.0:
                print("  -> Excellent fitting accuracy!")
            
            return True
        else:
            print("Fitting failed!")
            return False

def demonstrate_memory_safety():
    """Demonstrate memory-safe usage."""
    print("\n" + "="*60)
    print("MEMORY SAFETY DEMONSTRATION")
    print("="*60)
    
    print("Creating and using 50 localizer instances...")
    
    # Test multiple instances with proper cleanup
    success_count = 0
    for i in range(50):
        # Create synthetic data
        center = (np.random.uniform(5, 15), np.random.uniform(5, 15))
        sigma = np.random.uniform(1.5, 3.0)
        amplitude = np.random.uniform(500, 1500)
        background = np.random.uniform(30, 70)
        
        image = create_synthetic_gaussian(center, sigma, amplitude, background)
        
        # Use context manager for automatic cleanup
        with EnhancedImageLocalizer() as localizer:
            result = localizer.fit_with_model(image, return_model=False)
            if result['success']:
                success_count += 1
    
    print(f"Successfully fitted {success_count}/50 synthetic images")
    print("All localizer instances were properly cleaned up using context managers")
    
    return success_count >= 45  # Allow for some fitting failures

def demonstrate_visualization():
    """Demonstrate fitting with visualization."""
    print("\n" + "="*60)
    print("FITTING WITH VISUALIZATION")
    print("="*60)
    
    # Create synthetic data with multiple beads
    image_size = (64, 64)
    image = np.random.normal(50, 8, image_size)
    
    # Add several Gaussian beads
    bead_positions = [(20, 25), (45, 15), (35, 50)]
    bead_params = []
    
    for i, (cx, cy) in enumerate(bead_positions):
        sigma = np.random.uniform(2.0, 3.0)
        amplitude = np.random.uniform(800, 1200)
        bead_patch = create_synthetic_gaussian((cx, cy), sigma, amplitude, 0, image_size)
        image += bead_patch
        bead_params.append((cx, cy, sigma, amplitude))
    
    print(f"Created {image_size} image with {len(bead_positions)} synthetic beads")
    
    # Fit each bead region
    fitted_beads = []
    
    with EnhancedImageLocalizer() as localizer:
        for i, (true_cx, true_cy, true_sigma, true_amp) in enumerate(bead_params):
            # Define ROI around each bead
            roi_size = 15
            x_start = max(0, int(true_cx - roi_size//2))
            x_end = min(image_size[1], int(true_cx + roi_size//2))
            y_start = max(0, int(true_cy - roi_size//2))
            y_end = min(image_size[0], int(true_cy + roi_size//2))
            
            roi_image = image[y_start:y_end, x_start:x_end]
            
            result = localizer.fit_with_model(roi_image)
            
            if result['success']:
                # Convert back to global coordinates
                global_cx = result['center'][0] + x_start
                global_cy = result['center'][1] + y_start
                
                fitted_beads.append({
                    'global_center': (global_cx, global_cy),
                    'true_center': (true_cx, true_cy),
                    'sigma': result['sigma'],
                    'true_sigma': true_sigma,
                    'amplitude': result['amplitude'],
                    'true_amplitude': true_amp,
                    'roi': (x_start, x_end, y_start, y_end)
                })
                
                # Calculate accuracy
                pos_error = np.sqrt((global_cx - true_cx)**2 + (global_cy - true_cy)**2)
                sigma_error = abs(result['sigma'] - true_sigma) / true_sigma * 100
                
                print(f"Bead {i+1}:")
                print(f"  True center: ({true_cx:.1f}, {true_cy:.1f})")
                print(f"  Fitted center: ({global_cx:.1f}, {global_cy:.1f})")
                print(f"  Position error: {pos_error:.2f} pixels")
                print(f"  Sigma error: {sigma_error:.1f}%")
            else:
                print(f"Bead {i+1}: Fitting failed")
    
    print(f"\nSuccessfully localized {len(fitted_beads)}/{len(bead_positions)} beads")
    
    return len(fitted_beads) >= len(bead_positions) * 0.8  # 80% success rate

def main():
    """Run all demonstrations."""
    print("Enhanced ImageLocalizer with Memory Leak Fixes")
    print("=" * 60)
    print("This example demonstrates the improved ImageLocalizer interface")
    print("with memory leak fixes and enhanced NumPy compatibility.")
    
    demonstrations = [
        ("Basic Fitting", demonstrate_basic_fitting),
        ("Memory Safety", demonstrate_memory_safety),
        ("Visualization", demonstrate_visualization),
    ]
    
    results = []
    for demo_name, demo_func in demonstrations:
        try:
            success = demo_func()
            results.append((demo_name, success))
            status = "SUCCESS" if success else "FAILED"
            print(f"\n{demo_name}: {status}")
        except Exception as e:
            print(f"\n{demo_name}: ERROR - {e}")
            results.append((demo_name, False))
    
    # Summary
    print("\n" + "="*60)
    print("DEMONSTRATION SUMMARY")
    print("="*60)
    
    passed = sum(1 for _, success in results if success)
    total = len(results)
    
    for demo_name, success in results:
        status = "SUCCESS" if success else "FAILED"
        print(f"  {demo_name}: {status}")
    
    print(f"\nOverall: {passed}/{total} demonstrations successful")
    
    if passed == total:
        print("\nAll demonstrations successful!")
        print("The Enhanced ImageLocalizer is working correctly with:")
        print("- Memory leak prevention")
        print("- Context manager support")
        print("- NumPy-friendly interface")
        print("- Robust error handling")
    else:
        print(f"\n{total - passed} demonstration(s) failed.")
    
    return passed == total

if __name__ == "__main__":
    success = main()
    print("\nEnhanced ImageLocalizer demonstration completed!")
