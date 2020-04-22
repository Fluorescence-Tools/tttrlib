import numpy as np
import tttrlib
import pylab as p

data = tttrlib.TTTR('../../test/data/BH/BH_SPC132.spc', 'SPC-130')
mt = data.get_macro_time()
time_in_ms = 1.0
tw_ranges = data.get_time_window_ranges(
    minimum_window_length=time_in_ms,
    minimum_number_of_photons_in_time_window=20
)
start_stop = tw_ranges.reshape([len(tw_ranges) // 2, 2])

sel = list()
for start, stop in start_stop:
    sel += range(start, stop)
sel = np.array(sel)

green_indeces = data.get_selection_by_channel([0, 8])
red_indeces = data.get_selection_by_channel([1, 9])

fig, ax = p.subplots(3, sharex=True, sharey=False)

p.setp(ax[0].get_xticklabels(), visible=False)
ax[0].plot(tttrlib.compute_intensity_trace(mt[green_indeces], 30000), 'g')

ax[1].plot(tttrlib.compute_intensity_trace(mt[red_indeces], 30000), 'r')
ax[1].invert_yaxis()

ax[2].plot(tttrlib.compute_intensity_trace(mt[green_indeces], 30000), 'g')
ax[2].plot(tttrlib.compute_intensity_trace(mt[red_indeces], 30000), 'r')
ax[2].plot(tttrlib.compute_intensity_trace(mt[sel], 30000), 'b')

p.subplots_adjust(
    left=None, bottom=None, right=None, top=None,
    wspace=None, hspace=0
)

p.show()