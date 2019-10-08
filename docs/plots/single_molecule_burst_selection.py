import numpy as np
import tttrlib
import pylab as p

data = tttrlib.TTTR('../../data/BH/BH_SPC132.spc', 'SPC-130')
mt = data.get_macro_time()
tw_ranges = data.get_ranges_by_count_rate(50000, -1, 20, -1)


sel = list()
for start, stop in zip(tw_ranges[::2], tw_ranges[1::2]):
    sel += range(start, stop)
sel = np.array(sel)

green_indeces = data.get_selection_by_channel(np.array([0, 8]))
red_indeces = data.get_selection_by_channel(np.array([1, 9]))

fig, ax = p.subplots(3, 1, sharex=True, sharey=False)

p.setp(ax[0].get_xticklabels(), visible=False)
ax[0].plot(tttrlib.histogram_trace(mt[green_indeces], 30000), 'g')

ax[1].plot(tttrlib.histogram_trace(mt[red_indeces], 30000), 'r')
ax[1].invert_yaxis()

ax[2].plot(tttrlib.histogram_trace(mt[green_indeces], 30000), 'g')
ax[2].plot(tttrlib.histogram_trace(mt[red_indeces], 30000), 'r')
ax[2].plot(tttrlib.histogram_trace(mt[sel], 30000), 'b')

p.subplots_adjust(left=None, bottom=None, right=None, top=None, wspace=None, hspace=0)

p.show()