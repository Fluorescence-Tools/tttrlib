.. _superres-guide:

Super-resolution: eSRRF, ISM, SOFISM and s²ISM
==============================================

:class:`tttrlib.CLSMSuperRes` collects the super-resolution reconstructions for
confocal laser-scanning data. They fall into two families that sharpen an image
for quite different reasons, and the distinction decides which one you want.

.. list-table::
   :header-rows: 1
   :widths: 16 30 30 24

   * - Method
     - What it exploits
     - What it needs
     - Linear?
   * - **eSRRF**
     - the *image*: local intensity gradients converge where an emitter is
     - nothing beyond the image
     - no — can create structure
   * - **ISM (APR)**
     - the *detector*: each array element sees a displaced PSF
     - an array detector
     - yes — photon-conserving
   * - **SOFISM**
     - the *emitters*: independent blinking
     - an array detector **and** fluctuations
     - no — a cumulant
   * - **s²ISM**
     - a *PSF model*: joint inversion over axial planes
     - an array detector and a PSF
     - no — iterative ML

References
----------

* Fourier ring correlation — ported from BrightEyes-ISM ``FRC_lib``;
  see ``CLSMSuperRes.frc_curve`` and ``CLSMSuperRes.frc_resolution``.
* eSRRF — Laine *et al.*, *Nat. Methods* **20**, 1949 (2023),
  `doi:10.1038/s41592-023-02057-w <https://doi.org/10.1038/s41592-023-02057-w>`_.
  Ported from NanoJ-eSRRF.
* ISM / adaptive pixel reassignment — Tortarolo *et al.*,
  *Nat. Commun.* **13**, 7929 (2022),
  `doi:10.1038/s41467-022-35333-y <https://doi.org/10.1038/s41467-022-35333-y>`_.
  Ported from BrightEyes-ISM ``APR_lib``.
* Focus-ISM — same reference; ported from BrightEyes-ISM ``FocusISM_lib``.
* SOFISM — Sroda *et al.*, *Optica* **7**, 1308 (2020),
  `doi:10.1364/OPTICA.399600 <https://doi.org/10.1364/OPTICA.399600>`_.
* s²ISM — Zunino *et al.*, *Nat. Photonics* (2025),
  `doi:10.1038/s41566-025-01695-0 <https://doi.org/10.1038/s41566-025-01695-0>`_.
  Ported from the ``s2ism`` reference implementation.

Photon-level eSRRF
------------------

eSRRF computes a Radial Gradient Convergence (RGC) field, which peaks where the
local intensity gradients converge — that is, on an emitter — and renders it on
a grid ``magnification`` times finer::

    rgc = tttrlib.CLSMSuperRes.rgc_map(
        image, magnification=4, fwhm=2.5, sensitivity=1, intensity_weighting=True)

``fwhm`` is the PSF width in native pixels and ``sensitivity`` the exponent
applied to the normalized field; raising it sharpens and sparsifies.

Because tttrlib works on single photons rather than camera frames, eSRRF is also
available as a *reassignment*: each photon is redistributed onto the finer
raster by sampling the RGC field as a spatial prior, and the result is a TTTR
stream that can be written back to a file::

    reassigned = tttrlib.CLSMSuperRes.reassign_photons(
        clsm, tttr, magnification=2, fwhm=1.0, sensitivity=2, search_radius=1.0)
    tttrlib.CLSMSuperRes.write(reassigned, nx, ny, magnification, "out.ptu")

Micro times and routing channels are preserved, so lifetime information survives
the reassignment. The macro time is synthetic — it encodes the position in the
magnified raster — so correlation on the raster timescale is not meaningful in
the output.

.. warning::

   eSRRF is nonlinear. On a target of shrinking line pairs its modulation is
   **not monotonic**: it can recover contrast at separations where the pairs
   have already merged, by splitting a merged blur into an artificial doublet.
   Judge a resolution claim only where every wider feature is also resolved.
   See :ref:`sphx_glr_auto_examples_ism_plot_ism_esrrf_comparison.py`.

Array-detector reconstructions
------------------------------

All three take a detector cube of shape ``(n_det, ny, nx)``.

**Shift vectors.** Every element images the object through a PSF displaced by
half the element's own offset. The shift that registers element *k* onto a
reference element is estimated by phase cross-correlation on Hann-apodized,
optionally denoised images, refined to ``1/usf`` of a pixel::

    shifts = tttrlib.CLSMSuperRes.shift_vectors(cube, usf=10, filter_sigma=1.0)

``shifts`` is ``(n_det, 2)`` as ``(dy, dx)``, row axis first.

**Adaptive pixel reassignment.** Registering every element with its shift and
summing gives a sharper image at no cost in photons::

    apr = tttrlib.CLSMSuperRes.apr_reconstruction(cube, usf=10)[0]

**Focus-ISM.** Each pixel's *micro-image* — its distribution over the detector
array — is fitted with two Gaussians sharing the array centre: a narrow
in-focus one whose width is calibrated from a central patch, and a wider free
one for the background::

    focus, background, ism = tttrlib.CLSMSuperRes.focus_reconstruction(
        cube, sigma_bound=2.0, threshold=0.0, calibration_size=16)

The calibration patch must contain in-focus structure, since it sets the
in-focus width everything else is judged against.

SOFISM
------

SOFISM multiplies the ISM and SOFI mechanisms. At every scan position the array
records a short time series; for each *pair* of elements the temporal
cross-correlation of the fluctuations is formed,

.. math::

   C_{ij}(\mathbf{r}, \tau) = \frac{1}{N_t - \tau}
       \sum_t \delta I_i(\mathbf{r}, t)\, \delta I_j(\mathbf{r}, t + \tau)

whose effective PSF is the *product* of the two elements' PSFs. The pair acts as
one virtual detector midway between them, so it is reassigned by
:math:`\vec{v}_{ij} = (\vec{v}_i + \vec{v}_j)/2`::

    sofism = tttrlib.CLSMSuperRes.sofism_reconstruction(cube_4d, lag=0, usf=10)

The input is ``(n_time, n_det, ny, nx)``. Autocorrelation terms are excluded by
default: their shot noise does not cancel and would ride into the result as a
bias.

.. important::

   SOFISM contrast comes from emitters blinking **independently**, and the
   fluctuations must be resolved *within a pixel dwell*. Blinking slower than
   the dwell leaves nothing to correlate, and a static sample yields no signal
   at all — a fact worth using as a control on real data.

The optional Fourier reweighting of the paper multiplies the spectrum by
:math:`W(k) = 1/(\mathrm{OTF}^2(k) + \varepsilon)`::

    sharper = tttrlib.CLSMSuperRes.fourier_reweight(sofism, otf, epsilon=1e-3)

s²ISM
-----

s²ISM is not a reassignment. It treats the array as :math:`N_{ch}` images of one
object seen through :math:`N_{ch}` different PSFs and inverts them jointly by
multi-image Richardson–Lucy over a stack of axial planes:

.. math::

   \hat{I}_{ch} = \sum_z O_z * h_{z,ch}, \qquad
   O_z \leftarrow O_z \cdot \sum_{ch} \frac{I_{ch}}{\hat{I}_{ch}} \star h_{z,ch}

::

    obj = tttrlib.CLSMSuperRes.s2ism_reconstruction(cube, psf, max_iter=60)
    focal_plane = obj[obj.shape[0] // 2]

The *sectioning* comes from the axial stack: out-of-focus haze is explained by
the defocused planes instead of being smeared into the focal one, which a
single-plane deconvolution cannot do because it has nowhere else to put it.
Unlike APR this needs a PSF *model* — ``psf`` is ``(nz, n_det, ny, nx)``,
centred in the frame — and it does not derive one from the data.

``auto_stop=True`` halts once the focal-plane photon count settles, which
matters because that count is **not** monotonic in the iteration number: late
iterations move flux out to the defocused planes.

Choosing a method
-----------------

* No array detector? Only eSRRF applies.
* Want a gain you can defend without qualification? **APR** — linear and
  photon-conserving, but bounded near :math:`\sqrt{2}`.
* Sample blinks and you can time-resolve it within the dwell? **SOFISM** — it
  reaches roughly :math:`2\times` and, unlike eSRRF, from a physical mechanism.
* Out-of-focus background is the problem, and you have a PSF model?
  **s²ISM** for sectioning, or **focus-ISM** for a cheaper two-component split.
* eSRRF composes with APR: running it on the reassigned image starts from a
  genuinely sharper input.

.. note::

   Running eSRRF on an APR image is sound where the background is not
   photon-starved, but the non-negativity clip after registration rectifies the
   interpolation's negative lobes. In dim regions that leaves a positively
   biased, spatially *correlated* background — which is exactly what a
   gradient-convergence detector turns into spurious puncta. On sparse
   structure over a dark background, prefer SOFISM or plain APR.

Worked examples
---------------

* :ref:`sphx_glr_auto_examples_ism_plot_ism_esrrf_comparison.py` — all methods on
  one resolution target, with measured modulation depth
* :ref:`sphx_glr_auto_examples_ism_plot_sofism_simulation.py` — SOFISM on an
  acquisition from tttrlib's own photon simulator, with a static control
* :ref:`sphx_glr_auto_examples_ism_plot_s2ism_sectioning.py` — sectioning a
  sharp object out of an out-of-focus halo
* :ref:`sphx_glr_auto_examples_ism_plot_tubulin_ism_simulation.py` — a tubulin
  phantom through a simulated SPAD array
* :ref:`sphx_glr_auto_examples_single_molecule_plot_clsm_superres_limitations.py`
  — where eSRRF misleads

The notebook :doc:`modules/clsm_superres` walks the whole pipeline end to end.
