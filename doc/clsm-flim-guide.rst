.. _clsm_flim_guide:

CLSM, FLIM, and Image Workflows
===============================

CLSM and FLIM workflows turn a TTTR photon stream into image objects whose
pixels still know which photons they contain. This makes it possible to compute
intensity images, mean microtime images, lifetime maps, phasor maps, decay
images, segmentation-based decays, and image correlation curves.

Core Objects
------------

``CLSMSettings``
   Describes marker channels, line/frame markers, reading routine, pixel counts,
   and bidirectional scanning behavior.

``CLSMImage``
   Stores frames, lines, pixels, photon index ranges, and derived images. It is
   the main object for intensity, mean lifetime, phasor, decay image, pixel mask,
   transform, crop, rebin, stack, and image correlation workflows.

``CLSMFrame``, ``CLSMLine``, and ``CLSMPixel``
   Hierarchical views into an image. They keep photon index ranges attached to
   frame, line, and pixel positions, which is what makes later TTTR selections
   possible.

``CLSMSuperRes``
   Super-resolution reconstructions on CLSM data: photon-level eSRRF
   reassignment onto a finer raster, and the array-detector (ISM) methods --
   shift-vector estimation, adaptive pixel reassignment and focus-ISM.

Marker Routines
---------------

``CLSM_DEFAULT``
   Use when frame/line markers are represented directly by routing channels.

``CLSM_SP5`` and ``CLSM_SP8``
   Use for Leica data where marker information can be encoded differently from
   normal photon events.

``CLSM_BH_SPC130``
   Use for Becker & Hickl SPC-130 CLSM files.

Simple Path
-----------

Create an intensity image:

.. code-block:: python

   data = tttrlib.TTTR("image.ht3", "HT3")
   image = tttrlib.CLSMImage(data, fill=True, channels=[0])
   intensity = image.intensity

Use this path to verify marker settings before running more expensive pixel-wise
analysis.

What To Document In a CLSM/FLIM Result
--------------------------------------

A complete image workflow should report enough information to rebuild the image:

Input stream
   File type, routing channels, marker channels, and event-type assumptions.

Image geometry
   Number of frames, lines, pixels per line, bidirectional scanning, and whether
   incomplete frames were kept or removed.

Photon assignment
   Whether ``fill=True`` was used, which channels are included, and whether the
   result is frame-resolved or stacked.

Microtime calibration
   TCSPC bin width, excitation period, microtime gate, and any binning factor.

Lifetime model
   Moment, phasor, MLE, or decay-fit settings, including IRF and background
   handling when fitted lifetimes are shown.

Analysis Paths
--------------

Mean microtime and mean lifetime
   Use ``CLSMImage.get_mean_micro_time_image`` or ``CLSMImage.get_mean_lifetime``
   when moments are sufficient and a full parametric fit is unnecessary.

Phasor
   Use ``CLSMImage.get_phasor`` for fast visual separation of lifetime
   populations and IRF-corrected phasor maps.

Fluorescence decay images
   Use ``CLSMImage.get_fluorescence_decay`` or pixel-mask decay extraction when
   a fitted model or segmentation-based decay is needed.

MLE lifetime fitting
   Use ``Fit23`` on pixel decay histograms when the photon counts are low but a
   parametric lifetime estimate is required.

Image correlation spectroscopy
   Use ``CLSMImage.compute_ics`` and the image-correlation examples for spatial
   or spatiotemporal correlation of image stacks.

Pixel-Level Decay Workflow
--------------------------

Use pixel-level decays when a map needs a model-based lifetime:

.. code-block:: python

   decay = image.get_fluorescence_decay(
       frame=0,
       line=42,
       pixel=24,
       micro_time_coarsening=4,
   )
   result = fit23(decay, irf=irf, background=background, dt=dt, period=period)

Document the minimum photon threshold used to include a pixel. Low-count pixels
should be masked before producing maps that compare fitted parameters.

Phasor Workflow
---------------

Phasor maps are useful before parametric fitting because they quickly reveal
mixed populations and pixels with poor signal:

.. code-block:: python

   g, s = image.get_phasor(
       micro_time_coarsening=4,
       first_valid_micro_time=0,
       last_valid_micro_time=1024,
   )

Keep the harmonic, microtime window, and IRF correction status with the plot.

Safe Transformations
--------------------

Cropping and rebinning preserve useful image summaries, but any operation that
changes photon ordering can invalidate temporal workflows such as FCS, STICS, or
pixel-level time correlation. Keep the original ``TTTR`` stream and record the
transform chain when exporting transformed images.

Required Checks
---------------

Every CLSM/FLIM example should state:

* file type and marker routine,
* frame/line/pixel dimensions,
* selected routing channels,
* whether pixels are filled,
* whether frames are stacked,
* microtime binning factor,
* whether output arrays are frame-resolved or stacked.

Troubleshooting
---------------

Empty image
   Check marker channels, event type handling, reading routine, and whether
   ``fill=True`` was used.

Incorrect dimensions
   Verify the number of pixels per line and whether bidirectional scanning or
   incomplete frames need handling.

Unexpected lifetime map
   Check IRF, background, microtime binning, and minimum photon threshold.

Examples
--------

* :doc:`auto_examples/flim/plot_read_clsm_data`
* :doc:`auto_examples/flim/plot_marker`
* :doc:`auto_examples/flim/plot_intensity_image`
* :doc:`auto_examples/flim/plot_imaging_representations`
* :doc:`auto_examples/flim/plot_mean_lifetime`
* :doc:`auto_examples/flim/plot_lifetime_moments`
* :doc:`auto_examples/flim/plot_phasor`
* :doc:`auto_examples/flim/plot_mle_lifetime`
* :doc:`auto_examples/flim/plot_segmentation_based_decays`
* :doc:`auto_examples/image_correlation/index`
