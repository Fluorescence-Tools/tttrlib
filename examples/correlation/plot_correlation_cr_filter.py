"""
===================================
Count rate filtered correlation - 1
===================================

Low count rate regions of a TTTR file can be discriminated. The example below
displays the correlation analysis of a single molecule FRET experiment. Note,
the background has a significant contribution to the unfiltered correlation
function. The example uses a sliding time-window (TW). TWs with less than a
certain amount of photons are discriminated by the selection.


Such a filter can be used to remove the background in a single-molecule experiment
that decreased the correlation amplitude.

"""
import pylab as p
import tttrlib
import numpy as np


#  Read the data data
data = tttrlib.TTTR('../../test/data/bh/bh_spc132.spc', 'SPC-130')

fig, ax = p.subplots(nrows=1, ncols=2)

correlator = tttrlib.Correlator(
    tttr=data,
    channels=([0], [8]),  # green correlation
)

# Plot the raw/unfiltered correlations
ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation
)
correlator = tttrlib.Correlator(
    tttr=data,
    channels=([0, 8], [1, 9]),  # green-red correlation
)
ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation
)

# This is a selection where at most 60 photons in any time window of
# 10 ms are allowed
filter_options = {
    'n_ph_max': 60,
    'time_window': 10.0e6,  # = 10 ms (macro_time_resolution is in ns)
    'time': data.macro_times,
    'macro_time_calibration': data.header.macro_time_resolution,
    'invert': True  # set invert to True to select TW with more than 60 ph
}
data_selection = data[tttrlib.selection_by_count_rate(**filter_options)]
correlator = tttrlib.Correlator(
    channels=([0], [8]),
    tttr=data_selection
)


ax[1].semilogx(
    correlator.x_axis,
    correlator.correlation
)

# green-red cross-correlation
correlator = tttrlib.Correlator(
    channels=([0, 8], [9, 1]),
    tttr=data_selection
)
ax[1].semilogx(
    correlator.x_axis,
    correlator.correlation
)

p.show()
