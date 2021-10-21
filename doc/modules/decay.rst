.. _lifetime_analysis:

===========================
Fluorescence decay analysis
===========================

Fluorescence decays
===================

Phasor
======
Changing the data representation from the classical time delay
histogram to the phasor representation provides a global view
of the fluorescence decay at each pixel of an image. In the phasor
representation reveal the presence of different in a a fluorescence
decay. The analysis of time-resolve fluorescence data in the phasor space is done
by observing clustering of populations in the phasor plot rather
than by fitting the fluorescence decay using exponentials. The
analysis is instantaneous since is not based on calculations or nonlinear
fitting. Thus, the phasor approach can simplify the way data
are analyzed and makes FLIM technique accessible to the nonexpert
in spectroscopy and data analysis :cite:`DIGMAN2008L14`.

The phasor transformation can use data collected in either the time or the
frequency domain. In the time-correlated single-photon counting (TCSPC)
technique, the histogram of the photon arrival time at each pixel of the image
is transformed to Fourier space, and the phasor coordinates are calculated
using the following relations:

.. math::

    g_{i,j}(\omega) = \int_{0}^{T} I(t) \cdot \cos(n\omega t) dt / \int_{0}^{T} I(t) dt

    s_{i,j}(\omega) = \int_{0}^{T} I(t) \cdot \sin(n\omega t) dt / \int_{0}^{T} I(t) dt

in which gi,j(ω) and si,j(ω) are the x and y coordinates of the phasor plot,
n and ω are the harmonic frequency and the angular frequency of excitation,
respectively, and T is the repeat frequency of the acquisition :cite:`ranjit2018fit`.


Fluorescence lifetime analysis
==============================


Maximum likelihood fits
=======================

tttrlib provides a set of precompile models with fits (e.g. fit23, fit24,
and fit25) that have their own model function, target (objective) function,
fit function, and parameter correction function. Briefly, the purpose of these
functions is as follows.

+--------------------------+--------------------------------------------+
| Function                 | Description                                |
+==========================+============================================+
| Model function           | Computes the model for a set of parameters |
+--------------------------+--------------------------------------------+
| Target/objective function| Computes ob and returns a score for data   |
+--------------------------+--------------------------------------------+
| Fit function             | Optimizes the parameters to the data       |
+--------------------------+--------------------------------------------+
| Correction function      | Assures reasonable range of parameters     |
+--------------------------+--------------------------------------------+

The model function computes for a set of input parameters the corresponding
realization of the model (the model fluorescence decay curve(s)). The target
function (more often called objective function) computes the disagreement of the
data and the model function for a given set of model parameters. The fitting
function optimizes a set of selected input parameters to the data by minimizing
the disagreement of the data and the model. Finally, the estimates of the model
parameters are corrected by a correction function. These functions have names
that relate to the name of the model, e.g., `DecayFit23.target` corresponds to `fit23`.
For computationally efficiency, the functions are hardcoded in C.

The models of the different fits differ in their parameters. The parameters of
the fits are described in detail below. The target functions explicitly consider
the counting noise and provide an estimate of the likelihood of the parameters.
The initial values of the parameters are specified by the user. Next, the likelihood
of the target is maximized by the Limited-Memory Broyden-Fletcher-Goldfarb-Shanno
Algorithm (L-BFGS). L-BFGS is an iterative method for solving unconstrained nonlinear
optimization problems. Gradients for the L-BFGS algorithm are approximated by linear
interpolation to handle the differentiable components, i.e., the model parameters
that are free. The parameters of a model ca be either specified as free or as fixed.
Only free parameters are optimized.

Python
^^^^^^
For a use in python the ``tttrlib`` module exposes a set of C functions that can be
used to (1) compute model and (2) target/objective function and (3) optimize
model parameters to experimental data. Besides the exposed functions the fit models
are accessible via a simplified object-based interface that reduces the number of
lines of code that need to be written for analyzing fluorescence decay histograms.
The code blocks that are used below to illustrate the tttrlib functionality are
extracts from the tests located in the test folder of the tttrlib repository. The
test can be used as a more detailed refernce on how to use tttrlib.

Model functions can be computed using for instance the ``DecayFit23.modelf``
function of the ``tttrlib`` module. Here, the ``23`` represents a particular model
function. The use of a ``tttrlib`` model function is for the model function
23 (``DecayFit23.model``) below.

.. literalinclude:: ../examples/fluorescence_decay/plot_fit23_usage_1.py
   :language: python
   :lines: 36-44
   :linenos:

To compute a model, first a set of model parameters and a set of corrections need
to be specified. All input parameters for ``DecayFit23.model`` are numpy arrays.
In addition to the model parameters and the corrections ``DecayFit.23.model``
requires an instrument response function (irf) and a background pattern. The model
functions operate on numpy arrays and modify the numpy array for a model in-place.
This means, that the output of the function is written to the input numpy-array of the model. In the
example above the output is written to the array ``model``.


To compute the value of a target for a realization of model parameters ``DecayFit23``
provides the functions ``DecayFit23.target``. The inputs of a target function (here
``DecayFit23.target``) are an numpy array containing the model parameter values and a
structure that contains the corrections and all other necessary data to compute
the value of the objective function (for fit23 i.e. data, irf, background, time
resolution).

.. literalinclude:: ../examples/fluorescence_decay/plot_fit23_usage_2.py
   :language: python
   :lines: 171-179
   :linenos:

The data needed in addition to the model parameters are passed to the target function
using ``fit2x.MParam`` objects that can be created by the ``fit2x.CreateMParam``
function from numpy arrays. The return value of a target function is the score
of the model parameters

Model parameters can be optimized to the data by fit functions for fit 23 the
fit function ``fit2x.DecayFit23.fit`` is used.

.. literalinclude:: ../examples/fluorescence_decay/plot_fit23_usage_2.py
   :language: python
   :lines: 250-253
   :linenos:

The fit functions takes like the target function an object of the type
``fit2x.MParam`` in addition to the initial values, and a list of fixed model
parameters as an input. The array containing the initial values of the model
parameters are modified in-place buy the fit function.

Alternatively, a simplified python interface can be used to optimize a set of
model as illustrated by the source code embedded in the plot below. The simplified
interface handles the creation of auxiliary data structures such as ``tttrlib.MParam``.

.. plot:: ../examples/fluorescence_decay/plot_fit23_mini_example.py

    Analysis result of fit23 by the simplified python interface provided
    by ``tttrlib``

In the example shown above, first a fit object of the type ``fit2x.DecayFit23`` is
created. All necessary data except for the experimental data for a fit is passed
to the fit object when it is created. To perform a fit on experimental data for
a set for a set of initial values, the fit object is called using the inital values
and the data as parameters.


