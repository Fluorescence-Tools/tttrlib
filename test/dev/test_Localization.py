from __future__ import division

import unittest
import numpy as np
from sys import platform
import scipy.stats

import tttrlib
from misc.compute_irf import model_irf


class Tests(unittest.TestCase):

    def test_fconv(self):
        period = 12.0
        lifetime_spectrum = np.array([1.0, 4.1])
        irf, time_axis = model_irf(
            n_channels=32,
            period=period,
            irf_position_p=2.0,
            irf_position_s=2.0,
            irf_width=0.25
        )
        dt = time_axis[1] - time_axis[0]

        irf += 0.0
        model_ref = np.array([3.84934940e-15, 1.28147164e-12, 2.39814805e-10, 2.51323110e-08,
                              1.48317225e-06, 4.95259533e-05, 9.43470390e-04, 1.03962151e-02,
                              6.77549488e-02, 2.70166361e-01, 6.91596363e-01, 1.21273196e+00,
                              1.58572312e+00, 1.71429416e+00, 1.69244968e+00, 1.62476106e+00,
                              1.55186473e+00, 1.48146393e+00, 1.41421431e+00, 1.35001612e+00,
                              1.28873217e+00, 1.23023021e+00, 1.17438394e+00, 1.12107282e+00,
                              1.07018175e+00, 1.02160088e+00, 9.75225335e-01, 9.30955008e-01,
                              8.88694331e-01, 8.48352077e-01, 8.09841158e-01, 7.73078441e-01,
                              7.37984567e-01, 7.04483778e-01, 6.72503757e-01, 6.41975467e-01,
                              6.12833009e-01, 5.85013471e-01, 5.58456801e-01, 5.33105670e-01,
                              5.08905353e-01, 4.85803609e-01, 4.63750568e-01, 4.42698624e-01,
                              4.22602332e-01, 4.03418311e-01, 3.85105148e-01, 3.67623310e-01,
                              3.50935060e-01, 3.35004373e-01, 3.19796859e-01, 3.05279689e-01,
                              2.91421526e-01, 2.78192454e-01, 2.65563916e-01, 2.53508649e-01,
                              2.42000631e-01, 2.31015019e-01, 2.20528099e-01, 2.10517232e-01,
                              2.00960808e-01, 1.91838198e-01, 1.83129708e-01, 1.74816540e-01])
        model_fconv = np.zeros_like(irf)
        tttrlib.fconv(
            fit=model_fconv,
            irf=irf,
            x=lifetime_spectrum,
            dt=dt
        )

        np.testing.assert_array_almost_equal(model_ref, model_fconv)

        # AVX wont be supported on Apple -> M1
        if platform != "darwin":
            model_fconv_avx = np.zeros_like(irf)
            tttrlib.fconv_avx(
                fit=model_fconv_avx,
                irf=irf,
                x=lifetime_spectrum,
                dt=dt
            )
            np.testing.assert_array_almost_equal(model_fconv_avx, model_fconv)
