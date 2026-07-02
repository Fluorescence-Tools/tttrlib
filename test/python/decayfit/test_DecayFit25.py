from __future__ import division

import unittest
import numpy as np

import tttrlib
from misc.compute_irf import model_irf


irf, time_axis = model_irf(
    n_channels=64,
    period=32.,
    irf_position_p=2.0,
    irf_position_s=18.0,
    irf_width=0.25
)
bg = np.zeros_like(irf) + 0.2


class Tests(unittest.TestCase):

    def test_fit25(self):
        n_channels = 32
        irf_position_p = 2.0
        irf_position_s = 18.0
        irf_width = 0.25
        irf, time_axis = model_irf(
            n_channels=n_channels,
            period=32.,
            irf_position_p=irf_position_p,
            irf_position_s=irf_position_s,
            irf_width=irf_width
        )
        dt = time_axis[1] - time_axis[0]
        data = np.array(
            [0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
            1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )
        bg = np.zeros_like(irf)
        settings = {
            'dt': dt,
            'g_factor': 1.0,
            'l1': 0.1,
            'l2': 0.1,
            'period': 32.0,
            'convolution_stop': 31,
            'irf': irf,
            'background': bg,
            'verbose': False
        }
        fit25 = tttrlib.Fit25(**settings)
        tau1, tau2, tau3, tau4 = 0.5, 1.0, 2.0, 4.0
        gamma, r0 = 0.02, 0.38
        x = np.array([tau1, tau2, tau3, tau4, gamma, r0])
        fixed = np.array([0, 0, 0, 0, 1, 1])
        r = fit25(
            data=data,
            initial_values=x,
            fixed=fixed
        )
        best_tau = r['x'][0]
        self.assertEqual(best_tau, 2.0)
