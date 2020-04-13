import tttrlib
import numpy as np
import pylab as p

# the example data set is located in the test folder of the GIT
# repository
data = tttrlib.TTTR('./test/data/BH/BH_SPC132.spc', 'SPC-130')
mt = data.get_macro_time()

# make a cross-correlation between the two green channels (ch 0, ch 8)
# This selects the indices of the first routing channel 0
ch1_indeces = data.get_selection_by_channel([0])
t1 = mt[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)

# This selects the indices of the routing channel 8
ch2_indeces = data.get_selection_by_channel([8])
t2 = mt[ch2_indeces]
w2 = np.ones_like(t1, dtype=np.float)

# This initializes a new correlator object
correlator = tttrlib.Correlator()
correlator.n_bins = 10  # the correlator will use 10 equal spaces bins each block
correlator.n_casc = 25  # there are 25 blocks
correlator.set_macrotimes(t1, t2)
correlator.set_weights(w1, w2)

x = correlator.get_x_axis_normalized()
y = correlator.get_corr_normalized()

p.semilogx(x, y)
p.show()
