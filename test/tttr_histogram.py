
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
