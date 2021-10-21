from __future__ import division

import unittest
import numpy as np
from sys import platform
import scipy.stats

import tttrlib
from misc.compute_irf import model_irf


class Tests(unittest.TestCase):

    data = np.array(
        [
            0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
            1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        ],
        dtype=np.float
    )
    w_sq = np.sqrt(data)**2.
    model = np.array(
        [
            1.55512239e-06, 1.28478411e-06, 1.84127094e-03, 6.97704393e-01,
            5.26292095e+00, 7.46022080e+00, 5.86203555e+00, 4.37710024e+00,
            3.27767338e+00, 2.46136154e+00, 1.85332949e+00, 1.39904465e+00,
            1.05863172e+00, 8.02832214e-01, 6.10103966e-01, 4.64532309e-01,
            3.54320908e-01, 2.70697733e-01, 2.07119266e-01, 1.58689654e-01,
            1.21735290e-01, 9.34921381e-02, 7.18751573e-02, 5.53076815e-02,
            4.25947583e-02, 3.28288183e-02, 2.53192074e-02, 1.95393890e-02,
            1.50872754e-02, 1.16553438e-02, 9.00806647e-03, 6.96482554e-03,
            1.32357360e-06, 1.00072013e-05, 2.67318412e-02, 1.32397314e+00,
            3.09121580e+00, 3.29547010e+00, 2.64779127e+00, 2.11040744e+00,
            1.67602309e+00, 1.32697773e+00, 1.04788208e+00, 8.25634384e-01,
            6.49268597e-01, 5.09724532e-01, 3.99592220e-01, 3.12860291e-01,
            2.44684015e-01, 1.91180006e-01, 1.49249476e-01, 1.16429072e-01,
            9.07668453e-02, 7.07203018e-02, 5.50733629e-02, 4.28692191e-02,
            3.33563629e-02, 2.59454278e-02, 2.01748116e-02, 1.56833931e-02,
            1.21889419e-02, 9.47107726e-03, 7.35784602e-03, 5.71517161e-03
        ]
    )

    def test_rescale(self):
        model = np.copy(self.model)
        data = np.copy(self.data)
        model /= 10
        scale = tttrlib.rescale(
            fit=model,
            decay=data,
            start=0,
            stop=-1
        )
        self.assertAlmostEqual(
            scale, 10.1010, places=2
        )

    def test_rescale_w(self):
        model = np.copy(self.model)
        data = np.copy(self.data)
        w_sq = np.copy(self.w_sq)
        model /= 10
        scale = tttrlib.rescale_w(
            fit=model,
            decay=data,
            w_sq=w_sq,
            start=0,
            stop=-1
        )
        self.assertAlmostEqual(
            scale, 10, places=1
        )

    def test_rescale_w_bg(self):
        model = np.copy(self.model)
        data = np.copy(self.data)
        w_sq = np.copy(self.w_sq)
        model /= 10
        scale = tttrlib.rescale_w_bg(
            fit=model,
            decay=data,
            w_sq=w_sq,
            bg=1.0,
            start=0,
            stop=-1
        )
        self.assertAlmostEqual(
            scale, 10.42089, places=4
        )

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

    def test_fconv_per(self):
        period = 13.0
        lifetime_spectrum = np.array([1.0, 4.1])
        irf, time_axis = model_irf(
            n_channels=32,
            period=period,
            irf_position_p=2.0,
            irf_position_s=2.0,
            irf_width=0.15
        )
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]

        model_fconv_per = np.zeros_like(irf)
        tttrlib.fconv_per(
            fit=model_fconv_per,
            irf=irf,
            x=lifetime_spectrum,
            period=period,
            start=0,
            stop=-1,
            dt=dt
        )
        ref = np.array(
            [0.14280653, 0.13579708, 0.12913168, 0.12279343, 0.1167663,  0.11103499,
             0.105585  , 0.10097891, 0.13309064, 0.50999418, 1.31833782, 1.83959874,
             1.85633556, 1.76860169, 1.68179244, 1.5992441,  1.52074752, 1.44610384,
             1.37512393, 1.30762796, 1.24344494, 1.18241225, 1.12437526, 1.06918693,
             1.01670744, 0.96680383, 0.91934967, 0.87422472, 0.83131467, 0.79051079,
             0.75170972, 0.71481314, 0.67972757, 0.64636413, 0.61463828, 0.58446966,
             0.55578181, 0.52850207, 0.50256131, 0.47789382, 0.45443709, 0.43213171,
             0.41092115, 0.39075168, 0.3715722,  0.35333412, 0.33599122, 0.31949958,
             0.30381741, 0.28890497, 0.27472449, 0.26124004, 0.24841745, 0.23622424,
             0.22462951, 0.2136039,  0.20311946, 0.19314964, 0.18366917, 0.17465403,
             0.16608139, 0.15792953, 0.15017779, 0.14280653]
        )
        np.testing.assert_array_almost_equal(model_fconv_per, ref)

        # AVX wont be supported on Apple -> M1
        if platform != "darwin":
            model_fconv_avx = np.zeros_like(irf)
            tttrlib.fconv_per_avx(
                fit=model_fconv_avx,
                irf=irf,
                x=lifetime_spectrum,
                period=period,
                start=0,
                stop=-1,
                dt=dt
            )
            np.testing.assert_array_almost_equal(model_fconv_avx, model_fconv_per)

    def test_fconv_per_cs(self):
        period = 13.0
        lifetime_spectrum = np.array([1.0, 4.1])
        irf, time_axis = model_irf(
            n_channels=32,
            period=period,
            irf_position_p=2.0,
            irf_position_s=2.0,
            irf_width=0.15
        )
        irf[irf < 0.001] = 0.0
        dt = time_axis[1] - time_axis[0]

        model_fconv_per_cs = np.zeros_like(irf)
        tttrlib.fconv_per_cs(
            fit=model_fconv_per_cs,
            irf=irf,
            x=lifetime_spectrum,
            period=period,
            stop=-1,
            conv_stop=14,
            dt=dt
        )
        ref = np.array([0.13579708, 0.12913168, 0.12279343, 0.1167663,  0.11103499, 0.105585,
                        0.10040251, 0.0960508,  0.12840442, 0.50553797, 1.31410034, 1.83556925,
                        1.85250385, 1.76495806, 1.67832765, 1.59594937, 1.51761451, 1.4431246,
                        1.37229093, 1.30493402, 1.24088322, 1.17997627, 1.12205885, 1.06698422,
                        1.01461284, 0.96481204, 0.91745564, 0.87242366, 0.82960201, 0.7888822,
                        0.75016106, 0.7133405,  0.67832721, 0.6450325 , 0.61337202, 0.58326554,
                        0.5546368,  0.52741326, 0.50152594, 0.47690927, 0.45350087, 0.43124144,
                        0.41007458, 0.38994666, 0.37080669, 0.35260619, 0.33529902, 0.31884136,
                        0.30319149, 0.28830978, 0.27415851, 0.26070184, 0.24790567, 0.23573758,
                        0.22416674, 0.21316384, 0.202701,   0.19275172, 0.18329078, 0.17429422,
                        0.16573924, 0.15760417, 0.1498684,  0.14251232])

        np.testing.assert_array_almost_equal(ref, model_fconv_per_cs)

    def test_sconv(self):
        period = 12.0
        irf, time_axis = model_irf(
            n_channels=32,
            period=period,
            irf_position_p=2.0,
            irf_position_s=2.0,
            irf_width=0.25
        )
        tau = 4.1
        decay = np.exp(-time_axis / tau)
        model_sconv = np.zeros_like(irf)
        tttrlib.sconv(
            fit=model_sconv,
            irf=irf,
            model=decay
        )
        ref = np.array([0.00000000e+00, 6.72772613e-12, 1.25902772e-09, 1.31944633e-07,
                        7.78665430e-06, 2.60011255e-04, 4.95321954e-03, 5.45801295e-02,
                        3.55713481e-01, 1.41837340e+00, 3.63088091e+00, 6.36684281e+00,
                        8.32504638e+00, 9.00004436e+00, 8.88536083e+00, 8.52999555e+00,
                        8.14728984e+00, 7.77768563e+00, 7.42462513e+00, 7.08758461e+00,
                        6.76584390e+00, 6.45870861e+00, 6.16551571e+00, 5.88563229e+00,
                        5.61845418e+00, 5.36340461e+00, 5.11993301e+00, 4.88751379e+00,
                        4.66564524e+00, 4.45384840e+00, 4.25166608e+00, 4.05866182e+00,
                        3.87441898e+00, 3.69853983e+00, 3.53064472e+00, 3.37037120e+00,
                        3.21737330e+00, 3.07132073e+00, 2.93189821e+00, 2.79880477e+00,
                        2.67175310e+00, 2.55046895e+00, 2.43469048e+00, 2.32416777e+00,
                        2.21866224e+00, 2.11794613e+00, 2.02180203e+00, 1.93002238e+00,
                        1.84240907e+00, 1.75877296e+00, 1.67893351e+00, 1.60271837e+00,
                        1.52996301e+00, 1.46051039e+00, 1.39421056e+00, 1.33092041e+00,
                        1.27050331e+00, 1.21282885e+00, 1.15777252e+00, 1.10521547e+00,
                        1.05504424e+00, 1.00715054e+00, 9.61430968e-01, 9.17786836e-01])
        np.testing.assert_array_almost_equal(ref, model_sconv)

    def test_convolve_lifetime_spectrum_periodic(self):
        period = 10
        time_axis = np.linspace(0, 25, period)
        irf_position = 6.0
        irf_width = 1.0
        irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
        lifetime_spectrum = np.array([0.2, 1.1, 0.8, 4.0])
        model_decay = np.zeros_like(time_axis)
        tttrlib.fconv_per_cs_time_axis(
            model=model_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            irf=irf,
            period=period
        )
        reference = np.array(
            [1.68774524e-08, 3.08318738e-03, 5.04539601e-01, 4.54789614e-01,
             2.32436705e-01, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00,
             0.00000000e+00, 0.00000000e+00])
        np.testing.assert_almost_equal(reference, model_decay, decimal=3)

    def test_lamp_shift(self):
        time_axis = np.linspace(4.5, 5.5, 16)
        irf_position = 5.0
        irf_width = 0.25
        irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
        irf_shift = np.empty_like(irf)
        time_shift = 0.5
        tttrlib.shift_lamp(irf, irf_shift, time_shift)
        ref = np.array(
            [
                0.28561886, 0.44980189, 0.66053707, 0.90452776, 1.15505166,
                1.37543629, 1.52736396, 1.58164736, 1.52736396, 1.37543629,
                1.15505166, 0.90452776, 0.66053707, 0.44980189, 0.28561886,
                0.
            ]
        )
        np.testing.assert_array_almost_equal(ref, irf_shift)

