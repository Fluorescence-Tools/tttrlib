"""
============================
Micro time gated correlation
============================

The micro time gating example can be easily modified to correlate
the prompt and the delayed excitation in a pulsed interleaved excitation (PIE)
experiment by changing the selection on the weights.

TODO: add reference to PIE https://www.sciencedirect.com/science/article/pii/S000634950572991X?via%3Dihub
TODO: Does it really make sense to show a green-delay correlation? Red-delay I would understand....
"""
import matplotlib.pylab as plt
import tttrlib
import numpy as np


fig, ax = plt.subplots(1, 2, sharex='col', sharey='row')

#  Read the TTTR data
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

# Create a correlator
correlator = tttrlib.Correlator()

# set numbers of bin and correlation cascades of the multi-tau correlation algorithm
B = 9
n_casc = 25
correlator.n_bins = B
correlator.n_casc = n_casc

# Select the green channels (channel number 0 and 8)
ch1_indices = data.get_selection_by_channel([0])
ch2_indices = data.get_selection_by_channel([8])

# get the macrotimes and set the weights
# First, the weights will = 1, i.e. no weighting will be used
t = data.macro_times

t1 = t[ch1_indices]
w1 = np.ones_like(t1, dtype=np.float)
t2 = t[ch2_indices]
w2 = np.ones_like(t2, dtype=np.float)

correlator.set_events(t1, w1, t2, w2)

x = correlator.x_axis
y = correlator.correlation

# Show the result
ax[0].semilogx(x, y, label="Gp/Gs")
ax[0].set_title('RAW correlations')
ax[0].set_xlabel('corr. time / ms')
ax[0].set_ylabel('Correlation Amplitude')
ax[0].legend()

# Mask the data using the microtime information
t = data.get_macro_time()
mt = data.get_micro_time()

t1 = t[ch1_indices]
mt1 = mt[ch1_indices]
w1 = np.ones_like(t1, dtype=np.float)
# This sets the weight where the micro time is smaller than 1500
# to zero and masks the photons, i.e. this corresponds to only taking
# the green photon emitted in the "delay" tine window of the PIE experiment
# into account for the correlation
w1[np.where(mt1 < 1500)] *= 0.0

t2 = t[ch2_indices]
mt2 = mt[ch2_indices]
w2 = np.ones_like(t2, dtype=np.float)
# same weights for correlation channel 2
w2[np.where(mt2 < 1500)] *= 0.0

# run the gated correlation
correlator.set_events(t1, w1, t2, w2)

x = correlator.x_axis
y = correlator.correlation

# show the gated correlation
ax[1].semilogx(x, y, label="Gp/Gs")
ax[1].set_xlabel('corr. time / ms')
ax[1].set_title('Gated correlation')
ax[1].legend()

plt.show()
