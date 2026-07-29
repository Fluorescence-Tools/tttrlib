r"""
==================================
Fit25: select from fixed lifetimes
==================================

``Fit25`` compares four candidate lifetimes and returns the lifetime that best
describes a polarization-resolved Jordi-format decay. Use it when candidate
lifetimes are known from calibration or a model grid and the question is which
candidate explains the measured photons best.

The input parameter vector is:

``[tau1, tau2, tau3, tau4, gamma, r0]``
   The four ``tau`` entries are candidates. ``gamma`` is the scattered fraction
   and ``r0`` is the fundamental anisotropy. The selected lifetime is returned
   in ``result["x"][0]``.
"""

import matplotlib.pyplot as plt
import numpy as np

import tttrlib


def make_jordi_irf(n_channels=32, period=32.0):
    time_axis = np.linspace(0.0, period, n_channels * 2)
    irf = (
        np.exp(-0.5 * ((time_axis - 2.0) / 0.25) ** 2)
        + np.exp(-0.5 * ((time_axis - 18.0) / 0.25) ** 2)
    )
    return irf.astype(np.float64), time_axis


irf, time_axis = make_jordi_irf()
background = np.zeros_like(irf)
dt = time_axis[1] - time_axis[0]

data = np.array(
    [
        0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0, 1, 1, 1, 2,
        0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    ],
    dtype=np.int32,
)

fit25 = tttrlib.DecayFit2(
    'fit25',
    tttrlib.setup_vector('fit25', dt=dt, period=32.0, g_factor=1.0, l1=0.1, l2=0.1,
                         convolution_stop=len(irf) // 2 - 1),
    irf.tolist())

problem = tttrlib.DecayFitProblem(2, len(irf) // 2, dt)
problem.irf = tttrlib.VectorDouble(np.asarray(irf, dtype=float).tolist())
problem.background = tttrlib.VectorDouble(np.asarray(background, dtype=float).tolist())
problem.data = tttrlib.VectorDouble(np.asarray(data, dtype=float).tolist())

candidate_lifetimes = np.array([0.5, 1.0, 2.0, 4.0])
initial = [*candidate_lifetimes, 0.02, 0.38]
# The four candidates and the scatter fraction are scored; r0 is held.
constraints = tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, 0, 0, 0, -1, -1]))
outcome = fit25.fit(initial, constraints, problem)
result = {"x": np.asarray(outcome.parameters), "twoIstar": outcome.objective}

plt.plot(data, label="data")
plt.plot(np.asarray(problem.model), label="best fit25 model")
plt.xlabel("microtime channel")
plt.ylabel("counts")
plt.legend()
plt.show()

print("Fit25 lifetime selection")
print("========================")
print(f"candidates: {candidate_lifetimes}")
print(f"selected tau: {result['x'][0]:.1f} ns")
print(f"twoIstar: {result['twoIstar']:.3f}")
# This model classifies rather than measures, so which candidate won is the
# answer — and the registry reports it directly.
named = tttrlib.results_as_dict('fit25', list(outcome.results))
print(f"winning candidate: index {named['selected_index']}")
