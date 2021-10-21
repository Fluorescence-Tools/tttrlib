"""
==============================
Maximum likelihood: DecayFit23
==============================

Usage
^^^^^
"""
import numpy as np
import pylab as p

import tttrlib

#%%
# First, we define our instrument response function (IRF) and the data array. Both,
# the IRF and the data are stacked arrays that contain the fluorescence decay histograms
# of the parallel and perpendicular detection channels.

irf = np.array(
    [0, 0, 0, 260, 1582, 155, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 22, 1074, 830, 10, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0], dtype=np.float64
)
data = np.array(
    [0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
     1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
)


#%%
# Second, a set of constant parameters needs to be specified that define instrumental
# parameters. To analyze the decay the time-resolution of the histogram bins (dt),
# the g factor that corrects for the sensitivity of the parallel and perpendicular
# detection channel to detect light, two constants l1 and l2 that quantify the mixing
# of the parallel and perpendicular detection channel, and the excitation period need
# to be specified. The excitation periord (in nanoseconds) accounts for periodic
# excitation (high repetition rates). Moreover the convolution stop channel that
# defines the convolution range is needed. The parameter ``background`` is an array
# that defines a non-constant offset that gets scaled by :math:`\gamma` (the fraction
# of scattered light).

settings = {
    'dt': 0.5079365079365079,
    'g_factor': 1.0, 'l1': 0.1, 'l2': 0.2,
    'convolution_stop': 31,
    'irf': irf,
    'period': 16.0,
    'background': np.zeros_like(irf)
}

#%%
# The settings are used to initialize a instance of the class ``Fit23``. A dataset
# is fitted by calling an instance of ``Fit23`` using the data, an array of the initial
# values of the fitting parameters, and an array that specifies which parameters are
# fixed.

fit23 = tttrlib.Fit23(**settings)

tau, gamma, r0, rho = 2.2, 0.01, 0.38, 1.22
x0 = np.array([tau, gamma, r0, rho])
fixed = np.array([0, 1, 1, 0])
r = fit23(
    data=data,
    initial_values=x0,
    fixed=fixed
)

#%%
# Calling an ``Fit23`` instance returns a dictionary that contains an array with the
# fit results ``x``, an array that of the fixed values ``fixed``, and a floating
# point number ``twoIstar`` that quantifies the goodness of the optimized model.


p.plot(fit23.data, label='data')
p.plot(fit23.model, label='model')
p.show()

print("Results")
print("=======")
print("tau: {:.2f}".format(r['x'][0]))
print("rho: {:.2f}".format(r['x'][3]))
