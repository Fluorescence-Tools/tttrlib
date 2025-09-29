#!/usr/bin/env python3
"""
NumPy-friendly wrapper for tttrlib Image Localization

This module provides a clean NumPy interface for the tttrlib localization functionality,
making it easy to use NumPy arrays directly without manual conversion to SWIG vector types.
"""

import numpy as np
import tttrlib

class LocalizationNumPy:
    """
    NumPy-friendly wrapper for tttrlib localization functionality
    
    This class provides methods that accept and return NumPy arrays directly,
    handling all the conversion to/from the underlying SWIG interface automatically.
    """
    
    def __init__(self):
        """Initialize the localization wrapper"""
        self.localization = tttrlib.localization()
    
    def fit2DGaussian(self, parameters, image_data):
        """
        Fit 2D Gaussian to image data using NumPy arrays
        
        Args:
            parameters (list or np.array): 18-element parameter array
                [x0, y0, A, sigma, ellipticity, bg, x1, y1, A1, x2, y2, A2, 
                 info, wi_nowi, fit_bg, ellipt_circ, model, twoIstar]
            image_data (np.array): 2D numpy array containing image data
            
        Returns:
            int: Result code (>0 for success, <=0 for failure)
            
        Note:
            The parameters list is modified in-place with the fitted values.
        """
        # Convert parameters to list if it's a numpy array
        if isinstance(parameters, np.ndarray):
            params = parameters.tolist()
        else:
            params = list(parameters)
        
        # Ensure we have exactly 18 parameters
        if len(params) != 18:
            raise ValueError("Parameters must be a list/array of exactly 18 elements")
        
        # Convert NumPy array to nested Python list
        if isinstance(image_data, np.ndarray):
            data_list = image_data.tolist()
        else:
            data_list = image_data
        
        # Call the SWIG wrapper method
        result = self.localization.fit2DGaussian_array(params, data_list)
        
        # Copy fitted parameters back to original array if it was numpy
        if isinstance(parameters, np.ndarray):
            parameters[:] = params
        else:
            parameters[:] = params
            
        return result
    
    def model2DGaussian(self, parameters, rows, cols):
        """
        Generate 2D Gaussian model using NumPy arrays
        
        Args:
            parameters (list or np.array): 18-element parameter array
            rows (int): Number of rows in output model
            cols (int): Number of columns in output model
            
        Returns:
            np.array: 2D numpy array containing the generated Gaussian model
        """
        # Convert parameters to list if it's a numpy array
        if isinstance(parameters, np.ndarray):
            params = parameters.tolist()
        else:
            params = list(parameters)
        
        # Ensure we have exactly 18 parameters
        if len(params) != 18:
            raise ValueError("Parameters must be a list/array of exactly 18 elements")
        
        # Call the SWIG wrapper method
        result_list = self.localization.model2DGaussian_array(params, rows, cols)
        
        # Convert result to NumPy array
        return np.array(result_list)
    
    def fit_and_model(self, parameters, image_data):
        """
        Convenience method to fit 2D Gaussian and generate model
        
        Args:
            parameters (list or np.array): 18-element parameter array
            image_data (np.array): 2D numpy array containing image data
            
        Returns:
            tuple: (result_code, fitted_parameters, model_array)
                - result_code (int): Fitting result (>0 for success)
                - fitted_parameters (np.array): Fitted parameter values
                - model_array (np.array): Generated 2D Gaussian model
        """
        # Make a copy of parameters to avoid modifying the original
        params = np.array(parameters, dtype=float)
        
        # Perform fitting
        result = self.fit2DGaussian(params, image_data)
        
        # Generate model using fitted parameters
        rows, cols = image_data.shape
        model = self.model2DGaussian(params, rows, cols)
        
        return result, params, model

# Convenience function for direct usage
def fit_gaussian_numpy(parameters, image_data):
    """
    Convenience function to fit 2D Gaussian using NumPy arrays
    
    Args:
        parameters (list or np.array): 18-element parameter array
        image_data (np.array): 2D numpy array containing image data
        
    Returns:
        int: Result code (>0 for success, <=0 for failure)
        
    Note:
        The parameters list/array is modified in-place with the fitted values.
    """
    loc = LocalizationNumPy()
    return loc.fit2DGaussian(parameters, image_data)

def model_gaussian_numpy(parameters, rows, cols):
    """
    Convenience function to generate 2D Gaussian model using NumPy arrays
    
    Args:
        parameters (list or np.array): 18-element parameter array
        rows (int): Number of rows in output model
        cols (int): Number of columns in output model
        
    Returns:
        np.array: 2D numpy array containing the generated Gaussian model
    """
    loc = LocalizationNumPy()
    return loc.model2DGaussian(parameters, rows, cols)

# Example usage
if __name__ == "__main__":
    print("NumPy Localization Wrapper Example")
    print("=" * 40)
    
    # Create synthetic 2D Gaussian data
    size = 21
    center = size // 2
    x = np.arange(size)
    y = np.arange(size)
    X, Y = np.meshgrid(x, y)
    
    # True parameters
    true_x, true_y = 10.5, 10.2
    true_sigma = 2.5
    true_amplitude = 1000.0
    true_background = 50.0
    
    # Generate synthetic data
    gaussian = true_amplitude * np.exp(-((X - true_x)**2 + (Y - true_y)**2) / (2 * true_sigma**2)) + true_background
    gaussian += np.random.poisson(gaussian * 0.1)  # Add noise
    
    print(f"Created synthetic {size}x{size} Gaussian")
    print(f"True parameters: center=({true_x}, {true_y}), σ={true_sigma}, A={true_amplitude}")
    
    # Set up fitting parameters
    parameters = np.zeros(18)
    parameters[0] = center + 0.5  # x0 initial guess
    parameters[1] = center - 0.3  # y0 initial guess
    parameters[2] = true_amplitude * 0.9  # amplitude guess
    parameters[3] = true_sigma * 1.1  # sigma guess
    parameters[4] = 1.0  # ellipticity (circular)
    parameters[5] = true_background * 1.2  # background guess
    parameters[14] = 0  # fit background
    parameters[15] = 1  # circular Gaussian
    parameters[16] = 0  # single Gaussian model
    
    # Create localization object
    loc = LocalizationNumPy()
    
    # Fit using NumPy arrays directly
    result = loc.fit2DGaussian(parameters, gaussian)
    
    if result > 0:
        print("\n✓ Fitting successful!")
        print(f"Fitted parameters:")
        print(f"  Center: ({parameters[0]:.2f}, {parameters[1]:.2f}) [True: ({true_x}, {true_y})]")
        print(f"  Sigma: {parameters[3]:.2f} [True: {true_sigma}]")
        print(f"  Amplitude: {parameters[2]:.0f} [True: {true_amplitude}]")
        print(f"  Background: {parameters[5]:.0f} [True: {true_background}]")
        
        # Generate model
        model = loc.model2DGaussian(parameters, size, size)
        print(f"Generated model shape: {model.shape}")
        
        # Calculate residuals
        residuals = gaussian - model
        rms_error = np.sqrt(np.mean(residuals**2))
        print(f"RMS error: {rms_error:.2f}")
        
    else:
        print("✗ Fitting failed")
    
    print("\nNumPy interface is ready to use!")
