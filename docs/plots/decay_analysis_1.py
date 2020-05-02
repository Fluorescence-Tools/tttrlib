import pylab as p
import scipy.optimize
import numpy as np
import tttrlib


def objective_function_chi2(
        x: np.ndarray,
        decay_object: tttrlib.Decay,
        x_min:int = 20,
        x_max:int = 150
):
    scatter = x[0]
    background = x[1]
    time_shift = x[2]
    irf_background = x[3]
    lifetime_spectrum = x[4:]
    decay_object.set_lifetime_spectrum(lifetime_spectrum)
    decay_object.set_irf_background_counts(irf_background)
    decay_object.set_areal_fraction_scatter(scatter)
    decay_object.set_constant_background(background)
    decay_object.set_irf_shift_channels(time_shift)
    wres = decay_object.get_weighted_residuals()
    return np.sum(wres[x_min:x_max]**2)


def objective_function_mle(
        x: np.ndarray,
        decay_object: tttrlib.Decay,
        x_min:int = 20,
        x_max:int = 150
):
    scatter = x[0]
    background = x[1]
    time_shift = x[2]
    irf_background = x[3]
    lifetime_spectrum = x[4:]
    decay_object.set_lifetime_spectrum(lifetime_spectrum)
    decay_object.set_irf_background_counts(irf_background)
    decay_object.set_areal_fraction_scatter(scatter)
    decay_object.set_constant_background(background)
    decay_object.set_irf_shift_channels(time_shift)
    d = decay_object.get_data()
    m = decay_object.get_model()
    v = np.sum(d[x_min:x_max] * np.log(d[x_min:x_max] / m[x_min:x_max]))
    return v



#######################################
########### Prepare data ##############
#######################################
# load file
spc132_filename = '../../test/data/bh/bh_spc132.spc'
data = tttrlib.TTTR(spc132_filename, 'SPC-130')
ch0_indeces = data.get_selection_by_channel([0, 8])
data_ch0 = data[ch0_indeces]

n_bins = 512
# selection from tttr object
cr_selection = data_ch0.get_selection_by_count_rate(
    time_window=6.0, n_ph_max=7
)
low_count_selection = data_ch0[cr_selection]
# create histogram for IRF
irf, _ = np.histogram(low_count_selection.micro_times, bins=n_bins)

##### Select high count regions
# equivalent selection using selection function
cr_selection = tttrlib.selection_by_count_rate(
    data_ch0.macro_times,
    0.005, n_ph_max=2,
    macro_time_calibration=data.header.macro_time_resolution / 1e6,
    invert=True
)
high_count_selection = data_ch0[cr_selection]
data_decay, _ = np.histogram(high_count_selection.micro_times, bins=n_bins)
time_axis = np.arange(0, n_bins) * data.header.micro_time_resolution * 4096 / n_bins

#######################################
###     Make decay object           ###
#######################################
decay_object = tttrlib.Decay(
    decay_data=data_decay.astype(np.float64),
    instrument_response_function=irf.astype(np.float64),
    time_axis=time_axis,
    excitation_period=data.header.macro_time_resolution
)
decay_object.evaluate(lifetime_spectrum = [1., 1.2, 1., 3.5])

# A minimum number of photons should be in each channel
# as no MLE is used and Gaussian distributed errors are assumed
sel = np.where(data_decay>8)[0]
x_min = min(sel)

# The BH card are not linear at the end of the TAC. Thus
# fit not the full range
x_max = max(sel)

# Set some initial values for the fit
scatter = [0.05]
background = [2.5]
time_shift = [0]
irf_background = [0]
lifetime_spectrum = [1., 1.2, 1., 3.5]

#######################################
###     Optimize                    ###
#######################################
x0 = np.hstack([scatter, background, time_shift, irf_background, lifetime_spectrum])
fit = scipy.optimize.minimize(
    objective_function_chi2,
    x0,
    args=(decay_object, x_min, x_max),
)
wres = decay_object.get_weighted_residuals()

#######################################
###     Plot                        ###
#######################################
fig, ax = p.subplots(nrows=2, ncols=1, sharex=True, sharey=False)
ax[1].semilogy(time_axis, irf, label="IRF")
ax[1].semilogy(time_axis, data_decay, label="Data")
ax[1].semilogy(time_axis[x_min:x_max], decay_object.get_model()[x_min:x_max], label="Model")
ax[1].legend()
ax[0].plot(
    time_axis[x_min:x_max], wres[x_min:x_max],
    label='w.res.',
    color='green'
)
p.show()

