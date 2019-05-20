import pylab as p
import numpy as np

import tttrlib

data = tttrlib.TTTR('../../examples/BH/BH_SPC132.spc', 'SPC-130')
mt = data.get_macro_time()

green_indeces = data.get_selection_by_channel(np.array([0, 8]))
red_indeces = data.get_selection_by_channel(np.array([1, 9]))

fig, ax = p.subplots(2, 1, sharex=True, sharey=False)

p.setp(ax[0].get_xticklabels(), visible=False)
ax[0].plot(tttrlib.histogram_trace(mt[green_indeces], 30000), 'g')

ax[1].plot(tttrlib.histogram_trace(mt[red_indeces], 30000), 'r')
ax[1].invert_yaxis()

p.subplots_adjust(left=None, bottom=None, right=None, top=None, wspace=None, hspace=0)

p.show()

