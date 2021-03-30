"""
===============================
Fluorescence decay analysis - 1
===============================
In many cases fluorescence decays can be described by linear combinations of
exponential decays. The ``Decay`` class of ``tttrlib`` computes models for fluorescence
decays, f(t), fluorescence intensity decay histograms, h(t), and to scores models
against experimental decay histograms. The ``Decay`` class handles typical artifacts
encountered in fluorescence decays such as scattered light, constant backgrounds,
convolutions with the instrument response function :cite:`oconnor_time-correlated_2012`,
pile-up artifacts :cite:`coates_correction_1968`, differential non-linearity of the
micro time channels :cite:`becker_advanced_2005` and more.

Below the basic usage of the ``Decay`` class is outlined with a few application
examples. These examples can be used as starting point for custom analysis pipelines,
e.g. for burst-wise single-molecule analysis, pixel-wise FLIM analysis or analysis
over larger ensembles of molecules or pixels.

Decay histograms
----------------

.. code-block::python

    tttr_object = tttrlib.TTTR('../../test/data/BH/BH_SPC132.spc', 'SPC-130')
    micro_time_coarsening = 8
    counts, bins = fit2x.Decay.compute_microtime_histogram(
        tttr_object,
        micro_time_coarsening=micro_time_coarsening,
    )



Decay class
-----------
Fluorescence decays can be either computed using the static method provided by the
``Decay`` class or
Create an instance the ``Decay`` class

.. code-block::python

    decay_object = fit2x.Decay(
        data=data_decay.astype(np.float64),
        instrument_response_function=irf.astype(np.float64),
        time_axis=time_axis,
        period=data.header.macro_time_resolution
    )


Single-molecule

"""
import matplotlib.pylab as plt
import scipy.optimize
import scipy.stats
import numpy as np
import tttrlib
import fit2x


def objective_function_chi2(
        x: np.ndarray,
        decay_object: fit2x.Decay,
        x_min: int = 20,
        x_max: int = 150
):
    scatter, background, time_shift, irf_background = x[0:4]
    lifetime_spectrum = x[4:]
    decay_object.set_lifetime_spectrum(lifetime_spectrum)
    decay_object.irf_background_counts = irf_background
    decay_object.scatter_fraction = scatter
    decay_object.constant_offset = background
    decay_object.irf_shift = time_shift
    # wres = decay_object.get_weighted_residuals()
    # return np.sum(wres[x_min:x_max]**2)
    return decay_object.get_score(x_min, x_max)


def objective_function_mle(
        x: np.ndarray,
        x0: np.ndarray,
        decay_object: fit2x.Decay,
        x_min: int = 20,
        x_max: int = 500,
        use_amplitude_prior: bool = True,
        use_initial_prior: bool = True,
        amplitude_bias: float = 5.0,
        initial_sd: float = 5.0,
        verbose: bool = False
):
    scatter, background, time_shift, irf_background = x[0:4]
    lifetime_spectrum = np.abs(x[4:])
    decay_object.set_lifetime_spectrum(lifetime_spectrum)
    decay_object.irf_background_counts = irf_background
    decay_object.scatter_fraction = scatter
    decay_object.constant_offset = background
    decay_object.irf_shift = time_shift
    chi2_mle = decay_object.get_score(x_min, x_max, score_type="poisson")
    # d = decay_object.get_data()[x_min:x_max]
    # m = decay_object.get_model()[x_min:x_max]
    # return np.sum((m - d) - d * np.log(m/d))
    ap = 0.0
    if use_amplitude_prior:
        p = lifetime_spectrum[::2]
        a = np.ones_like(p) + amplitude_bias
        ap = scipy.stats.dirichlet.logpdf(p / np.sum(p), a)
        chi2_mle += ap
    ip = 0.0
    if use_initial_prior:
        ip = -np.sum(np.log(1./((1+((x - x0) / initial_sd)**2.0))))
        chi2_mle += ip
    if verbose:
        print("Total log prob: %s" % chi2_mle)
        print("Parameter log prior: %s" % ip)
        print("Dirichlet amplitude log pdf: %s" % ap)
        print("---")
    return chi2_mle


#######################################
#           Prepare data
#######################################

# load file
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')
ch0_indeces = data.get_selection_by_channel([0, 8])
data_ch0 = data[ch0_indeces]

n_bins = 512
# selection from tttr object
cr_selection = data_ch0.get_selection_by_count_rate(
    time_window=6.0e-3, n_ph_max=7
)
low_count_selection = data_ch0[cr_selection]
# create histogram for IRF
irf, _ = np.histogram(low_count_selection.micro_times, bins=n_bins)

# Select high count regions
# equivalent selection using selection function
cr_selection = tttrlib.selection_by_count_rate(
    data_ch0.macro_times,
    0.100, n_ph_max=5,
    macro_time_calibration=data.header.macro_time_resolution / 1e6,
    invert=True
)
high_count_selection = data_ch0[cr_selection]
data_decay, _ = np.histogram(high_count_selection.micro_times, bins=n_bins)
time_axis = np.arange(0, n_bins) * data.header.micro_time_resolution * 4096 / n_bins

###################################
#     Make decay object
###################################
decay_object = fit2x.Decay(
    data=data_decay.astype(np.float64),
    irf_histogram=irf.astype(np.float64),
    time_axis=time_axis,
    excitation_period=data.header.macro_time_resolution,
    lifetime_spectrum=[1., 1.2, 1., 3.5]
)
decay_object.evaluate()

# A minimum number of photons should be in each channel
# as no MLE is used and Gaussian distributed errors are assumed
sel = np.where(data_decay > 1)[0]
x_min = 10 #int(min(sel))

# The BH card are not linear at the end of the TAC. Thus
# fit not the full range
x_max = 450 #max(sel)

# Set some initial values for the fit
scatter = [0.05]
background = [2.01]
time_shift = [2]
irf_background = [5]
lifetime_spectrum = [0.5, 0.5, 0.5, 3.5]

###################################
#     Optimize                    #
###################################
x0 = np.hstack(
    [
        scatter,
        background,
        time_shift,
        irf_background,
        lifetime_spectrum
    ]
)
fit = scipy.optimize.minimize(
    objective_function_mle, x0,
    args=(x0, decay_object, x_min, x_max, False, False)
)
fit_mle = fit.x

x0 = np.hstack([scatter, background, time_shift, irf_background, lifetime_spectrum])
fit = scipy.optimize.minimize(
    objective_function_mle, x0,
    args=(x0, decay_object, x_min, x_max)
)
fit_map = fit.x

###################################
#     Plot                        #
###################################
fig, ax = plt.subplots(nrows=2, ncols=1, sharex=True, sharey=False)
ax[1].semilogy(time_axis, irf, label="IRF")
ax[1].semilogy(time_axis, data_decay, label="Data")
ax[1].semilogy(
    time_axis[x_min:x_max],
    decay_object.model[x_min:x_max], label="Model"
)
ax[1].legend()
ax[0].plot(
    time_axis[x_min:x_max],
    decay_object.weighted_residuals[x_min:x_max],
    label='w.res.',
    color='green'
)
plt.show()

