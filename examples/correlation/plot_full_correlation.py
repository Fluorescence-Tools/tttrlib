"""
================
Full correlation
================

When processes faster than the macro time clock are of interest, the micro time
and the macro time can be combined into a united time axis. Using the combined
time axis a so called full correlation can be performed using continuous wave excitation
in contrast to the usually performed pulsed excitation.

The example below illustrates how a full correlation can be computed. Note, in
the example the full correlation is computed for a sample that was measured in a
pulsed excitation experiment. However, the same procedure can be applied to cw data.

"""
import tttrlib
import matplotlib.pylab as plt
import numpy as np

data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

# create TTTR objects for the two channels - either in a two approach:
ch1_indices = data.get_selection_by_channel([8, 1])
tttr_ch1 = tttrlib.TTTR(data, ch1_indices)
# or using just a single line of code:
tttr_ch2 = data.get_tttr_by_channel([0, 9])

# correlate with the new TTTR objects
full_corr_settings = {
    "method": 'default',
    "n_casc": 37,  # n_bins and n_casc defines the settings of the multi-tau
    "n_bins": 7,  # correlation algorithm
    "make_fine": True  # Use the microtime information ( also called "fine" correlation)
}

correlator = tttrlib.Correlator(
    **full_corr_settings,
    tttr=(tttr_ch1, tttr_ch2),
)

x = correlator.x_axis
y = correlator.correlation

# Show the result
fig, ax = plt.subplots(1, 1, sharex='col', sharey='row')
ax.semilogx(x, y, label="GpRp/GsRs - full")

# the above is equivalent to the following
correlator_ref = tttrlib.Correlator(**full_corr_settings)  # read the settings from above
t1, t2 = data.macro_times[ch1_indices], tttr_ch2.macro_times  # Get the macrotime information
mt1, mt2 = data.micro_times[ch1_indices], tttr_ch2.micro_times  # Get the microtime information
w1, w2 = np.ones_like(t1, dtype=np.float), np.ones_like(t2, dtype=np.float)  # Generate weights
# Note: the weights are all set to 1 here, i.e. no weighting
correlator_ref.set_macrotimes(t1, t2)
correlator_ref.set_weights(w1, w2)
n_microtime_channels = data.get_number_of_micro_time_channels()
correlator_ref.set_microtimes(mt1, mt2, n_microtime_channels)
x_ref = correlator_ref.x_axis
y_ref = correlator_ref.correlation
# x-axis needs to be multiplied with microtime resolution to obtain correct timing:
x_ref *= data.header.micro_time_resolution

# add the result
ax.semilogx(x_ref, y_ref, label="GpRp/GsRs - full 2")

"""
For comparison a normal correlation (without considering the 
micro times).
"""

correlator_ref = tttrlib.Correlator(
    tttr=(tttr_ch1, tttr_ch2),
    n_casc=25,
    n_bins=7
)

x_normal = correlator_ref.x_axis
y_normal = correlator_ref.correlation

ax.semilogx(x_normal, y_normal, label="GpRp/GsRs - normal")
ax.set_xlabel('corr. time / sec')
ax.set_ylabel('Correlation Amplitude')
ax.legend()

plt.show()
