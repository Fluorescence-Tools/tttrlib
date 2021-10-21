"""


fit23
^^^^^
This example illustrates how to recover a fluorescence lifetime (tau) and a
rotational correlation time (rho)  using ``fit23`` from polarization resolved
fluorescence decay histograms. ``fit23`` makes use of a maximum likelihood estimator
(MLE) and thus can be applied to low photon count data as frequently encountered
in time-resolved single-molecule spectroscopy or fluorescence lifetime imaging (FLIM).

``fit23`` optimizes a single rotational correlation time :math:`\rho` and a
fluorescence lifetime :math:`\tau` to a polarization resolved fluorescence decay
considering the fraction of scattered light and instrument response function in
the two detection channels for the parallel and perpendicular fluorescence. Fit23
operates on fluorescence decays in the :term:`Jordi-format`.

``fit23`` is intended to be used for data with very few photons, e.g. for pixel analysis
in fluorescence lifetime image microscopy (FLIM) or for single-molecule spectroscopy.
The fit implements a maximum likelihood estimimator as previously described :cite:`maus_experimental_2001`.
Briefly, the MLE fit quality parameter 2I* = :math:`-2\ln L(n,g)` (where :math:`L`
is the likelihood function, :math:`n` are the experimental counts, and :math:`g`
is the model function) is minimized. The model function :math:`g` under magic-angle
is given by:

:math:`g_i=N_g \left[ (1-\gamma) \frac{irf_i \ast \exp(iT/k\tau) + c}{\sum_{i=0}^{k}irf_i \ast \exp(iT/k\tau) + c} + \gamma \frac{bg_i}{\sum_i^{k} bg_i} \right]`

:math:`N_e` is not a fitting parameter but set to the experimental number of
photons :math:`N`, :math:`\ast` is the convolution operation, :math:`\tau` is the
fluorescence lifetime, :math:`irf` is the instrument response function, :math:`i`
is the channel number, :math:`bg_i` is the background count in the channel :math:`i`,

The convolution by fit23 is computed recursively and accounts for high repetition
rates:

:math:`irf_i \ast \exp(iT/k\tau) = \sum_{j=1}^{min(i,l)}irf_j\exp(-(i-j)T/k\tau) + \sum_{j=i+1}^{k}irf_j\exp(-(i+k-j)T/k\tau) `

The anisotropy treated as previously described :cite:`schaffer_identification_1999`.
The correction factors needed for a correct anisotropy used by ``fit2x`` are
defined in the glossary (:term:`Anisotropy`).


"""