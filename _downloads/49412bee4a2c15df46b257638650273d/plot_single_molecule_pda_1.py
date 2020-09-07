"""
================================
Photon distribution analysis - 1
================================

Photon distribution analysis
============================
Theory
------
The library tttrlib provides the option to compute models for single molecule
histograns using Photon Distribution Analysis (PDA) for scoring of realizations
of models by experimental single molecule histograms :cite:`kalinin2007`.

#.. plot:: ../examples/single_molecule/single_molecule_pda_1.py

The ``tttrlib.Pda`` class implements the Pda algorithm as previously described
:cite:`antonik2006`.

tttrlib.Pda objects
-------------------
To create a PDA model first create an instance of the ``tttrlib.Pda`` class. The
Pda class implements a Photon Distribution Analysis algorithm for photons that
are registed in two detection channels (channel 1 and channel 2). These two channels
can be any channel type. In a FRET experiment channel 1 (ch1) and channel 2 (ch2)
are typically the green and red detection channel, respectively. In an anisotropy
PDA experiment the two channels can be the parallel and perpendicular detection
channel. Using the theoretical probability for detecting a photon in the first
detection channel as an input (in the original manuscript this is called pG) the
number of photons in the first and the second detection channel are computed.
Hereby, the algorithm considers the background fluorescence in the two detection
channels. The measured signal consists of fluorescence (F) and background (B)
contributions (S=F+B).

To compute a PDA histogram, first, create an instance of the  ``tttrlib.Pda`` class.
When you create a new instance, you can specify a set of parameters that are of
relevance.

.. code-block:: python

    import tttrlib
    n_photons = 50
    kw = {
        "hist2d_nmax": n_photons,
        "hist2d_nmin": 10,
        "background_ch1": 2.3,
        "background_ch2": 1.2,
    }
    pda = tttrlib.Pda(**kw)

The ``Pda`` class will compute a matrix here called ``s1s2`` that will contain the
probabilities of detecting a photon in ch1 and ch2. The indices of the matrix correspond
to the number of photons, the matrix values to the probability. The parameter
``hist2d_nmax`` specifies up to wich maximum number of photons this matrix is
computed. The parameter ``hist2d_nmin`` specifies the minimum number of photons in
this matrix which will be considered in later steps. The parameter ``background_ch1``
and ``background_ch2`` specify the background count rate in the two channels.

The parameters can also be changed after the Pda object is created.

.. code-block:: python

    pda.background_ch1 = 2.0
    pda.background_ch2 = 5.0
    pda.hist2d_nmin = 5
    pda.hist2d_nmax = 60

To compute the PDA histogram intensity distribution of the fluorescence p(F),
needs to be specified. The intensity distribution of the fluorescence, P(F), can
be obtained from the total measured signal intensity distribution P(S) by deconvolution
assuming that the background signals obey Poisson distributions. In this description
we simply compute a Poisson distribution for p(F).

.. code-block:: python

    import scipy.stats
    mu = 20 # expectation value for the number of photons
    dist = scipy.stats.poisson(mu)
    x = np.arange(0, n_photons)
    pF = dist.pmf(x)
    pda.setPF(pF)

The last statement in the code above assigns the distribution p(F) to the Pda
object.

Next, a set of species with associated amplitudes with corresponding theoretical
probabilities of detecting a photon in the first channel. This can be done by
either assigning the amplitudes and the probabilities separately to the Pda object

.. code-block:: python

    amplitudes = [0.5, 0.5]
    probabilities_ch1 = [0.8, 0.2]
    pda.set_amplitudes(amplitudes)
    pda.set_probabilities_ch1(probabilities_ch1)

or by assigning a spectrum that consists of interleaved amplitudes and probabilities

.. code-block:: python

    p_spectrum_ch1 = np.dstack([amplitudes, probabilities_ch1]).flatten()
    pda.spectrum_ch1 = p_spectrum_ch1

The spectrum is interleaved array [a1, p1, a2, p2, ...] where ai refers to
amplitudes and p1 refers to probabilities of registering a photon in the first
channel. The probabilities ``probabilities_ch1`` are the  theoretical probability
of registering a photon in the first channel. In a FRET experiment the probability
relates to the FRET efficiency by

.. math::

    p_G = \left( 1 + \alpha + \frac{\gamma E}{(1-E)} \right)^{-1}
    \text{with} \gamma = \frac{g_R \Phi_A}{g_R \Phi_D}

where :math:`g_G`, :math:`g_R` are the detection efficiencies in the green and
red detection channel, respectively. :math:`\Phi_A`, :math:`\Phi_D` are the
fluorescence quantum yield of the acceptor and the donor, respectively. :math:`\alpha`
is the cross-talk from the donor to the acceptor channel, and :math:`E` is the
FRET efficiency.

The computed distribution of photons in ch1 and ch2 is accessed by the attribute
``s1s2``.

.. code-block:: python

    s1s2 = pda.s1s2

The matrix ``s1s2`` is computed when the attributed is accessed. The matrix is only
updated if a parameter of relevance is changed and the matrix is accessed.

In a PDA 2D analysis this matrix is often reduced in dimensionality to represent
the model and score against the data. For this dimensionality reduction
``tttrlib.Pda`` offers a method. However, first, it needs to be specified how the
matrix is reduced in dimensionality. For that, a function needs to be specified
and assigned to the object. Any python function with a least two arguments can
be used for that. The first argument always corresponds to ch1, the second
argument to ch2. For instance, a histogram of the proximity ration can be computed
by first defining a corresponding function and then creating a histogram using the
method ``get_1dhistogram``.

.. code-block:: python

    s1s2 = pda.s1s2
    # A one dimensional representation of the s1s2 matrix if obtain
    # by a function that projects the pairs of photons. Any python function
    # accepting at least two arguments can be used
    proximity_ratio = lambda ch1, ch2: ch2 / (ch1 + ch2)

    # The python function is used to set the attribute `histogram_function`
    pda.histogram_function = proximity_ratio

    # The method get_1dhistogram of the Pda object returns a 1D histogram
    # of the s1s2 array for the specified function
    x_pr, y_pr = pda.get_1dhistogram(
        log_x=False,
        x_min=0.0,
        x_max=1.0,
        n_bins=21
    )

The arguments of ``get_1dhistogram`` define the range and the resolution of the
histogram.

Functions, e.g., the FRET efficiency, that require additional parameters can be
passed to the Pda object by defining a function with additional arguments. Note,
potential division by zero need to be handled.

.. code-block:: python

    def fret_efficiency(ch1, ch2, phiD=0.8, phiA=0.32, det_ratio=0.32):
        return 1.0 / (1. + phiD / phiA * det_ratio * ch2 / ch1)

    pda.histogram_function = fret_efficiency
    x_eff, y_eff = pda.get_1dhistogram(
        log_x=False,
        x_min=0.0,
        x_max=1.0,
        n_bins=31
    )


Histograms with a logarithmic scale are computed by setting `log_x` to True.
When the option ``skip_zero_photon`` is set to False the first column and row of
the s1s2 matrix (zero photons in ch1 or ch2) is used. In this case potential division
by zeros in the histogram function need to be handled. The default value for
``skip_zero_photon`` is True.

.. code-block:: python

    sg_sr = lambda ch1, ch2: max(1, ch1) / max(1, ch2)
    pda.histogram_function = sg_sr
    x_sgsr, y_sgsr = pda.get_1dhistogram(
        log_x=True,
        x_min=0.05,
        x_max=80.0,
        n_bins=31,
        skip_zero_photon=False
    )

Finally, the 2D counting histogram and the 1D representations can be plotted.

.. code-block:: python

    fig, ax = p.subplots(nrows=1, ncols=3)
    ax[0].imshow(s1s2)
    ax[1].plot(x_pr, y_pr, label='Proximity ratio')
    ax[1].plot(x_eff, y_eff, label='FRET efficiency')
    ax[1].legend()
    ax[2].semilogx(x_sgsr, y_sgsr, label='Sg/Sr')
    ax[2].legend()
    p.show()

.. note::
    To score models against the data either the 2D histogram or the 1D representation
    can be used. The scoring is described elsewhere :cite:`kalinin2007`.

Example analysis of experimental data
-------------------------------------
In this section a small single-molecule dataset will be loaded and analyzed with
by optimizing a set of parameters to experimental histograms using the ``tttrlib.Pda``
class and basic `SciPy <https://www.scipy.org/>`_ functions. The photons that
correspond to the data selected for the PDA histograms are indexed. This way,
fluorescence decay curves that correspond to the PDA histograms are computed.

#.. plot:: ../examples/single_molecule/single_molecule_pda_2.py

The first step when a dataset is analyzed by the ``tttrlib.Pda`` class is to load
and create a ``TTTR`` object. In the example, the tttr data is split into multiple
BH132 spc files. Hence, for simpler analysis, the data is first stacked into a single
``TTTR`` object.

.. code-block:: python

    # open a set of files and stack them in a single TTTR object
    files = glob.glob('./data/bh/bh_spc132_smDNA/*.spc')
    data = tttrlib.TTTR(files[0], 'SPC-130')
    for d in files[1:]:
        data.append(tttrlib.TTTR(d, 'SPC-130'))

Next, the photons registered in the different routing channels need to be selected
and the photon trace is split into time windows (tws). Time windows with a specified
minimum number of photons are discriminated to reduce the background contribution to
the PDA histogram. The photons in the two detection channels are counted and a matrix
S1S2 that contains a histogram of the photon counts is created. This photon count
matrix is computed up to a maximum number of photons that is specified ahead. The
larger the maximum number of photons in the S1S2 matrix, the slower the algorithm
will be. This maximum number depends on the size of the tws. For large tws the maximum
number should be increased. For setups with large foci a larger maximum number of
photons should be used. Overall, the maximum number of photons should be adjusted
to the experiment. By inspecting a S1S2 matrix this number can be adjusted. Moreover,
The distribution of the signal intensity P(S) needs to be determined. The PDA
algorithm requires the distribution of the fluorescence intensity P(F), which can
be determined from P(S). In practice, at low background when tws with low photons
counts are discriminated P(F) can be approximated by P(S). Moreover, for a consistent
analysis over multiple representations of the experimental data, e.g., TCSPC or FCS,
care must be taken that the tw selection did not introduce a selection bias or that
the selections are at least consistent.

To create a experimental P(S1,S2) histogram, that is stored in form of a matrix,
the static method ``tttrlib.Pda.compute_experimental_histograms`` is used. The
method takes the channel selection, the tw size, and the minimum number of photons
in a tw as an input.

.. code-block:: python

    # define what is PDA channel 1 and channel 2
    channels_1 = [0, 8]  # 0, 8 are green channels
    channels_2 = [1, 9]  # 1,9 are red channels
    minimum_number_of_photons = 20
    maximum_number_of_photons = 150
    minimum_time_window_length = 1.0

    s1s2_e, ps, tttr_indices = tttrlib.Pda.compute_experimental_histograms(
        tttr_data=data,
        channels_1=channels_1,
        channels_2=channels_2,
        maximum_number_of_photons=maximum_number_of_photons,
        minimum_number_of_photons=minimum_number_of_photons,
        minimum_time_window_length=minimum_time_window_length
    )

The method ``tttrlib.Pda.compute_experimental_histograms`` returns the experimental
histogram that contains the counts in the first and second PDA channel, the histogram
over the number of counts P(S), and the tttr indices as numpy arrays. These indices
can be used in later steps to process the associated photons.

Next, a new instance of the ``tttrlib.Pda`` class needs to be created and a function
that converts P(S1,S2) into a 1 dimensional histogram needs to be specified.

.. code-block:: python

    # define a Pda object
    kw_pda = {
        "hist2d_nmax": maximum_number_of_photons,
        "hist2d_nmin": minimum_number_of_photons,
        "pF": ps
    }
    pda = tttrlib.Pda(**kw_pda)
    # set a function to make a 1D histogram
    pda.histogram_function = lambda ch1, ch2: ch1 / max(1, ch2)

Here, the ratio of the first and the second channel is used. Above we defined the
associated the first channel to the routing channel numbers [0, 8] and the second
channel to the routing channel numbers [1, 9]. For the dataset in this example this
corresponds to the "green" and "red" detection channel. Hence, the function used
to create one dimensional representations of the P(S1,S2) matrix is the ratio of
the green and red signals.

Now, we can define a set of parameters that is optimized to the experimental data.
Here we use a three species model. Moreover, we define an initial values for the
green and red background signal. Using these values, we compute a one dimensional
histogram weighted deviations of the model histogram for initial values.

.. code-block:: python

    initial_background_ch1 = 1.7
    initial_background_ch2 = 0.7
    initial_amplitudes = [0.33, 0.33, 0.33]
    initial_probabilities_ch1 = [0.0, 0.35, 0.45]
    kw_hist = {
        "x_max": 100.0,
        "x_min": 0.1,
        "log_x": True,
        "n_bins": 81,
        "n_min": 10
    }
    # get a initial model histogram
    x_model_initial, y_model_initial = pda.get_1dhistogram(
        amplitudes=initial_amplitudes,
        probabilities_ch1=initial_probabilities_ch1,
        **kw_hist
    )
    # get the data histogram
    x_data, y_data = pda.get_1dhistogram(
        s1s2=s1s2_e.flatten(),
        **kw_hist
    )
    initial_wres = (y_data - y_model_initial) / np.sqrt(y_data)

The computation of the initial weighted residuals can be omited and is merely here
for instructive purposed. Here, the deviations are weighted by the number of counts
in each histogram bin.

Next, an objective function, i.e., a function that with a minimum value for parameters
that agree optimally with the data is defined. A simple objective function is the
chi2 that measured the sum of the weighted squared deviations is used.

.. code-block:: python

    def chi2(
        x0: np.ndarray,
        y_data: np.ndarray,
        pda_object: tttrlib.Pda,
        n_species: int,
        kw_hist: dict
    ):
        amplitudes = x0[:n_species]
        probabilities = x0[n_species:n_species * 2]
        background_ch1 = x0[n_species * 2 + 0]
        background_ch2 = x0[n_species * 2 + 1]
        pda_object.background_ch1 = background_ch1
        pda_object.background_ch2 = background_ch2
        x_model, y_model = pda_object.get_1dhistogram(
            amplitudes=amplitudes,
            probabilities_ch1=probabilities,
            **kw_hist
        )
        weights = np.sqrt(y_data)
        np.place(weights, weights == 0, 10000000.0)
        wres = (y_data - y_model) / weights
        return np.sum(wres**2.0)

The model parameters are optimized using the defined objective function ``chi2``
using ``scipy`` whereas the parameters are bounded to a reasonable range.

.. code-block:: python

    n_species = 3
    x0 = np.array(
        initial_amplitudes + \
        initial_probabilities_ch1 + \
        [initial_background_ch1, initial_background_ch2]
    )
    bounds = [(-np.inf, np.inf)] * (2 * n_species) + [(0, 10), (0, 10)]
    fit = scipy.optimize.minimize(
        fun=chi2,
        x0=x0,
        bounds=bounds, #(0, np.inf),
        args=(y_data, pda, n_species, kw_hist),
    )

The model parameters are collected, the final set of weighted residuals
computed, and the micro times of the tws collected to create fluorescence decay
histograms that can be analyzed by other software packages (e.g.
`ChiSurf <https://github.com/fluorescence-tools/chisurf/>`_.).

.. code-block::python

    fitted_amplitudes = fit.x[:n_species]
    fitted_probabilities_ch1 = fit.x[n_species:2+n_species]
    fitted_background_ch1 = fit.x[n_species * 2 + 0]
    fitted_background_ch2 = fit.x[n_species * 2 + 1]
    pda.background_ch1 =fitted_background_ch1
    pda.background_ch2 =fitted_background_ch2
    x_model_fit, y_model_fit = pda.get_1dhistogram(
        amplitudes=fitted_amplitudes,
        probabilities_ch1=fitted_probabilities_ch1,
        **kw_hist
    )
    fit_wres = (y_data - y_model_fit) / np.sqrt(y_data)
    # Decay histogram
    tttr_selection = data[tttr_indices]
    tttr_08 = tttr_selection[tttr_selection.get_selection_by_channel([0, 8])]
    tttr_19 = tttr_selection[tttr_selection.get_selection_by_channel([1, 9])]
    decay_axis = np.linspace(256, 4096, 16)
    decay_red, _ = np.histogram(tttr_19.micro_times, bins=decay_axis)
    decay_green, _ = np.histogram(tttr_08.micro_times, bins=decay_axis)

Finally, the collected results are plotted for illustrative purposes.

.. code-block::python

    fig, ax = p.subplots(nrows=2, ncols=3)
    ax[0, 0].set_title('Experimental S1S2')
    ax[0, 1].set_title('1D Histograms')
    ax[1, 0].set_title('Model S1S2')
    ax[0, 2].set_title('Signal count distribution, P(S)')
    ax[1, 2].set_title('Fluorescence decays, f(t)')
    ax[1, 2].set_xlabel('time / ns')
    ax[0, 0].set_ylabel('Signal(red)')
    ax[0, 1].set_ylabel('w.res.')
    ax[1, 1].set_xlabel('Signal(green)/Signal(red)')
    ax[1, 1].set_ylabel('Counts')
    ax[1, 0].set_xlabel('Signal(green)')
    ax[1, 0].set_ylabel('Signal(red)')
    ax[0, 2].set_ylabel('Counts')
    ax[0, 2].set_xlabel('Number of photons')
    ax[0, 2].plot(ps, label="P(S)")
    ax[0, 2].legend()
    ax[0, 0].imshow(s1s2_e[1:, 1:])
    ax[1, 0].imshow(pda.s1s2[1:, 1:])
    ax[1, 0].legend()
    ax[0, 0].legend()
    ax[1, 2].bar(
        decay_axis[1:] * data.header.micro_time_resolution,
        decay_red, color='red', alpha=0.5
    )
    ax[1, 2].bar(
        decay_axis[1:] * data.header.micro_time_resolution,
        decay_green, color='green', alpha=0.5
    )
    ax[1, 2].set_yscale('log')
    fit_wres = np.nan_to_num(fit_wres, posinf=0, neginf=0)
    y_model_initial = np.nan_to_num(y_model_initial, posinf=0, neginf=0)
    ax[0, 1].set_ylim(-8, 8)
    if kw_hist['log_x']:
        ax[0, 1].semilogx(x_data, initial_wres, label="Initial")
        ax[0, 1].semilogx(x_data, fit_wres, label="Optimized")
        ax[1, 1].semilogx(x_model_initial, y_model_initial, label="Initial")
        ax[1, 1].semilogx(x_model_fit, y_model_fit, label="Optimized")
        ax[1, 1].semilogx(x_data, y_data, label="Experiment")
    else:
        ax[1, 1].plot(x_model_initial, y_model_initial, label="Model")
        ax[1, 1].plot(x_model_fit, y_model_fit, label="Optimized")
        ax[1, 1].plot(x_data, y_data, label="Experiment")
        ax[0, 1].plot(x_data, initial_wres, label="Initial")
        ax[0, 1].plot(x_data, fit_wres, label="Optimized")
    ax[1, 1].legend()
    ax[0, 1].legend()
    p.show()

The noise in the fluorescence decay histograms highlight that for a detailed
analysis of the fluorescence decays more photons are needed in comparison to an
intensity analysis by PDA.

Overall, in this section it was illustrated how experimental data can be analyzed.
The presented analysis serves only as example. For more detailed analysis the noise
model, i.e. the weights, and the underlying assumption when a chi2 is minimized
needs to be considered. For histograms with low counts maximizing the likelihood
function is more appropriate. Moreover, for data with considerable background P(F)
needs to be deconvolved from the experimental P(S) :cite:`kalinin2007`.

"""

from __future__ import division
import pylab as p
import tttrlib

import scipy.stats
import numpy as np

n_photons = 50
kw = {
    "hist2d_nmax": n_photons,
    "hist2d_nmin": 10,
    "background_ch1": 2.3,
    "background_ch2": 1.2,
}
pda = tttrlib.Pda(**kw)

# The parameters can also be set as attributes
pda.background_ch1 = 2.0
pda.background_ch2 = 5.0

# Here, we use scipy and a poisson distribution to compute an pF.
# For a real analysis pF needs to be estimated from the experiment.
mu = 20 # expectation value for the number of photons
dist = scipy.stats.poisson(mu)
x = np.arange(0, n_photons)
pF = dist.pmf(x)
pda.setPF(pF)

# Now we define a set of species with associated amplitudes
amplitudes = [0.33, 0.33, 0.33]

# probabilities_ch1 is the theoretical probability of detecting a photon
# in the first channel. In the PDA manuscript this is also called pG
probabilities_ch1 = [0.75, 0.4, 0.2]

pda.set_amplitudes(amplitudes)
pda.set_probabilities_ch1(probabilities_ch1)

# Alternatively the spectrum of amplitudes and probabilities
# can be set as an interleaved array [a1, p1, a2, p2, ...]
p_spectrum_ch1 = np.dstack([amplitudes, probabilities_ch1]).flatten()
pda.spectrum_ch1 = p_spectrum_ch1
pda.evaluate()

# The computed distribution of photons in channel 1 and channel 2
# is given by the attribute s1s2. This attribute is convolved with
# the probability of detecting a photon pF and the specified background
s1s2 = pda.s1s2

# A one dimensional representation of the s1s2 matrix if obtain
# by a function that projects the pairs of photons. Any python function
# accepting at least two arguments can be used
proximity_ratio = lambda ch1, ch2: ch2 / (ch1 + ch2)

# The python function is used to set the attribute `histogram_function`
pda.histogram_function = proximity_ratio

# The method get_1dhistogram of the Pda object returns a 1D histogram
# of the s1s2 array for the specified function
x_pr, y_pr = pda.get_1dhistogram(
    log_x=False,
    x_min=0.0,
    x_max=1.0,
    n_bins=21
)

# functions, e.g., the FRET efficiency, that require additional parameters
# can be passed to the Pda object by defining a function with additional
# arguments. Note, potential division by zero need to be handled. Above,
# zero divisions were not handled as overall minimum number of photons was
# set to 10 (hist2d_nmin) and the histogram starts be be computed from the
# first channel.
def fret_efficiency(ch1, ch2, phiD=0.8, phiA=0.32, det_ratio=0.32):
    return 1.0 / (1. + phiD / phiA * det_ratio * ch2 / ch1)


pda.histogram_function = fret_efficiency
x_eff, y_eff = pda.get_1dhistogram(
    log_x=False,
    x_min=0.0,
    x_max=1.0,
    n_bins=31
)

# Histograms with a logarithmic scale are computed by setting `log_x`
# to True. When the option skip_zero_photon is set to False the first
# column and row of the s1s2 matrix (zero photons in ch1 or ch2) is used
# in this case potential division by zeros in the histogram function
# need to be handled. The default value for skip_zero_photon is True.
sg_sr = lambda ch1, ch2: max(1, ch1) / max(1, ch2)
pda.histogram_function = sg_sr
x_sgsr, y_sgsr = pda.get_1dhistogram(
    log_x=True,
    x_min=0.05,
    x_max=80.0,
    n_bins=31,
    skip_zero_photon=False
)

fig, ax = p.subplots(nrows=1, ncols=3)
ax[0].imshow(s1s2)
ax[1].plot(x_pr, y_pr, label='Proximity ratio')
ax[1].plot(x_eff, y_eff, label='FRET efficiency')
ax[1].legend()
ax[2].semilogx(x_sgsr, y_sgsr, label='Sg/Sr')
ax[2].legend()
p.show()
