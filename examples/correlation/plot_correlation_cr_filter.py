"""
===============================
Count rate filtered correlation
===============================

Low count rate regions in a TTTR file can be discriminated. The example below
displays the correlation analysis of a single molecule FRET experiment. Note,
the background has a significant contribution to the unfiltered correlation
function. The example uses a sliding time-window (TW). TWs with less than a
certain amount of photons are discriminated by the selection.

Such a filter can be used to remove the background in a single-molecule experiment
that decreased the correlation amplitude.

"""
import matplotlib.pylab as plt
import tttrlib

"""
First, we do a normal correlation where all data is correlated unfiltered.  
"""

#  Read the data data
tttr_data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

fig, ax = plt.subplots(1, 2, sharex='col', sharey='row')

# correlation settings: default algorithm, n_bins and n_casc defines the settings of the multi-tau
# correlation steps, make_fine: false = no microtime information used
settings = {
    "method": "default",
    "n_bins": 3,
    "n_casc": 27,
    "make_fine": False
}
correlator = tttrlib.Correlator(
    tttr=tttr_data,
    channels=([0], [8]),  # green correlation
    **settings
)

# Plot the raw/unfiltered correlations
ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pG/sG"
)

ax[0].set_xlabel('corr. time / ms')
ax[0].set_ylabel('Correlation Amplitude')

correlator = tttrlib.Correlator(
    tttr=tttr_data,
    channels=([8, 0], [1, 9]),  # green-red cross-correlation
    **settings
)

ax[0].semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pG,sG/pR,sR"
)

ax[0].set_title('RAW correlations')
ax[0].legend()

"""
To reduce the contribution of scattered light on the correlation: Regions in 
the TTTR stream that have a minimum count rate are selected. 
"""

# This is a selection where at most 60 photons in any time window of
# 10 ms are allowed
filter_options = {
    'n_ph_max': 60,
    'time_window': 10.0e-3,  # = 10 ms (macro_time_resolution is in seconds)
    'invert': True  # set invert to True to select TW with more than 60 ph
}

"""
Part of the photon stream is selected either by creating a list of indices that
refer to TTTR events. These indices are used to create a new TTTR object by slicing 
the original TTTR object. Below two options to create such a selection is 
illustrated.
"""

# Selections (indices) can be selected either using
selection_idx = tttrlib.selection_by_count_rate(
        time=tttr_data.macro_times,
        macro_time_calibration=tttr_data.header.macro_time_resolution,
        **filter_options
    )

selection_idx = tttr_data.get_selection_by_count_rate(**filter_options)
tttr_selection = tttr_data[selection_idx]

"""
Alternatively, a new TTTR object can be created directly.
"""

tttr_selection = tttr_data.get_tttr_by_count_rate(**filter_options)

correlator = tttrlib.Correlator(
    channels=([0], [8]),  # green correlation
    tttr=tttr_selection,
    **settings
)

ax[1].semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pG/sG"
)

ax[1].set_xlabel('corr. time / ms')

correlator = tttrlib.Correlator(
    channels=([0, 8], [1, 9]),  # green-red cross-correlation
    tttr=tttr_selection,
    **settings
)

ax[1].semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pG,sG/pR,sR"
)

ax[1].legend()
ax[1].set_title('Count rate filtered correlations')

plt.show()
