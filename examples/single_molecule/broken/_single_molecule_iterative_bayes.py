import pylab as p
import scipy.optimize
import scipy.stats
import numpy as np
import numba as nb
import tttrlib

eps = 1e-9


@nb.jit()
def entropy(amplitudes, amplitude_prior):
    s = 0.0
    for a, a0 in zip(amplitudes, amplitude_prior):
        if a > eps and a0 > eps:
            s += a * np.log(a / a0)
    return s


def objective_function_entropy(
        x: np.ndarray,
        x0: np.ndarray,
        decay_object: tttrlib.Decay,
        x_min: int, x_max: int,
        lifetime_spectrum: np.array,
        use_entropy: bool = True,
        use_initial_prior: bool = False,
        entropy_weight: float = 1.5,
        initial_sd: float = 0.01,
        verbose: bool = True
):
    # overwrite amplitudes
    amplitudes = x[4:]
    lifetime_spectrum[::2] = amplitudes
    kw = {
        'lifetime_spectrum': lifetime_spectrum
    }
    decay_object.set(**kw)
    score = 0.0
    chi2 = decay_object.get_chi2(x_min, x_max, type='poisson')
    if verbose:
        print("Chi2 : %s" % chi2)
    score += chi2

    if use_entropy:
        s = 0.5 * entropy(x, x0)
        s += entropy(amplitudes, np.ones_like(amplitudes))
        score -= entropy_weight * s
        if verbose:
            print("Negative Entropy: %s" % s)
            print("---")

    if use_initial_prior:
        v = x[:4]
        v0 = x0[:4]
        ip = -np.sum(np.log(1./((1+((v - v0) / initial_sd)**2.0))))
        score += ip
        if verbose:
            print("Parameter initial log prior: %s" % ip)
    if np.isnan(score):
        return np.inf
    return score


n_bins = 128  # number of bins in histogram
time_window_size = 1.0  # size of time window used for burst selection
minimum_number_of_photons_in_tw = 120

data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')
data_green = data[data.get_selection_by_channel([0, 8])]

# low count rate for IRF
irf, _ = np.histogram(
    data_green[
        data_green.get_selection_by_count_rate(
            time_window=6.0,
            n_ph_max=7
        )
    ].micro_times, bins=n_bins
)

# Select bursts
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
time_axis = np.arange(0, n_bins) * data.header.micro_time_resolution * 4096 / n_bins

###################################
#     Make decay object           #
###################################
# fit range
x_min, x_max = 2, 110

# initial values for the fit
scatter, background, time_shift, irf_background = 0.05, 0, 0, 5
amplitudes = np.ones(31, dtype=np.float64)
min_lifetime, max_lifetime = 0.01, 6.0
lifetimes = np.linspace(min_lifetime, max_lifetime, len(amplitudes))
lifetime_spectrum = np.empty(2 * len(lifetimes))
lifetime_spectrum[::2] = amplitudes
lifetime_spectrum[1::2] = lifetimes

x0 = np.hstack([scatter, background, time_shift, irf_background, amplitudes])
fig, ax = p.subplots(nrows=1, ncols=1)
for i, data_decay in enumerate(data_decays):
    decay_object = tttrlib.Decay(
        decay_data=data_decay.astype(np.float64),
        instrument_response_function=irf.astype(np.float64),
        time_axis=time_axis,
        excitation_period=data.header.macro_time_resolution
    )
    fit = scipy.optimize.minimize(
        objective_function_entropy, x0,
        args=(x0, decay_object, x_min, x_max, lifetime_spectrum),
        method='BFGS'
    )
    x0 = fit.x
    print("mean lifetime: %s, chi2: %s" % (np.dot(x0[4:], lifetimes) / np.sum(x0[4:]), fit.fun))
    ax.plot(lifetimes, fit.x[4:], label='%i' % i)
p.legend()
p.show()

fig, ax = p.subplots(nrows=2, ncols=1, sharex=True, sharey=False)
ax[1].semilogy(time_axis, irf, label="IRF")
for data_decay in data_decays:
    ax[1].semilogy(time_axis, data_decay, label="Data_%s" % i)
ax[1].semilogy(time_axis[x_min:x_max], decay_object.model[x_min:x_max], label="Model")
ax[1].legend()
ax[0].plot(
    time_axis[x_min:x_max], decay_object.weighted_residuals[x_min:x_max],
    label='w.res.',
    color='green'
)
p.show()

