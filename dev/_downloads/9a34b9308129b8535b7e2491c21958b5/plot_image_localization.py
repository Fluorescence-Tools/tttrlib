"""
2D Gaussian image localization
==============================

This example localizes a bright point source in a 2D image using
``tttrlib.ImageLocalizer``. The image below is a small synthetic placeholder;
replace ``image`` with a camera, CLSM, or other microscopy image when applying
the workflow to real data.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib


# %%
# Create a placeholder image
# --------------------------
# The localizer expects a 2D NumPy array. In real use, this can be a cropped
# camera frame, a CLSM intensity image, or any other image-like array with one
# isolated point source.

rng = np.random.default_rng(42)
rows, cols = 96, 96
y, x = np.indices((rows, cols), dtype=float)

true_x = 53.4
true_y = 41.8
true_sigma = 2.6
true_amplitude = 900.0
background = 18.0

image = background + true_amplitude * np.exp(
    -((x - true_x) ** 2 + (y - true_y) ** 2) / (2.0 * true_sigma**2)
)
image = rng.poisson(image).astype(np.float64)


# %%
# Select a region of interest
# ---------------------------
# Fitting a local ROI is usually more stable than fitting the full image. The
# ROI is given as ``(rows, columns)`` slices. The fitted center can be reported
# both in ROI coordinates and in global image coordinates.

roi = (slice(32, 54), slice(42, 66))
roi_image = image[roi]


# %%
# Fit the point source
# --------------------
# ``ImageLocalizer.fit`` returns a ``GaussianFitResult``. The result keeps the
# fitted parameter vector, the ROI offset, and optionally the fitted model image.

with tttrlib.ImageLocalizer(fit_background=True, allow_elliptical=False) as localizer:
    result = localizer.fit(
        image,
        roi=roi,
        guess={
            "sigma": 2.5,
            "background": float(np.median(roi_image)),
        },
        return_model=True,
    )

fitted_x, fitted_y = result.to_global()
residual = roi_image - result.model
residual_rms = float(np.sqrt(np.mean(residual**2)))

print(f"Fit status: {result.status}")
print(f"Success: {result.success}")
print(f"Fitted center: x={fitted_x:.2f}, y={fitted_y:.2f}")
print(f"Fitted sigma: {result.sigma:.2f} px")
print(f"Fitted amplitude: {result.amplitude:.1f}")
print(f"Fitted background: {result.background:.1f}")
print(f"Residual RMS: {residual_rms:.2f} counts")


# %%
# Inspect the fit
# ---------------
# The result model is in ROI coordinates, so it can be compared directly with
# the cropped ROI.

fig, axes = plt.subplots(1, 3, figsize=(10, 3.4), constrained_layout=True)

vmin = float(np.percentile(roi_image, 2))
vmax = float(np.percentile(roi_image, 99.5))

axes[0].imshow(roi_image, origin="lower", cmap="magma", vmin=vmin, vmax=vmax)
axes[0].plot(result.center[0], result.center[1], "c+", markersize=12, mew=2)
axes[0].set_title("ROI")

axes[1].imshow(result.model, origin="lower", cmap="magma", vmin=vmin, vmax=vmax)
axes[1].set_title("Gaussian model")

axes[2].imshow(residual, origin="lower", cmap="coolwarm")
axes[2].set_title("Residual")

for ax in axes:
    ax.set_xlabel("x / px")
    ax.set_ylabel("y / px")

plt.show()
