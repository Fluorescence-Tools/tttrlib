import pylab as p
import scipy.optimize
import scipy.stats
import numpy as np
import tttrlib
import time
import glob
import os


# Read TTTR
files = glob.glob("../test/data/bh/bh_spc132_sm_dna/*.spc")
sorted(glob.glob('*.spc'), key=os.path.getmtime)
data = tttrlib.TTTR(files[0], 'SPC-130')
for d in files[1:]:
    data.append(tttrlib.TTTR(d, 'SPC-130'))

# select green photons
data_green = data[data.get_selection_by_channel([1, 9])]
# the macro time clock in ms
macro_time_resolution = data.header.macro_time_resolution / 1e6

# Make histograms
n_bins = 256  # number of bins in histogram
x_min, x_max = 4, 240  # fit range
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
minimum_number_of_photons_in_tw = 40  # minimum 120 photons in 1 ms
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

# initial values for the fit
burst_fits = list()
scatter, background, time_shift, irf_background = 0., 0.0, 0, 1300
lifetime_spectrum = [0.4, 2]

decay_object = tttrlib.Decay(
    decay_data=data_decays[0].astype(np.float64),
    instrument_response_function=irf.astype(np.float64),
    time_axis=time_axis,
    excitation_period=data.header.macro_time_resolution,
    x_max=x_max,
    x_min=x_min,
    lifetime_spectrum=lifetime_spectrum
)
lifetimes = list()
start = time.time()
x0 = np.hstack([scatter, background, time_shift, irf_background, lifetime_spectrum])
for i, data_decay in enumerate(data_decays):
    decay_object.data = data_decay
    chi2_before = decay_object.chi2 / (x_max - x_min)
    x0[5] = 1.
    decay_object.optimize(
        x=x0,
        fixed=[1, 1, 1, 1, 1, 0]
    )
    lifetime = decay_object.lifetime_spectrum[1]
    lifetimes.append(lifetime)
    chi2_after = decay_object.chi2 / (x_max - x_min)
    print("chi2_before: %.3f, chi2_after: %.3f, lifetime: %.2f" % (
        chi2_before, chi2_after, lifetime))
stop = time.time()
print("Run time: %s" % (stop - start))

p.plot(decay_object.model)
p.plot(decay_object.data)
p.show()


p.hist(lifetimes, range=(0, 6), bins=51)
p.show()
