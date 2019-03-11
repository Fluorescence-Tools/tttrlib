#!/usr/bin/python

import timeit


#############
# Test the TTTR reading function
#############

import phconvert
import tttrlib
import numpy as np
import pylab as p
import numba

plot_output = True

# Load different file types
# 0 = PQ-PTU
# 1 = PQ-HT3
# 2 = BH-Spc132
# 3 = BH-Spc600_256
# 4 = BH-Spc600_4096 (I don't have a sample file)
# 5 = Photon-HDF5


photons_ptu = tttrlib.TTTR('../../examples/PQ/PTU/PQ_PTU_HH_T3.ptu', 0)
ht3 = tttrlib.TTTR('../../examples/PQ/HT3/01.ht3', 1)
spc132 = tttrlib.TTTR('../../examples/BH/BH_SPC132.spc', 2)
spc600_256 = tttrlib.TTTR('../../examples/BH/BH_SPC630_256.spc', 3)
photon_hdf5 = tttrlib.TTTR('../../examples/HDF/1a_1b_Mix.hdf5', 5)

# Compare speed to phconvert
n_test_runs = 10
time_phconvert = timeit.timeit(
    'phconvert.pqreader.load_ht3("../../examples/PQ/HT3/1a_1b_Mix.ht3", ovcfunc=None)',
    number=n_test_runs,
    setup="from __main__ import phconvert"
)
time_tttrlib = timeit.timeit(
    'tttrlib.TTTR("../../../examples/PQ/HT3/1a_1b_Mix.ht3", 1)',
    number=n_test_runs,
    setup="from __main__ import tttrlib"
)

print("time(phconvert) = %s" % (time_phconvert / n_test_runs))
print("time(tttrlib) = %s" % (time_tttrlib / n_test_runs))
print("tttrlib speedup: %.2f" % (time_phconvert / time_tttrlib))

# time(phconvert) = 5054 ms (without numba)
# time(phconvert) = 1840 ms (with numba) # 270 MB file -> 141 MB/sec
# time(tttrlib)   = 690 ms               # 270 Mb file -> 391 MB/sec

r = spc132
ch0 = r.get_indeces_by_channels(np.array([0]))
p.hist(r.micro_times[ch0], bins=np.linspace(1, 2000))

ch1 = r.get_indeces_by_channels(np.array([2]))
p.hist(r.micro_times[ch1], bins=np.linspace(1, 2000))

p.show()

################################
# Test the histogram function
################################
import tttrlib
import numpy as np
import pylab as p

events = tttrlib.TTTR('../../examples/PQ/HT3/01.ht3', 1)
# select special events
special_events = np.where(events.event_types == 1)
mt_special_events = events.macro_times[special_events]
p.plot(mt_special_events)
p.show()

# select photon
photon_events = np.where(events.event_types == 0)
mt = events[photon_events].micro_times.astype(np.int)


print("\n\nTesting linear histograms")
print("-------------------------")
for ch in range(6):
    data = mt[events.get_indeces_by_channels(np.array([ch]))].astype(np.float)
    bins = np.linspace(0, 32000, 32000, dtype=np.float)
    hist = np.zeros(len(bins), dtype=np.float)
    weights = np.ones_like(data)
    tttrlib.histogram1D_double(data, weights, bins, hist, 'lin', True)
    p.plot(hist)

p.show()
# Compare speed to numpy
n_test_runs = 50
time_np_hist_lin = timeit.timeit(
    'np.histogram(data, bins=bins)',
    number=n_test_runs,
    setup="from __main__ import np, bins, data"
)
time_tttrlib_hist_lin = timeit.timeit(
    "tttrlib.histogram1D_double(data, weights, bins, hist, 'lin', True)",
    number=n_test_runs,
    setup="from __main__ import tttrlib, data, weights, bins, hist"
)
print("time(numpy) = %s" % (time_np_hist_lin / n_test_runs))
print("time(tttrlib) = %s" % (time_tttrlib_hist_lin / n_test_runs))
print("tttrlib speedup: %.2f" % (time_np_hist_lin / time_tttrlib_hist_lin))



print("\n\nTesting logarithmic histograms")
print("------------------------------")
bins = np.logspace(0, 3.5, 32000, dtype=np.float)
data = mt[r.get_indeces_by_channels(np.array([0]))].astype(np.float)
hist = np.zeros(len(bins), dtype=np.float)
weights = np.ones_like(data)
tttrlib.histogram1D_double(data, weights, bins, hist, '', True)
if plot_output:
    p.plot(hist + 20)
    p.plot(np.histogram(data, bins=bins)[0])
    p.show()
n_test_runs = 200
time_np_hist_lin = timeit.timeit(
    'np.histogram(data, bins=bins)',
    number=n_test_runs,
    setup="from __main__ import np, bins, data"
)
time_tttrlib_hist_lin = timeit.timeit(
    "tttrlib.histogram1D_double(data, weights, bins, hist, 'log10', True)",
    number=n_test_runs,
    setup="from __main__ import tttrlib, data, weights, bins, hist"
)
print("time(numpy) = %s" % (time_np_hist_lin / n_test_runs))
print("time(tttrlib) = %s" % (time_tttrlib_hist_lin / n_test_runs))
print("tttrlib speedup: %.2f" % (time_np_hist_lin / time_tttrlib_hist_lin))


print("\n\nTesting arbitrary spacing")
print("-------------------------")
bins1 = np.logspace(0, 3.0, 320, dtype=np.float)
bins2 = np.linspace(1001, 4096, 1024, dtype=np.float)
bins = np.hstack([bins1, bins2])
data = mt[r.get_indeces_by_channels(np.array([0]))].astype(np.float)
hist = np.zeros(len(bins), dtype=np.float)
weights = np.ones_like(data)
tttrlib.histogram1D_double(data, weights, bins, hist, '', True)
if plot_output:
    p.plot(hist + 20)
    p.plot(np.histogram(data, bins=bins)[0])
    p.show()

n_test_runs = 200
time_np_hist_lin = timeit.timeit(
    'np.histogram(data, bins=bins)',
    number=n_test_runs,
    setup="from __main__ import np, bins, data"
)
time_tttrlib_hist_lin = timeit.timeit(
    "tttrlib.histogram1D_double(data, weights, bins, hist, '', True)",
    number=n_test_runs,
    setup="from __main__ import tttrlib, data, weights, bins, hist"
)
print("time(numpy) = %s" % (time_np_hist_lin / n_test_runs))
print("time(tttrlib) = %s" % (time_tttrlib_hist_lin / n_test_runs))
print("tttrlib speedup: %.2f" % (time_np_hist_lin / time_tttrlib_hist_lin))



#####################
# Complex Histograms
#####################

# create an linear axis
import tttrlib
import numpy as np
import pylab as p


def histogram2d(data, bins):
    h = tttrlib.doubleHistogram()
    h.setAxis(0, "x", -3, 3, bins, 'lin')
    h.setAxis(1, "y", -3, 3, bins, 'lin')
    h.setData(data.T)
    h.update()
    return h.getHistogram().reshape((bins, bins))


print("\n\nTesting 2D Histogram linear spacing")
print("---------------------------------------")
x = np.random.randn(10000)
y = 0.2 * np.random.randn(10000)
data = np.vstack([x, y])
bins = 100

hist2d = histogram2d(data, 100)
if plot_output:
    p.imshow(hist2d)
    p.show()

n_test_runs = 2000
time_np_2dhist_lin = timeit.timeit(
    'np.histogram2d(x, y, bins=bins)',
    number=n_test_runs,
    setup="from __main__ import np, bins, x, y"
)
time_tttrlib_2dhist_lin = timeit.timeit(
    "histogram2d(data, bins)",
    number=n_test_runs,
    setup="from __main__ import data, bins, histogram2d"
)
print("time(numpy) (ms) = %s" % (time_np_2dhist_lin / n_test_runs * 1000.0))
print("time(tttrlib) (ms) = %s" % (time_tttrlib_2dhist_lin / n_test_runs * 1000.0))
print("tttrlib speedup: %.2f" % (time_np_2dhist_lin / time_tttrlib_2dhist_lin))


# %timeit histogram2d(data, bins=100)    # 234 micro seconds / loop
# %timeit np.histogram2d(x, y, bins=100) # 1.93 ms / loop
# speedup 8.25

####################
## Create event trace based on selection
####################
import tttrlib
import numpy as np

data = tttrlib.TTTR('../../examples/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')
data.get_macro_time()

ch1_indeces = data.get_selection_by_channel(np.array([0]))
p2 = tttrlib.TTTR(data, ch1_indeces)
ch0 = p2.get_macro_time()




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



##############
# Selection based on channels
# usage: use e.g. routing signals
# as line marker/frame marker/ trigger for stopped-flow
###############
import tttrlib
import numpy as np


photons = tttrlib.TTTR('../../examples/PQ/HT3/01.ht3', 1)

