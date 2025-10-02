#!/usr/bin/env python3
"""
NumPy-friendly wrapper for tttrlib Image Localization

This module provides a clean NumPy interface for the tttrlib localization functionality,
making it easy to use NumPy arrays directly without manual conversion to SWIG vector types.
"""

import numpy as np

from tttrlib import GaussianFitResult, ImageLocalizer


class LocalizationNumPy(ImageLocalizer):
    """Backwards compatible NumPy focussed localisation helper."""

    def fit2DGaussian(self, parameters, image_data, **kwargs):
        """Fit a 2-D Gaussian returning the raw status code.

        This method mirrors the original helper while delegating all heavy
        lifting to :class:`tttrlib.ImageLocalizer`. The ``parameters`` array is
        updated in-place and the status code from the underlying optimiser is
        returned for convenience.
        """

        result = super().fit(image_data, initial=parameters, **kwargs)
        self.last_result: GaussianFitResult = result
        return result.status

    def model2DGaussian(self, parameters, rows=None, cols=None, *, shape=None):
        """Generate a model image for the provided parameters."""

        if shape is None:
            if rows is None or cols is None:
                raise ValueError("Either `shape` or `rows` and `cols` must be provided")
            shape = (int(rows), int(cols))
        return super().model_image(parameters, shape)

    def fit_and_model(self, parameters, image_data, **kwargs):
        """Fit a Gaussian and return both the status code and model image."""

        params = np.array(parameters, copy=True, dtype=float)
        status = self.fit2DGaussian(params, image_data, **kwargs)
        model = self.model2DGaussian(params, shape=image_data.shape)
        return status, params, model


def fit_gaussian_numpy(parameters, image_data, **kwargs):
    """Convenience wrapper delegating to :class:`LocalizationNumPy`."""

    loc = LocalizationNumPy()
    return loc.fit2DGaussian(parameters, image_data, **kwargs)


def model_gaussian_numpy(parameters, rows=None, cols=None, *, shape=None):
    """Generate a Gaussian model via :class:`LocalizationNumPy`."""

    loc = LocalizationNumPy()
    return loc.model2DGaussian(parameters, rows=rows, cols=cols, shape=shape)

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
