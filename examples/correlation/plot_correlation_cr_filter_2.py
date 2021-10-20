"""
===================================
Count rate filtered correlation - 2
===================================
Scan count rate filter and compute stack of correlation
functions for different mean count rates in time windows.
Potential application: filter aggregates, see also
PAID correlation (Laurence???)
TODO: You mean this:https://www.osapublishing.org/ol/viewmedia.cfm?uri=ol-31-6-829&seq=0&html=true

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
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

fig, ax = p.subplots(nrows=1, ncols=2)

#  Generate raw correlations of the data w/o any filters
correlator = tttrlib.Correlator(
    tttr=data,
    n_bins=1,  # n_bins and n_casc defines the settings of the multi-tau
    n_casc=30,  # correlation algorithm
    channels=([0], [8])  # green correlation
)

# Plot the raw/unfiltered correlations of the green channels
ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pG/sG"
)

correlator = tttrlib.Correlator(
    tttr=data,
    n_bins=1,
    n_casc=30,
    channels=([0, 8], [1, 9])  # green-red cross-correlation
)

# Add the cross-correlation
ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pG,sG/pR,sR"
)

# Next, generate the filtered correlations.
# This is a selection where at most 60 photons in any time window of
# 10 ms are allowed
filter_options = {
    'n_ph_max': 60,
    'time_window': 10.0e-3,  # = 10 ms (macro_time_resolution is in ns)
    'time': data.macro_times,
    'macro_time_calibration': data.header.macro_time_resolution,
    'invert': True  # set invert to True to select TW with more than 60 ph
}

correlations = list()
for tw in np.logspace(-3.5, 1, 50):  # 0.005 -> 10 in 50 steps (50 ms -> 10 s)
    filter_options['time_window'] = tw
    correlator = tttrlib.Correlator(
        n_bins=3,
        n_casc=30,
        channels=([0], [8]),
        tttr=data[tttrlib.selection_by_count_rate(**filter_options)]
    )
    correlations.append(correlator.correlation)

# TODO: x-axis = correlation time as "index", y-axis = time window size
# why log-scale? numbers/indices are plotted not correlation time
# what should be seen from this image?
p.imshow(np.log(np.array(correlations)))

# TODO: I think it would be better to show few selected correlations curves
#ax[1].semilogx(
#    correlator.x_axis,
#    correlations[49]
#)

p.show()
