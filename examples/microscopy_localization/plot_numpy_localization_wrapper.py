"""
=======================================
NumPy-friendly Localization Wrapper
=======================================

This example demonstrates a NumPy-friendly wrapper for the tttrlib Image Localization
functionality, making it easy to use NumPy arrays directly without manual conversion
to SWIG vector types.

The wrapper provides a clean interface that handles the conversion between NumPy arrays
and the SWIG vector format required by the underlying C++ localization algorithms.

"""

import numpy as np
import tttrlib

#%%
# NumPy-friendly localization wrapper class
# -----------------------------------------
# We create a wrapper class that provides a clean NumPy interface for the
# tttrlib localization functionality.

class LocalizationNumPy:
    """NumPy-friendly wrapper for tttrlib Image Localization"""
    
    def __init__(self):
        """Initialize the localization wrapper"""
        self.last_result = None
        self.last_vars = None
    
    def fit2DGaussian(self, parameters, image_data):
        """
        Fit a 2D Gaussian to image data using NumPy arrays
        
        Parameters
        ----------
        parameters : array_like
            18-element parameter array with initial guesses and fitting options
        image_data : numpy.ndarray
            2D image data as NumPy array
            
        Returns
        -------
        int
            Status code from the fitting algorithm (>0 for success)
        """
        
        # Make a copy of parameters to avoid modifying the input
        vars = list(parameters)
        
        # Convert NumPy array to SWIG vector format
        data_2d = tttrlib.new_VectorDouble_2D()
        for row in image_data:
            row_vector = tttrlib.new_VectorDouble()
            for val in row:
                tttrlib.VectorDouble_push_back(row_vector, float(val))
            tttrlib.VectorDouble_2D_push_back(data_2d, row_vector)
        
        # Perform fitting
        result = tttrlib.localization_fit2DGaussian(vars, data_2d)
        
        # Store results for later access
        self.last_result = result
        self.last_vars = vars
        
        # Update the input parameters array in-place
        parameters[:] = vars
        
        return result
    
    def model2DGaussian(self, parameters, rows=None, cols=None, shape=None):
        """
        Generate a 2D Gaussian model image
        
        Parameters
        ----------
        parameters : array_like
            18-element parameter array with Gaussian parameters
        rows : int, optional
            Number of rows in output image
        cols : int, optional
            Number of columns in output image
        shape : tuple, optional
            Shape of output image as (rows, cols)
            
        Returns
        -------
        list
            2D list representing the model image
        """
        
        if shape is not None:
            rows, cols = shape
        elif rows is None or cols is None:
            raise ValueError("Either 'shape' or 'rows' and 'cols' must be provided")
        
        # Generate model using SWIG interface
        vars = list(parameters)
        model = tttrlib.localization_model2DGaussian_array(vars, rows, cols)
        
        return model
    
    def fit_and_model(self, parameters, image_data):
        """
        Fit a Gaussian and return both the status code and model image
        
        Parameters
        ----------
        parameters : array_like
            18-element parameter array with initial guesses
        image_data : numpy.ndarray
            2D image data as NumPy array
            
        Returns
        -------
        tuple
            (status, fitted_parameters, model_image)
        """
        
        params = np.array(parameters, copy=True, dtype=float)
        status = self.fit2DGaussian(params, image_data)
        
        if status > 0:
            model = self.model2DGaussian(params, shape=image_data.shape)
        else:
            model = None
            
        return status, params, model

#%%
# Convenience functions
# ---------------------
# We provide standalone convenience functions for quick access to the
# localization functionality.

def fit_gaussian_numpy(parameters, image_data):
    """Convenience function for fitting a 2D Gaussian to NumPy array data"""
    loc = LocalizationNumPy()
    return loc.fit2DGaussian(parameters, image_data)

def model_gaussian_numpy(parameters, rows=None, cols=None, shape=None):
    """Convenience function for generating a 2D Gaussian model"""
    loc = LocalizationNumPy()
    return loc.model2DGaussian(parameters, rows=rows, cols=cols, shape=shape)

#%%
# Example usage demonstration
# ---------------------------
# Let's demonstrate the NumPy wrapper with a simple synthetic example.

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

print(f"Creating synthetic {size}x{size} Gaussian")
print(f"True parameters: center=({true_x}, {true_y}), sigma={true_sigma}, A={true_amplitude}")

# Generate synthetic data
gaussian = true_amplitude * np.exp(-((X - true_x)**2 + (Y - true_y)**2) / (2 * true_sigma**2)) + true_background
gaussian += np.random.poisson(gaussian * 0.1)  # Add noise

#%%
# Test the NumPy wrapper
# ----------------------
# Now we test the wrapper with our synthetic data.

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

#%%
# Display results
# ---------------
# Show the fitting results and accuracy metrics.

if result > 0:
    print("\nFitting successful!")
    print(f"Fitted parameters:")
    print(f"  Center: ({parameters[0]:.2f}, {parameters[1]:.2f}) [True: ({true_x}, {true_y})]")
    print(f"  Sigma: {parameters[3]:.2f} [True: {true_sigma}]")
    print(f"  Amplitude: {parameters[2]:.0f} [True: {true_amplitude}]")
    print(f"  Background: {parameters[5]:.0f} [True: {true_background}]")
    
    # Generate model
    model = loc.model2DGaussian(parameters, size, size)
    print(f"Generated model with shape: {len(model)}x{len(model[0])}")
    
    # Calculate accuracy metrics
    pos_error = np.sqrt((parameters[0] - true_x)**2 + (parameters[1] - true_y)**2)
    sigma_error = abs(parameters[3] - true_sigma) / true_sigma * 100
    amp_error = abs(parameters[2] - true_amplitude) / true_amplitude * 100
    
    print(f"\nAccuracy metrics:")
    print(f"  Position error: {pos_error:.3f} pixels")
    print(f"  Sigma error: {sigma_error:.1f}%")
    print(f"  Amplitude error: {amp_error:.1f}%")
    
    if pos_error < 1.0 and sigma_error < 20:
        print("Excellent fitting accuracy!")
    else:
        print("Good fitting results")
        
else:
    print("Fitting failed")

#%%
# Test convenience functions
# --------------------------
# Demonstrate the standalone convenience functions.

print(f"\nTesting convenience functions:")

# Test standalone fitting function
params_copy = np.array(parameters, copy=True)
status = fit_gaussian_numpy(params_copy, gaussian)
print(f"Standalone fit result: {'Success' if status > 0 else 'Failed'}")

# Test standalone model function
if status > 0:
    model_standalone = model_gaussian_numpy(params_copy, shape=(size, size))
    print(f"Standalone model generated: {len(model_standalone)}x{len(model_standalone[0])}")

print("\nNumPy wrapper interface is ready to use!")
print("This wrapper makes it easy to use tttrlib localization with NumPy arrays.")
