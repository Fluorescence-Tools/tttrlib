import pylab as p
import tttrlib
import numpy as np

fig, ax = p.subplots(nrows=1, ncols=2)


#  Read the data data

data = tttrlib.TTTR('../../data/BH/BH_SPC132.spc', 'SPC-130')

# Create correlator
B = 9
n_casc = 25

correlator = tttrlib.Correlator()
correlator.set_n_bins(B)
correlator.set_n_casc(n_casc)


# Select the green channels (channel number 0 and 8)

ch1_indeces = data.get_selection_by_channel(np.array([0]))
ch2_indeces = data.get_selection_by_channel(np.array([8]))

mt = data.get_macro_time()

t1 = mt[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)

t2 = mt[ch2_indeces]
w2 = np.ones_like(t2, dtype=np.float)

correlator.set_events(t1, w1, t2, w2)
correlator.run()

x = correlator.get_x_axis_normalized()
y = correlator.get_corr_normalized()

ax[0].semilogx(x, y)


# green-red cross correlation

ch1_indeces = data.get_selection_by_channel(np.array([0, 8]))
ch2_indeces = data.get_selection_by_channel(np.array([1, 9]))

mt = data.get_macro_time()

t1 = mt[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)

t2 = mt[ch2_indeces]
w2 = np.ones_like(t2, dtype=np.float)

correlator.set_events(t1, w1, t2, w2)
correlator.run()

x = correlator.get_x_axis_normalized()
y = correlator.get_corr_normalized()

ax[1].semilogx(x, y)


p.show()
