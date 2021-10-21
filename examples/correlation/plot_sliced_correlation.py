"""
==================================
Correlation of sliced TTTR objects
==================================

Introduction
------------
Fluorescence Correlation Spectroscopy (FCS) in cells often
suffers from artifacts caused by bright aggregates or vesicles,
depletion of fluorophores or bleaching of a fluorescent background.
A common practice is to record multiple FCS curves and reject
manually FCS curves. When recording TTTR data that is later correlated
the processing of the data, i.e., the computation of FCS curves
and the sorting based on certain criteria :cite:`Ries2010` can be
automated to greatly simplify and accelerates the data analysis.

Here, we demonstrate how to use tttrlib to slice data into subsets
and correlate the subsets individually. The output of such procedure
can be used an an input to select and discriminate perturbed FCS
curves for automated suppression of sample-related artifacts in
Fluorescence Correlation Spectroscopy.
"""

#%%
# Implementation
# --------------
# First, we import libraries to read the data into a new TTTR container.
# We inspect the header to find header tags that inform
# on the macro time calibration. We print the content of the data header
# as is can be useful for asserting correct correlation parameters in later
# steps.
import pylab as plt
import tttrlib
import numpy as np

data = tttrlib.TTTR('../../tttr-data/pq/ptu/pq_ptu_hh_t2.ptu')
print(data.header.get_json())

#%%
# Here, we manually compute the calibration. This many cases it may not be
# necessary in many cases.
time_calibration = data.header.tag('MeasDesc_GlobalResolution')['value']

#%%
# Next, we plan to split the TTTR data into separate data chunks. Here, chunk
# the data into at least 5 seconds long pieces. The method ``get_ranges_by_time_window``
# returns an one-dimensional array with the beginning and the end index of each
# chunk. Later, we will use these start/stop indices to define TTTR slices
# that will be correlated. To slice the data into time windows we use the
# macro time calibration we computed previously. Note, the last chunk can
# be shorter than the specified time.
minimum_window_length = 5.0  # in seconds
time_windows = data.get_ranges_by_time_window(minimum_window_length, macro_time_calibration=time_calibration)
start_stop = time_windows.reshape((len(time_windows)//2, 2))

#%%
# Before correlating the data, we define the number of bins and the number
# of coarsening steps for the multi-tau correlation algorithm and create
# a new correlator instance.
corr_settings = {
    "n_bins": 9,
    "n_casc": 30
}
correlator = tttrlib.Correlator(**corr_settings)

#%%
# We print the used routing channels and define the routing channels
# that should be used in the correlation as a first an second correlation
# channel.
print(data.get_used_routing_channels())
ch1 = [0]
ch2 = [2]

# use start stop to create new TTTR objects that are correlated
correlations = list()
for start, stop in start_stop:
    indices = np.arange(start, stop, dtype=np.int64)
    tttr_slice = data[indices]
    tttr_ch1 = tttr_slice[tttr_slice.get_selection_by_channel(ch1)]
    tttr_ch2 = tttr_slice[tttr_slice.get_selection_by_channel(ch2)]
    correlator.set_tttr(
        tttr_1=tttr_ch1,
        tttr_2=tttr_ch2
    )
    correlations.append(
        (correlator.x_axis, correlator.correlation)
    )

#%%
# Finally, we compute the correlation for all data and plot the all correlations
# for all subsets.
correlator = tttrlib.Correlator(
    tttr=data,
    channels=(ch1, ch2),
    **corr_settings
)

fig, ax = plt.subplots(nrows=1, ncols=2, sharey=True, sharex=True)
ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation
)
ax[0].set_title('Correlation all data')
ax[1].set_title('Correlation of slices')
for x, y in correlations[:-1]:
    ax[1].semilogx(x, y)
ax[0].set_ylim([min(correlator.correlation) * 0.9, max(correlator.correlation) * 1.1])
ax[0].set_xlabel(r'corr. time (s) ')
ax[1].set_xlabel(r'corr. time (s) ')
ax[0].set_ylabel(r'corr. amplitude')
plt.show()


#%%
# Slicing data and correlating photon traces of data subsets can be
# useful to discrimiate artifacts or to estimate the noise of experimental
# correlation curves.

