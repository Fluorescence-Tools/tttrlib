r"""
==================================
Fit24: bi-exponential decay model
==================================

``Fit24`` fits a bi-exponential fluorescence decay model in Jordi format. Use it
when the data contain two lifetime components and anisotropy is not part of the
model.

The optimized parameter vector is:

``[tau1, gamma, tau2, A2, offset]``
   ``tau1`` and ``tau2`` are the two lifetimes, ``A2`` is the amplitude of the
   second component, ``gamma`` is the scattered/background fraction, and
   ``offset`` is a constant background term.

This example creates deterministic synthetic data, fits it with the reusable
``Fit24`` class, and prints the recovered parameters.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib


def make_jordi_irf(n_channels=64, period=32.0):
    """Return a two-channel Jordi-format IRF and its time axis."""
    time_axis = np.linspace(0.0, period, n_channels * 2)
    irf = (
        np.exp(-0.5 * ((time_axis - 2.0) / 0.25) ** 2)
        + np.exp(-0.5 * ((time_axis - 18.0) / 0.25) ** 2)
    )
    return irf.astype(np.float64), time_axis


np.random.seed(0)

irf, time_axis = make_jordi_irf()
background = np.zeros_like(irf) + 0.2
dt = time_axis[1] - time_axis[0]
period = 32.0
convolution_stop = len(irf) // 2 - 1
corrections = np.array([period, 1.0, 0.1, 0.1, convolution_stop])

true_parameters = np.array([4.0, 0.01, 0.5, 0.9, 1.0])
probability_model = np.zeros_like(irf)
tttrlib.DecayFit24.modelf(
    true_parameters,
    irf,
    background,
    dt,
    corrections,
    probability_model,
)

data = np.random.poisson(probability_model * 500_000 / probability_model.sum())

fit24 = tttrlib.Fit24(
    dt=dt,
    irf=irf,
    background=background,
    period=period,
    convolution_stop=convolution_stop,
)

initial = np.array([3.5, 0.02, 0.7, 0.5, 1.0])
fixed = np.array([0, 0, 0, 0, 0], dtype=np.int16)
result = fit24(data=data, initial_values=initial, fixed=fixed, include_model=True)

plt.plot(data, label="synthetic data")
plt.plot(result["model"], label="Fit24 model")
plt.xlabel("microtime channel")
plt.ylabel("counts")
plt.legend()
plt.show()

print("Fit24 recovered parameters")
print("==========================")
print(f"tau1:   {result['x'][0]:.3f} ns")
print(f"gamma:  {result['x'][1]:.4f}")
print(f"tau2:   {result['x'][2]:.3f} ns")
print(f"A2:     {result['x'][3]:.3f}")
print(f"offset: {result['x'][4]:.3f}")
print(f"twoIstar: {result['twoIstar']:.3f}")
