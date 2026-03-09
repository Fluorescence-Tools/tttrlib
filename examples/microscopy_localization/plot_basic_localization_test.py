"""
=================================
Basic Image Localization Test
=================================

This example demonstrates the basic functionality of the Image Localization
features in tttrlib by fitting a synthetic 2D Gaussian.

The script creates synthetic data with known parameters and verifies that
the localization algorithm can recover the original parameters with high accuracy.
This serves as a validation test for the localization functionality.

"""

import numpy as np
import tttrlib

#%%
# Create synthetic 2D Gaussian data
# ---------------------------------
# We start by creating a synthetic 2D Gaussian with known parameters
# to test the localization algorithm's accuracy.

# Create synthetic 2D Gaussian data for testing
size = 21
center = size // 2
x, y = np.meshgrid(np.arange(size), np.arange(size))

# Synthetic Gaussian parameters
x0, y0 = center, center
amplitude = 1000.0
sigma = 2.5
background = 50.0

print(f"Creating synthetic {size}x{size} Gaussian data")
print(f"True parameters: center=({x0}, {y0}), sigma={sigma}, A={amplitude}")

#%%
# Generate synthetic 2D Gaussian with noise
gaussian = amplitude * np.exp(-((x - x0)**2 + (y - y0)**2) / (2 * sigma**2)) + background

# Add some Poisson noise to make it realistic
np.random.seed(42)
gaussian += np.random.poisson(gaussian * 0.1)

# Prepare NumPy array for fitting (float64, C-contiguous)
gaussian = np.ascontiguousarray(gaussian, dtype=np.float64)

print(f"Generated synthetic data with noise")
print(f"Data range: {np.min(gaussian):.1f} - {np.max(gaussian):.1f}")

#%%
# Convert to SWIG vector format
# -----------------------------
# The tttrlib localization functions require data in SWIG vector format.
# We convert the NumPy array to the required VectorDouble_2D format.

# Test if localization functionality is available
localization = tttrlib.new_localization()
print("Localization object created successfully")

# Convert image data to SWIG vector format
data_2d = tttrlib.new_VectorDouble_2D()
for row in gaussian:
    row_vector = tttrlib.new_VectorDouble()
    for val in row:
        tttrlib.VectorDouble_push_back(row_vector, float(val))
    tttrlib.VectorDouble_2D_push_back(data_2d, row_vector)

print("Data converted to SWIG vector format")

#%%
# Set up fitting parameters
# -------------------------
# The localization algorithm requires an 18-element parameter array
# with initial guesses and fitting options.

# Set up fitting parameters (18-element array)
vars = [0.0] * 18
vars[0] = center + 0.5  # x0 initial guess
vars[1] = center - 0.3  # y0 initial guess  
vars[2] = amplitude * 0.9  # amplitude guess
vars[3] = sigma * 1.1  # sigma guess
vars[4] = 1.0  # ellipticity (1.0 = circular)
vars[5] = background * 1.2  # background guess
vars[14] = 0  # fit background
vars[15] = 1  # circular Gaussian
vars[16] = 0  # single Gaussian model

print("Initial parameter guesses set")

#%%
# Perform 2D Gaussian fitting
# ---------------------------
# Now we perform the actual 2D Gaussian fitting using the tttrlib
# localization algorithm.

# Perform fitting using SWIG vector interface
result = tttrlib.localization_fit2DGaussian(vars, data_2d)

if result > 0:  # Successful fit
    fitted_x = vars[0]
    fitted_y = vars[1]
    fitted_amplitude = vars[2]
    fitted_sigma = vars[3]
    fitted_bg = vars[5]
    goodness_of_fit = vars[17]

    print("Gaussian fitting successful!")
    print("Fitted parameters:")
    print(f"  Center: ({fitted_x:.2f}, {fitted_y:.2f}) [True: ({x0}, {y0})]")
    print(f"  Sigma: {fitted_sigma:.2f} [True: {sigma}]")
    print(f"  Amplitude: {fitted_amplitude:.1f} [True: {amplitude}]")
    print(f"  Background: {fitted_bg:.1f} [True: {background}]")
    print(f"  Goodness of fit: {goodness_of_fit:.3f}")

    #%%
    # Generate model image
    # --------------------
    # We can generate a model image using the fitted parameters
    # to compare with the original data.
    
    # Generate model image using array interface
    try:
        model = tttrlib.localization_model2DGaussian_array(vars, size, size)
        if model is not None:
            print("Model image successfully generated")
    except Exception as model_error:
        print(f"Model generation failed: {model_error}")

    #%%
    # Calculate accuracy metrics
    # --------------------------
    # Finally, we calculate accuracy metrics to assess the quality
    # of the localization.
    
    # Check accuracy
    pos_error = np.sqrt((fitted_x - x0)**2 + (fitted_y - y0)**2)
    sigma_error = abs(fitted_sigma - sigma) / sigma * 100
    
    print("Accuracy metrics:")
    print(f"  Position error: {pos_error:.3f} pixels")
    print(f"  Sigma error: {sigma_error:.1f}%")
    
    if pos_error < 1.0 and sigma_error < 20:
        print("Fitting accuracy is good!")
    else:
        print("Fitting accuracy could be improved")
        
else:
    print("Gaussian fitting failed")

print("\nImage Localization test completed!")
