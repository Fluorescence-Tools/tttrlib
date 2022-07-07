"""
=================
Lifetime analysis
=================
Use an MLE to determine mean lifetime in a pixel
"""
import tttrlib

import json
import numpy as np
import pylab as plt

filename = '../../tttr-data/imaging/pq/ht3/crn_clv_img.ht3'
filename_irf = '../../tttr-data/imaging/pq/ht3/crn_clv_mirror.ht3'
channels_green = [0, 2]

data = tttrlib.TTTR(filename)
irf_tttr = tttrlib.TTTR(filename_irf)
irf_green: tttrlib.TTTR = irf_tttr[irf_tttr.get_selection_by_channel(channels_green)]

binning_factor = 16
irf, t = irf_green.get_microtime_histogram(binning_factor)

# %%
# Settings for MLE
settings = {
    'dt': data.header.micro_time_resolution * 1e9,
    'g_factor': 1.0, 'l1': 0.1, 'l2': 0.2,
    'convolution_stop': 31,
    'irf': irf,
    'period': 16.0,
    'background': np.zeros_like(irf)
}

# %%
# The settings are used to initialize a instance of the class ``Fit23``. A dataset
# is fitted by calling an instance of ``Fit23`` using the data, an array of the initial
# values of the fitting parameters, and an array that specifies which parameters are
# fixed.
fit23 = tttrlib.Fit23(**settings)

# %%
# Create a new CLSM Image. This image will be used as a template for the green and red image.
# This avoids passing through the TTTR screen multiple times. The frame line, and pixel locations
# will be copied for the green and red image from this template.
clsm_template = tttrlib.CLSMImage(data)
clsm_green = tttrlib.CLSMImage(
    source=clsm_template,
    channels=channels_green
)

fit23 = tttrlib.Fit23(**settings)
tau, gamma, r0, rho = 2.2, 0.01, 0.38, 1.22
x0 = np.array([tau, gamma, r0, rho])
fixed = np.array([0, 1, 1, 0])
r = fit23(
    data=data,
    initial_values=x0,
    fixed=fixed
)
data = fit23.data
model = fit23.model

fig, ax = plt.subplots(nrows=2, ncols=2)
ax[0, 0].set_title('Green intensity')
ax[0, 1].set_title('Red intensity')
ax[1, 0].set_title('Mean green fl. lifetime')
ax[1, 1].set_title('Pixel histogram')
ax[1, 1].set_xlabel('tauG / ns')
ax[1, 1].set_ylabel('log(Sg/Sr')
plt.show()
