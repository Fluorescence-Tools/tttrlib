.. _localization_guide:

Image Localization
==================

Image localization fits compact bright objects in two-dimensional images. In
tttrlib the public Python entry point is ``ImageLocalizer``. It wraps the
low-level ``localization`` Gaussian fitter and returns a ``GaussianFitResult``
with fitted parameters, the ROI offset, and an optional fitted model image.

Use this workflow when you already have a small 2D image or a cropped
``CLSMImage`` intensity plane and need the center position of an isolated bead,
spot, or emitter.

Basic Workflow
--------------

1. Convert the source image to a 2D ``numpy.ndarray``.
2. Crop a region of interest around one isolated point source.
3. Fit with ``ImageLocalizer.fit`` and request ``return_model=True`` when you
   want to inspect residuals.
4. Use ``GaussianFitResult.to_global`` to convert the fitted ROI-local center
   back to image coordinates.

The gallery example uses a synthetic placeholder image so it can run without
external data. Replace the ``image`` array with a real camera, CLSM, or other
microscopy image when applying the workflow:

* :doc:`auto_examples/microscopy_localization/plot_image_localization`

Practical Checks
----------------

Before using fitted positions downstream:

* inspect the ROI, model, and residual image;
* keep one point source per ROI;
* start with a reasonable ``sigma`` and ``background`` guess when the image is
  noisy;
* verify the reported center is inside the ROI and matches the visible spot.

API Surface
-----------

``ImageLocalizer``
   Context-managed helper for fitting a 2D Gaussian model to NumPy arrays or
   compatible image objects.

``GaussianFitResult``
   Result container exposing ``center``, ``to_global()``, ``sigma``,
   ``amplitude``, ``background``, and the optional fitted ``model`` image.
