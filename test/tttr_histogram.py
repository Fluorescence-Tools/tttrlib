
################################
# Test the histogram function
################################
import tttrlib
import numpy as np
import pylab as p
import numba as nb

events = tttrlib.TTTR('./examples/PQ/HT3/04.ht3', 1)
#events = tttrlib.TTTR('./examples/PQ/HT3/PQ_HT3_CLSM.ht3', 1)
# select special events
e = events.get_event_type()
t = events.get_macro_time()
m = events.get_micro_time()
c = events.get_routing_channel()


special_events = np.where(e == 1)
p.plot(c[special_events])
p.show()

list(c[special_events]).count(4)  # 41    - frame marker
list(c[special_events]).count(1)  # 10246 - line marker (1 comes first  = line start)
list(c[special_events]).count(2)  # 10246 - line marker (2 comes second = line stop)
# 10246 / (41-1) = 256 lines per image



def count_marker(channels, event_types, marker, event_type):
    n = 0
    for i, ci in enumerate(channels):
        if (ci == marker) and (event_types[i] == event_type):
            n += 1
    return n


@nb.jit
def find_marker(channels, markers, maker, event_type=1):
    r = list()
    for i, ci in enumerate(channels):
        if (ci == maker) and (markers[i] == event_type):
            r.append(i)
    return np.array(r)


@nb.jit
def marker_list_to_range(marker_list):
    r = list()
    for i in range(len(marker_list) - 1):
        r.append(marker_list[i])
        r.append(marker_list[i+1] - 1)
    return np.array(r).reshape(len(r)/2, 2)


@nb.jit
def marker_list_to_range(marker_list):
    r = list()
    for i in range(len(marker_list) - 1):
        r.append(marker_list[i])
        r.append(marker_list[i+1] - 1)
    return np.array(r).reshape(len(r)/2, 2)


count_marker(c, m, 4, 1)

frame_marker_list = find_marker(c, e, 4)
n_frames = len(frame_marker_list) - 1

line_start_marker_list = find_marker(c, e, 1)
line_stop_marker_list = find_marker(c, e, 2)
line_marker_range = marker_list_to_range(line_start_marker_list)
n_lines = len(line_marker_range) / n_frames


def make_image(
        c, m, t, e,
        n_frames, n_lines, pixel_duration,
        channels,
        frame_marker=4,
        line_start=1,
        line_stop=2,
        n_pixel=None,
        tac_coarsening=32, n_tac_max=2**15):
    if n_pixel is None:
        n_pixel = n_lines  # assume squared image

    n_tac = n_tac_max / tac_coarsening
    image = np.zeros((n_frames, n_lines, n_pixel, n_tac))
    # iterate through all photons in a line and add to image

    frame = -1
    current_line = 0
    time_start_line = 0
    invalid_range = True
    mask_invalid = True
    for ci, mi, ti, ei in zip(c, m, t, e):
        if ei == 1:  # marker
            if ci == frame_marker:
                frame += 1
                current_line = 0
                if frame < n_frames:
                    continue
                else:
                    break
            elif ci == line_start:
                time_start_line = ti
                invalid_range = False
                continue
            elif ci == line_stop:
                invalid_range = True
                current_line += 1
                continue
        elif ei == 0:  # photon
            if ci in channels and (not invalid_range or not mask_invalid):
                pixel = int((ti - time_start_line) // pixel_duration[current_line])
                if pixel < n_pixel:
                    tac = mi / tac_coarsening
                    image[frame, current_line, pixel, tac] += 1
    return image



image = make_image(
    c,
    m,
    t,
    e,
    n_frames,
    n_lines,
    pixel_duration,
    channels=np.array([0, 1])
)


p.imshow(np.log(image.sum(axis=(0, 3))), cmap='inferno')
p.show()


mt_special_events = t[special_events]

p.plot(mt_special_events)
p.show()
t_0_i
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
