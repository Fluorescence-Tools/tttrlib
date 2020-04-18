import pylab as p
import numpy as np

import tttrlib

data = tttrlib.TTTR('../../test/data/BH/BH_SPC132.spc', 'SPC-130')
mt = data.get_macro_time()

green_indeces = data.get_selection_by_channel([0, 8])
red_indeces = data.get_selection_by_channel([1, 9])

fig, ax = p.subplots(3, 1, sharex=True, sharey=False)

p.setp(ax[0].get_xticklabels(), visible=False)
green_trace = tttrlib.histogram_trace(mt[green_indeces], 480000)
red_trace = tttrlib.histogram_trace(mt[red_indeces], 480000)
m = min(len(green_trace), len(red_trace))
SgSr_ratio = (green_trace[:m] / red_trace[:m])
SgSr_ratio[np.where((green_trace[:m] + red_trace[:m]) < 30)] *= 0

ax[0].plot(green_trace, 'g', label='Green signal, Sg')
ax[1].plot(red_trace, 'r', label='Red signal, Sr')
ax[1].invert_yaxis()
ax[0].legend()
ax[1].legend()
ax[2].plot(SgSr_ratio, 'b', label='Sg/Sr')
ax[2].legend()
p.subplots_adjust(left=None, bottom=None, right=None, top=None, wspace=None, hspace=0)

p.show()

