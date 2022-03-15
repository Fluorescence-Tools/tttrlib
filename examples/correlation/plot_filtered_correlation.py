"""
====================
Filtered correlation
====================

Fluorescence correlations spectroscopy (FCS)
filtered fluorescence correlation spectroscopy (fFCS)
combines the micro and the macro time information.

The micro time information is used together with a filter function
to compute weights for the correlation. Instead of correlating the
photon traces the output of the filter function is correlated.

A very common xxx is to discriminated scattered light, or in
FRET FCS to compute species cross-correlation functions that inform
on dynamics.

Objective scatter filtered correlation using filtered correlation

"""
import pylab as plt
import tttrlib
import numpy as np

#%%
# First, we read the data data
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

#%%
# Next, we compute fluorescence intensity decay histograms for the
# low and high count rate regions of the data. These histograms will
# serve as references for the computation of the filters for fFCS

#%%
# For reference, we compute the
channels_gg = [0], [8]
channels_gr = [0, 8], [1, 9]

correlator = tttrlib.Correlator(
    tttr=data,
    n_bins=1,
    n_casc=30,
    channels=channels_gg,  # green correlation
)
x_gg, y_gg = correlator.x_axis, correlator.correlation

correlator = tttrlib.Correlator(
    tttr=data,
    n_bins=1,
    n_casc=30,
    channels=channels_gr,  # green-red correlation
)
x_gr, y_gr = correlator.x_axis, correlator.correlation

fig, ax = plt.subplots(nrows=1, ncols=2)
ax[0].semilogx(x_gg, y_gg)
ax[0].semilogx(x_gr, y_gr)
plt.show()
