"""
===============================
Fluorescence decay analysis
===============================
This script demonstrates how a single-molecule TTTR data
stream can be processed to generate a instrumnet response 
function (IRF) and a filtered decay histogram with discriminated
scattered light / background.

"""
import matplotlib
import matplotlib.pylab as plt
import numpy as np
import tttrlib


# load file
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132_sm_dna/m000.spc', 'SPC-130')

ch0_indeces = data.get_selection_by_channel([0, 8])
data_ch0 = data[ch0_indeces]

n_bins = 512

data, _ = np.histogram(data_ch0.micro_times, bins=n_bins)

# selection from tttr object
cr_selection = data_ch0.get_selection_by_count_rate(
    time_window=10.0e-3, n_ph_max=10
)
low_count_selection = data_ch0[cr_selection]
# create histogram for IRF
irf_cr, _ = np.histogram(low_count_selection.micro_times, bins=n_bins)

# Select high count regions
# equivalent selection using selection function
# This selects at least 60 photons in a time-windows of 10 ms
cr_selection = data_ch0.get_selection_by_count_rate(
    time_window=10.0e-3, n_ph_max=60, invert=True
)
high_count_selection = data_ch0[cr_selection]
data_cr, _ = np.histogram(high_count_selection.micro_times, bins=n_bins)

time_axis = np.arange(0, n_bins) #* data.header.micro_time_resolution * 4096 / n_bins * 1e9


###################################
#     Plot                        #
###################################
fig, ax = plt.subplots(nrows=1, ncols=1, sharex=True, sharey=False)
ax.semilogy(time_axis, data, label="Data")
ax.semilogy(time_axis, irf_cr, label="IRF")
ax.semilogy(time_axis, data_cr, label="count rate filtered Data")
ax.legend()
plt.show()

