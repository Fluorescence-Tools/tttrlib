import tttrlib
import numpy as np
import pylab as p

# This initializes a new correlator object
correlator = tttrlib.Correlator()
correlator.n_bins = 10  # the correlator will use 10 equal spaces bins each block
correlator.n_casc = 25  # there are 25 blocks

# the example data set is located in the test folder of the GIT
# repository
data = tttrlib.TTTR('./test/data/BH/BH_SPC132.spc', 'SPC-130')
mt = data.get_macro_time()

# This selects the indices of the first routing channel 0
ch1_indeces = data.get_selection_by_channel([0])
t1 = mt[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)

# This selects the indices of the routing channel 8
ch2_indeces = data.get_selection_by_channel([8])
t2 = mt[ch2_indeces]
w2 = np.ones_like(t1, dtype=np.float)

# create for reference a correlation curve without filter
correlator.set_macrotimes(t1, t2)
correlator.set_weights(w1, w2)
# plot the curve with pylab
p.semilogx(correlator.x_axis, correlator.correlation)

# now we filter parts of the trace with a low count rate
# This sets the weights of regions below a certain count rate to zero
cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
w1[cr_selection] *= 0.0
w2 = np.ones_like(t2, dtype=np.float)
cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
w2[cr_selection] *= 0.0

correlator.set_weights(w1, w2)
p.semilogx(correlator.x_axis, correlator.correlation)

p.show()
