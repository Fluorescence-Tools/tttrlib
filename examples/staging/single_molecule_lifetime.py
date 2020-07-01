import pylab as p
import scipy.optimize
import scipy.stats
import numpy as np
import tttrlib
import time
import glob
import os


def objective_function(
        x: np.ndarray,
        x0: np.ndarray,
        decay_object: tttrlib.Decay,
        x_min: int, x_max: int,
        use_initial_prior: bool = False,
        initial_sd: float = 1.0,
        verbose: bool = False
):
    kw = {
        'irf_background': x0[3],
        'constant_background': x0[1],
        'areal_scatter_fraction': x0[0],
        'irf_shift_channels': x0[2],
        'lifetime_spectrum': x[4:]
    }
    decay_object.set(**kw)
    chi2_mle = decay_object.get_chi2(x_min, x_max, type="poisson")
    ip = 0.0
    if use_initial_prior:
        v, v0 = x, x0
        ip = -np.sum(np.log(1./(1 + ((v - v0) / initial_sd)**2.0)))
        chi2_mle += ip
    if verbose:
        print("Total log prob: %s" % chi2_mle)
        print("Parameter initial log prior: %s" % ip)
        print("---")
    if np.isnan(chi2_mle):
        return np.inf
    return chi2_mle

# Read TTTR
files = glob.glob("../test/data/bh/bh_spc132_sm_dna/*.spc")
sorted(glob.glob('*.spc'), key=os.path.getmtime)
data = tttrlib.TTTR(files[0], 'SPC-130')
for d in files[1:]:
    data.append(tttrlib.TTTR(d, 'SPC-130'))


data_green = data[data.get_selection_by_channel([0, 8])]
# the macro time clock in ms
macro_time_resolution = data.header.macro_time_resolution / 1e6

# Make histograms
n_bins = 256  # number of bins in histogram
x_min, x_max = 4, 220  # fit range
dt = data.header.micro_time_resolution * 4096 / n_bins  # time resolution
time_axis = np.arange(0, n_bins) * dt

# IRF
# select background / scatter, maximum 7 photons in 6 ms
time_window_irf = 6.0
n_ph_max_irf = 7
irf, _ = np.histogram(
    data_green[
        data_green.get_selection_by_count_rate(
            time_window=time_window_irf,
            n_ph_max=n_ph_max_irf
        )
    ].micro_times, bins=n_bins
)

# Select bursts
time_window_size = 1.0  # size of time window used for burst selection
minimum_number_of_photons_in_tw = 60  # minimum 120 photons in 1 ms
tw_ranges = data_green.get_time_window_ranges(
    minimum_window_length=time_window_size,
    minimum_number_of_photons_in_time_window=minimum_number_of_photons_in_tw
)
start_stop = tw_ranges.reshape([len(tw_ranges) // 2, 2])
bursts_indices = [np.arange(start, stop) for start, stop in start_stop]
data_decays = [
    np.histogram(data_green[ph].micro_times, bins=n_bins)[0]
    for ph in bursts_indices
]
mt = data.macro_times
photons_per_burst = np.array(
    [len(ph) for ph in bursts_indices]
)
burst_durations = np.array(
    [(mt[ph[-1]] - mt[ph[0]]) * macro_time_resolution for ph in bursts_indices]
)
print(burst_durations)
print(photons_per_burst)

# initial values for the fit
scatter, background, time_shift, irf_background = 0.05, 0.0, 0, 50
lifetime_spectrum = [1, 4]
x0 = np.hstack([scatter, background, time_shift, irf_background, lifetime_spectrum])


decay_object = tttrlib.Decay(
    decay_data=data_decays[0].astype(np.float64),
    instrument_response_function=irf.astype(np.float64),
    time_axis=time_axis,
    excitation_period=data.header.macro_time_resolution
)
burst_fits = list()
start = time.time()
for i, data_decay in enumerate(data_decays):
    decay_object.data = data_decay
    fit = scipy.optimize.minimize(
        objective_function, x0,
        args=(x0, decay_object, x_min, x_max),
        method='BFGS'
    )
    burst_fits.append(fit.x)
    print("Mean lifetime: %.3f, chi2: %.3f" % (fit.x[5], fit.fun))
stop = time.time()
print("Run time: %s" % (stop - start))

fig, ax = p.subplots(nrows=2, ncols=1, sharex=True, sharey=False)
ax[1].semilogy(time_axis, irf, label="IRF")
for i, data_decay in enumerate(data_decays):
    ax[1].semilogy(time_axis, data_decay, label="Data_%s" % i)
ax[1].semilogy(
    time_axis[x_min:x_max],
    decay_object.model[x_min:x_max], label="Model"
)
ax[1].legend()
ax[0].plot(
    time_axis[x_min:x_max], decay_object.weighted_residuals[x_min:x_max],
    label='w.res.',
    color='green'
)
p.show()

lifetimes = np.array([f[5] for f in burst_fits])
print(lifetimes.std())
print(lifetimes.mean())

p.plot(decay_object.model)
p.plot(decay_object.data)
p.show()

p.hist(lifetimes, range=(0, 6), bins=51)
p.show()

