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

# compute a model function that is later used as "data"
model = np.zeros_like(time_axis)
bg = np.zeros_like(time_axis)
tttrlib.DecayFit23.modelf(param, irf_np, bg, dt, corrections, model)
# add poisson noise to model and use as data
data = np.random.poisson(model * n_photons)

# create MParam structure that contains all parameters for fitting
m_param = tttrlib.CreateMParam(
    irf=irf_np,
    background=bg,
    data=data.astype(np.int32),
    corrections=corrections,
    dt=dt
)

tau, gamma, r0, rho = 4., 0.01, 0.38, 1.5
bifl_scatter = -1
p_2s = 0
x = np.zeros(8, dtype=np.float64)
x[:6] = [tau, gamma, r0, rho, bifl_scatter, p_2s]

# test fitting
fixed = np.array([0, 1, 1, 1], dtype=np.int16)
chi2 = tttrlib.DecayFit23.fit(x, fixed, m_param)

m = np.array([m for m in m_param.get_model()])
p.plot(m)
p.plot(data)
p.plot(irf_np / max(irf_np) * max(data))
p.show()
