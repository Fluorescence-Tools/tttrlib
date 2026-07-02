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

    def test_fit26(self):
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
        pattern_1 = np.copy(irf)
        pattern_2 = np.array(
            [2.19319033e-08, 1.84413639e-08, 3.34901093e-05, 1.26870040e-02,
             9.39283373e-02, 1.22572899e-01, 9.04619781e-02, 6.48528685e-02,
             4.76001206e-02, 3.55488448e-02, 2.68771272e-02, 2.04940827e-02,
             1.57172794e-02, 1.21005483e-02, 9.34005838e-03, 7.22159597e-03,
             5.58990082e-03, 4.33007676e-03, 3.35581222e-03, 2.60158411e-03,
             2.01729163e-03, 1.56443990e-03, 1.21335530e-03, 9.41114763e-04,
             7.29984906e-04, 5.66234218e-04, 4.39223436e-04, 3.40705850e-04,
             2.64287576e-04, 2.05010412e-04, 1.59029028e-04, 1.23360965e-04,
             1.86812539e-08, 1.77852830e-07, 4.86291035e-04, 2.41480990e-02,
             5.93758154e-02, 6.60604491e-02, 5.44150218e-02, 4.37331561e-02,
             3.46979744e-02, 2.73087097e-02, 2.13834235e-02, 1.66888768e-02,
             1.29973503e-02, 1.01084281e-02, 7.85456846e-03, 6.09967166e-03,
             4.73504656e-03, 3.67479642e-03, 2.85148543e-03, 2.21239392e-03,
             1.71641883e-03, 1.33157038e-03, 1.03297999e-03, 8.01329509e-04,
             6.21619684e-04, 4.82208333e-04, 3.74060853e-04, 2.90167144e-04,
             2.25088434e-04, 1.74605311e-04, 1.35444470e-04, 1.05066633e-04]
        )
        data = np.array(
            [0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
            1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        )
        settings = {
            'pattern_1': pattern_1,
            'pattern_2': pattern_2,
            'verbose': False
        }
        fit26 = tttrlib.Fit26(**settings)
        x1 = 0.5
        x = np.array([x1])
        fixed = np.array([0])
        r = fit26(
            data=data,
            initial_values=x,
            fixed=fixed
        )
        # import pylab as p
        # p.plot(fit26.model)
        # p.plot(fit26.data)
        # p.plot(fit26.irf)
        # p.show()
        best_fraction_1 = r['x'][0]
        self.assertEqual(best_fraction_1 < 0.1, True)
