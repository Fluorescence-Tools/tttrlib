"""
============================
Micro time gated correlation
============================

The micro time gating example can be easily modified to correlate
the prompt and the delayed excitation in a pulsed interleaved excitation
experiment by changing the selection on the weights.

"""
import matplotlib.pylab as plt
import tttrlib
import numpy as np


fig, ax = plt.subplots(1, 2, sharex='col', sharey='row')

#  Read the TTTR data
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

# Create a correlator
correlator = tttrlib.Correlator()

B = 9
n_casc = 25
correlator.n_bins = B
correlator.n_casc = n_casc

# Select the green channels (channel number 0 and 8)
ch1_indeces = data.get_selection_by_channel([0])
ch2_indeces = data.get_selection_by_channel([8])

# green-red cross-correlation
t = data.macro_times

t1 = t[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)
t2 = t[ch2_indeces]
w2 = np.ones_like(t2, dtype=np.float)

correlator.set_events(t1, w1, t2, w2)

x = correlator.x_axis
y = correlator.correlation

ax[0].semilogx(x, y, label="Gp/Gs")
ax[0].set_title('RAW correlations')
ax[0].set_xlabel('corr. time / ms')
ax[0].set_ylabel('Correlation Amplitude')
ax[0].legend()

# Mask
t = data.get_macro_time()
mt = data.get_micro_time()

t1 = t[ch1_indeces]
mt1 = mt[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)
# This sets the weight where the micro time is smaller than 1500
# to zero and masks the photons
w1[np.where(mt1 < 1500)] *= 0.0

t2 = t[ch2_indeces]
mt2 = mt[ch2_indeces]
w2 = np.ones_like(t2, dtype=np.float)
w2[np.where(mt2 < 1500)] *= 0.0

correlator.set_events(t1, w1, t2, w2)

x = correlator.x_axis
y = correlator.correlation

ax[1].semilogx(x, y, label="Gp/Gs")
ax[1].set_xlabel('corr. time / ms')
ax[1].set_title('Gated correlation')
ax[1].legend()

plt.show()
