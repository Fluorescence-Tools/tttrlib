"""
============================
Normal multi-tau correlation
============================

As described previously there are several analogous possibilities to compute
correlations. Two possibilities are shown in the example below.

Analysing such correlation functions informs on diffusion and fast kinetics. Such
correlation functions can be analyzed by dedicated open tools for fluorescence
such as `ChiSurf <https://github.com/fluorescence-tools/chisurf/>`_,
`PyCorrFit <https://github.com/FCS-analysis/PyCorrFit>`_, and
`PAM <https://github.com/fluorescence-tools/pam>`_ or generic curve analysis
software.


"""
import matplotlib.pylab as plt
import tttrlib
import numpy as np


fig, ax = plt.subplots(sharex='col', sharey='row')

#  Read the data data
data = tttrlib.TTTR('../../tttr-data/bh/bh_spc132.spc', 'SPC-130')

# Correlator settings, if the same settings are used repeatedly it is useful to define them once
settings = {
    "method": "default",
    "n_bins": 7,  # n_bins and n_casc defines the settings of the multi-tau
    "n_casc": 27,  # correlation algorithm
    "make_fine": False  # Do not use the microtime information
}

# Create correlator
# Caution: x-axis in units of macro time counter
# tttrlib.Correlator is unaware of the calibration in the TTTR object
correlator = tttrlib.Correlator(**settings)

# Select the green channels (channel number 0 and 8)
ch1_indices = data.get_selection_by_channel([0])
ch2_indices = data.get_selection_by_channel([8])

# Select macro times for Ch1 and Ch2 and create array of weights
# Note: the weights are set to 1, i.e. no weighting is used
t = data.macro_times
t1 = t[ch1_indices]
w1 = np.ones_like(t1, dtype=np.float)
t2 = t[ch2_indices]
w2 = np.ones_like(t2, dtype=np.float)
correlator.set_events(t1, w1, t2, w2)

# scale the x-axis to have units in milliseconds (common unit in FCS)
x = correlator.curve.x * (data.header.macro_time_resolution)
y = correlator.curve.y

plt.semilogx(x, y, label="Gp/Gs")

# green-red cross correlation (red channels 1 and 9)
# TODO: verify "if the correlator is generated using the architecture below,
# the macro time units are known and no rescaling of the y-axis needs to be performed"
correlator = tttrlib.Correlator(
    tttr=(
        tttrlib.TTTR(data, data.get_selection_by_channel([0, 8])),
        tttrlib.TTTR(data, data.get_selection_by_channel([1, 9]))
    ),
    **settings
)

# no need to scale axis - correlator aware of macro time units
ax.semilogx(
    correlator.curve.x,
    correlator.curve.y,
    label="Gp,Gs/Rp,Rs"
)

# red channel correlation
correlator = tttrlib.Correlator(
    channels=([9], [1]),
    tttr=data,
    **settings
)

ax.semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pR,sR"
)

# Reverse cross-correlation: red-green
correlator = tttrlib.Correlator(
    channels=([9, 1], [0, 8]),
    tttr=data,
    **settings
)

ax.semilogx(
    correlator.x_axis,
    correlator.correlation,
    label="pRsR,pGsG"
)

# Show results
ax.set_xlabel('corr. time / sec')
ax.set_ylabel('Correlation Amplitude')
ax.legend()

plt.show()
