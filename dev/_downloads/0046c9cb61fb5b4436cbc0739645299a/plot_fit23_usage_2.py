"""
===============================
MLE fit23: Usage example 2
===============================

1) Compute model function
2) Simulate data for computed model function
3) Fit simulated data with model function

"""
import tttrlib
import numpy as np
import scipy.stats
import pylab as p


def model_irf(
        n_channels: int = 256,
        period: float = 32,
        irf_position_p: float = 2.0,
        irf_position_s: float = 18.0,
        irf_width: float = 0.25
):
    time_axis = np.linspace(0, period, n_channels * 2)
    irf_np = scipy.stats.norm.pdf(time_axis, loc=irf_position_p, scale=irf_width) + \
             scipy.stats.norm.pdf(time_axis, loc=irf_position_s, scale=irf_width)
    return irf_np, time_axis


# setup some parameters
n_channels = 128
n_corrections = 5
n_photons = 120
irf_position_p = 2.0
irf_position_s = 18.0
irf_width = 0.25
period, g, l1, l2, conv_stop = 32, 1.0, 0.1, 0.1, n_channels // 2 - 1
tau, gamma, r0, rho = 2.0, 0.01, 0.38, 1.2
np.random.seed(0)

irf_np, time_axis = model_irf(
    n_channels=n_channels,
    period=period,
    irf_position_p=irf_position_p,
    irf_position_s=irf_position_s,
    irf_width=irf_width
)
dt = time_axis[1] - time_axis[0]
conv_stop = min(len(time_axis), conv_stop)
param = np.array([tau, gamma, r0, rho])
corrections = np.array([period, g, l1, l2, conv_stop])

# Build the fit once from the instrument description and the IRF.
bg = np.zeros_like(time_axis)
fit23 = tttrlib.DecayFit2(
    'fit23',
    tttrlib.setup_vector('fit23', dt=dt, period=period, g_factor=g, l1=l1, l2=l2,
                         convolution_stop=int(conv_stop)),
    irf_np.tolist())

problem = tttrlib.DecayFitProblem(2, len(irf_np) // 2, dt)
problem.irf = tttrlib.VectorDouble(irf_np.tolist())
problem.background = tttrlib.VectorDouble(bg.tolist())

# model_curve gives the decay independent of any data, which is what generating
# a synthetic measurement needs.
model = np.asarray(fit23.model_curve(param, problem))
data = np.random.poisson(model * n_photons)
problem.data = tttrlib.VectorDouble(np.asarray(data, dtype=float).tolist())

# Fit from a deliberately wrong start, with only the lifetime free
# (0 = free, -1 = held).
tau, gamma, r0, rho = 4., 0.01, 0.38, 1.5
outcome = fit23.fit(
    [tau, gamma, r0, rho],
    tttrlib.DecayFitConstraints(tttrlib.VectorInt32([0, -1, -1, -1])),
    problem)
chi2 = outcome.objective
x = np.asarray(outcome.parameters)

m = np.asarray(problem.model)
p.plot(m)
p.plot(data)
p.plot(irf_np / max(irf_np) * max(data))
p.show()
