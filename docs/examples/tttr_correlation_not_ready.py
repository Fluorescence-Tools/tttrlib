

"""
# Use this to compare noise of the correlation curve (sth. seems to be off in the pycorrelate
# implementation...

import pycorrelate

t1 = mt[ch1_indeces]
t2 = mt[ch2_indeces]
bins = pycorrelate.make_loglags(0, np.log10(tau[-2]), 30)
G = pycorrelate.pcorrelate(t1.astype(np.float), t2.astype(np.float), bins, True)
p.semilogx(bins[1:], G)
p.show()

%time G=pycorrelate.pcorrelate(t1.astype(np.float), t2.astype(np.float), bins, True)

#######################################
# Selection based on time windows
#######################################
import tttrlib
import numpy as np
import pylab as p

photons = tttrlib.TTTR('../../examples/BH/BH_SPC132.spc', 2)
mt = photons.macro_times

tws = tttrlib.get_ranges_time_window(mt, 1000000, -1, 400, -1)
tws = tws.reshape([len(tws)/2, 2]) # bring the tws in shape

# convert the tws to photon selections
phs = list()
for tw in tws:
    phs += range(tw[0], tw[1])


# Use the tw selection for correlation

B = 10
n_casc = 25

t1 = mt[phs]
w1 = np.ones_like(t1, dtype=np.float)
t2 = mt[phs]
w2 = np.ones_like(t2, dtype=np.float)


correlator = tttrlib.Correlator()
correlator.set_n_bins(B)
correlator.set_n_casc(n_casc)
correlator.set_events(t1, w1, t2, w2)
correlator.run()

x = correlator.get_x_axis_normalized()
y = correlator.get_corr_normalized()

p.semilogx(x, y)
p.show()

"""
