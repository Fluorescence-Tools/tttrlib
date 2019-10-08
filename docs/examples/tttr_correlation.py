
#####################
# Correlation
#####################


import tttrlib
import numpy as np
import pylab as p

#data = tttrlib.TTTR("../../../examples/BH/m000.spc", 'SPC-130')
data = tttrlib.TTTR('./examples/BH/BH_SPC132.spc', 'SPC-130')
#data = tttrlib.TTTR("../../../examples/PQ/HT3/1a_1b_Mix.ht3", 'HT3')
#data = tttrlib.TTTR('../../examples/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')

# make a cross-correlation between the two green channels (ch 0, ch 8)
ch1_indeces = data.get_selection_by_channel(np.array([0]))
ch2_indeces = data.get_selection_by_channel(np.array([8]))

mt = data.get_macro_time()

t1 = mt[ch1_indeces]
w1 = np.ones_like(t1, dtype=np.float)
cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
w1[cr_selection] *= 0.0

t2 = mt[ch2_indeces]
w2 = np.ones_like(t2, dtype=np.float)
cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
w2[cr_selection] *= 0.0

B = 10
n_casc = 25

correlator = tttrlib.Correlator()
correlator.set_n_bins(B)
correlator.set_n_casc(n_casc)
correlator.set_events(t1, w1, t2, w2)
correlator.run()

x = correlator.get_x_axis_normalized()
y = correlator.get_corr_normalized()

p.semilogx(x, y)
p.show()


# option 2 with photon stream selector
import tttrlib
import numpy as np
import pylab as p


data = tttrlib.TTTR('../../examples/BH/BH_SPC132.spc', 'SPC-130')

# make a cross-correlation between the two green channels (ch 0, ch 8)
ch1_indeces = data.get_selection_by_channel(np.array([0]))
ch2_indeces = data.get_selection_by_channel(np.array([8]))
ph1 = tttrlib.TTTR(data, ch1_indeces)
ph2 = tttrlib.TTTR(data, ch2_indeces)


t1 = ph1.get_macro_time()
w1 = np.ones_like(t1, dtype=np.float)
cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
w1[cr_selection] *= 0.0

t2 = ph2.get_macro_time()
w2 = np.ones_like(t2, dtype=np.float)
cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
w2[cr_selection] *= 0.0

B = 10
n_casc = 25

correlator = tttrlib.Correlator()
correlator.set_n_bins(B)
correlator.set_n_casc(n_casc)
correlator.set_events(t1, w1, t2, w2)
correlator.run()

x = correlator.get_x_axis_normalized()
y = correlator.get_corr_normalized()

p.semilogx(x, y)
p.show()


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

