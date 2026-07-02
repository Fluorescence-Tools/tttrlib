import numpy as np
import scipy.stats


def model_irf(
        n_channels: int = 256,
        period: float = 32,
        irf_position_p: float = 2.0,
        irf_position_s: float = 18.0,
        irf_width: float = 0.25
):
    time_axis = np.linspace(0, period, n_channels * 2)
    irf_np = scipy.stats.norm.pdf(time_axis, loc=irf_position_p,
                                  scale=irf_width) + \
             scipy.stats.norm.pdf(time_axis, loc=irf_position_s,
                                  scale=irf_width)
    return irf_np, time_axis


