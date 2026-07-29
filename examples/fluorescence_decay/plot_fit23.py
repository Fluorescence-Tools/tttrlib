r"""
==================================
Polarization resolved lifetime MLE
==================================

fit23
-----

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
The fit implements a maximum likelihood estimator as previously described :cite:`maus2001experimental`.
Briefly, the MLE fit quality parameter 2I* = :math:`-2\ln L(n,g)` (where :math:`L`
is the likelihood function, :math:`n` are the experimental counts, and :math:`g`
is the model function) is minimized. The model function :math:`g` under magic-angle
is given by:

.. math::

   g_i = N_g \left[
       (1-\gamma)
       \frac{irf_i \ast \exp(iT/k\tau) + c}
            {\sum_{i=0}^{k}irf_i \ast \exp(iT/k\tau) + c}
       + \gamma \frac{bg_i}{\sum_i^{k} bg_i}
   \right]

:math:`N_e` is not a fitting parameter but set to the experimental number of
photons :math:`N`, :math:`\ast` is the convolution operation, :math:`\tau` is the
fluorescence lifetime, :math:`irf` is the instrument response function, :math:`i`
is the channel number, :math:`bg_i` is the background count in the channel :math:`i`,

The convolution by fit23 is computed recursively and accounts for high repetition
rates:

.. math::

   irf_i \ast \exp(iT/k\tau) =
       \sum_{j=1}^{min(i,l)} irf_j \exp(-(i-j)T/k\tau)
       + \sum_{j=i+1}^{k} irf_j \exp(-(i+k-j)T/k\tau)

The anisotropy treated as previously described :cite:`schaffer_identification_1999`.
The correction factors needed for a correct anisotropy used by ``fit2x`` are
defined in the glossary (:term:`Anisotropy`).


"""
import numpy as np
import pylab as p

import tttrlib

#%%
# First, we define our instrument response function (IRF) and the data array. Both,
# the IRF and the data are stacked arrays that contain the fluorescence decay histograms
# of the parallel and perpendicular detection channels.

irf = np.array(
    [0, 0, 0, 260, 1582, 155, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 22, 1074, 830, 10, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0], dtype=np.float64
)
data = np.array(
    [0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
     1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
)


#%%
# Second, a set of constant parameters needs to be specified that define instrumental
# parameters. To analyze the decay the time-resolution of the histogram bins (dt),
# the g factor that corrects for the sensitivity of the parallel and perpendicular
# detection channel to detect light, two constants l1 and l2 that quantify the mixing
# of the parallel and perpendicular detection channel, and the excitation period need
# to be specified. The excitation periord (in nanoseconds) accounts for periodic
# excitation (high repetition rates). Moreover the convolution stop channel that
# defines the convolution range is needed. The parameter ``background`` is an array
# that defines a non-constant offset that gets scaled by :math:`\gamma` (the fraction
# of scattered light).

dt = 0.5079365079365079
setup = tttrlib.setup_vector(
    'fit23', dt=dt, period=16.0, g_factor=1.0, l1=0.1, l2=0.2,
    convolution_stop=31)

#%%
# The setup and the IRF build the model once; it is immutable, so the same
# instance fits any number of decays. The measurement itself lives in a
# ``DecayFitProblem``.

fit23 = tttrlib.DecayFit2('fit23', setup, irf.tolist())

problem = tttrlib.DecayFitProblem(2, len(irf) // 2, dt)
problem.irf = tttrlib.VectorDouble(irf.tolist())
problem.background = tttrlib.VectorDouble(np.zeros_like(irf).tolist())
problem.data = tttrlib.VectorDouble(np.asarray(data, dtype=float).tolist())

#%%
# The link vector says what the optimiser may move: ``0`` free, ``-1`` held.
# Here the lifetime and the rotational correlation time are fitted while the
# scatter fraction and the fundamental anisotropy are held.

tau, gamma, r0, rho = 2.2, 0.01, 0.38, 1.22
x0 = [tau, gamma, r0, rho]
constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, 0]))
outcome = fit23.fit(x0, constraints, problem)

#%%
# The outcome carries the fitted ``parameters``, the ``objective`` (2I*), and a
# vector of ``results`` whose columns the registry names. The fitted curve is
# left on ``problem.model``.

r = {
    'x': np.asarray(outcome.parameters),
    'twoIstar': outcome.objective,
    **tttrlib.results_as_dict('fit23', list(outcome.results)),
}


p.plot(np.asarray(problem.data), label='data')
p.plot(np.asarray(problem.model), label='model')
p.show()

print("Results")
print("=======")
print("tau: {:.2f}".format(r['x'][0]))
print("rho: {:.2f}".format(r['x'][3]))
