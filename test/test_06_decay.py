from __future__ import division
import unittest

import scipy.stats
import tttrlib
import fit2x
import numpy as np

print("Test: ", __file__)


class Tests(unittest.TestCase):

    def test_getter_setter(self):
        decay = tttrlib.Decay()

        decay.set_use_amplitude_threshold(True)
        self.assertEqual(decay.get_use_amplitude_threshold(), True)
        decay.set_use_amplitude_threshold(False)
        self.assertEqual(decay.get_use_amplitude_threshold(), False)

        decay.set_amplitude_threshold(11)
        self.assertEqual(decay.get_amplitude_threshold(), 11)
        decay.set_amplitude_threshold(2.2)
        self.assertEqual(decay.get_amplitude_threshold(), 2.2)

        decay.set_constant_background(11)
        self.assertEqual(decay.get_constant_background(), 11)
        decay.set_constant_background(2.2)
        self.assertEqual(decay.get_constant_background(), 2.2)

        # This does not work because there is no IRF
        # decay.set_irf_shift_channels(11)
        # self.assertEqual(decay.get_irf_shift_channels(), 11)
        # decay.set_irf_shift_channels(2.2)
        # self.assertEqual(decay.get_irf_shift_channels(), 2.2)

        decay.set_total_area(11)
        self.assertEqual(decay.get_total_area(), 11)
        decay.set_total_area(2.2)
        self.assertEqual(decay.get_total_area(), 2.2)

        decay.set_areal_scatter_fraction(0.2)
        self.assertEqual(decay.get_areal_scatter_fraction(), 0.2)
        decay.set_areal_scatter_fraction(0.8)
        self.assertEqual(decay.get_areal_scatter_fraction(), 0.8)

        decay.set_convolution_start(12)
        self.assertEqual(decay.get_convolution_start(), 12)
        decay.set_convolution_start(3)
        self.assertEqual(decay.get_convolution_start(), 3)

        decay.set_convolution_stop(12)
        self.assertEqual(decay.get_convolution_stop(), 12)
        decay.set_convolution_stop(3)
        self.assertEqual(decay.get_convolution_stop(), 3)

        decay.set_correct_pile_up(True)
        self.assertEqual(decay.get_correct_pile_up(), True)
        decay.set_correct_pile_up(False)
        self.assertEqual(decay.get_correct_pile_up(), False)

        decay.set_irf([1, 2, 3])
        self.assertListEqual(list(decay.get_irf()), [1, 2, 3])
        decay.set_irf([4, 5, 6])
        self.assertListEqual(list(decay.get_irf()), [4, 5, 6])

        decay.set_lifetime_spectrum([1, 2, 3, 4])
        self.assertListEqual(list(decay.get_lifetime_spectrum()), [1, 2, 3, 4])
        decay.set_lifetime_spectrum([4, 5, 6, 7])
        self.assertListEqual(list(decay.get_lifetime_spectrum()), [4, 5, 6, 7])

        decay.set_weights([1, 2, 3, 4])
        self.assertListEqual(list(decay.get_weights()), [1, 2, 3, 4])
        decay.set_weights([4, 5, 6, 7])
        self.assertListEqual(list(decay.get_weights()), [4, 5, 6, 7])

        decay.set_time_axis([1, 2, 3, 4])
        self.assertListEqual(list(decay.get_time_axis()), [1, 2, 3, 4])
        decay.set_time_axis([4, 5, 6, 7])
        self.assertListEqual(list(decay.get_time_axis()), [4, 5, 6, 7])

        decay.set_data([1, 2, 3, 45])
        self.assertListEqual(list(decay.get_data()), [1, 2, 3, 45])

        decay.set_irf_background_counts(892.1)
        self.assertEqual(decay.get_irf_background_counts(), 892.1)

    def test_constructor_1(self):
        decay = tttrlib.Decay()
        # default values
        self.assertEqual(decay.is_valid(), False)
        self.assertEqual(decay.get_convolution_start(), 0)
        self.assertEqual(decay.get_convolution_stop(), -1)
        self.assertEqual(decay.get_correct_pile_up(), False)
        self.assertEqual(decay.get_use_amplitude_threshold(), False)
        self.assertEqual(decay.get_period(), 100.0)

    def test_constructor_2(self):
        decay = tttrlib.Decay(
            decay_data=[1, 2, 3, 4, 56]
        )
        self.assertListEqual(list(decay.get_data()), [1, 2, 3, 4, 56])

        decay = tttrlib.Decay(
            instrument_response_function=[1., 2, 3, 4, 56]
        )
        self.assertListEqual(list(decay.get_irf()), [1, 2, 3, 4, 56])

        decay = tttrlib.Decay(
            time_axis=[1., 2, 3, 4, 56]
        )
        self.assertListEqual(list(decay.get_time_axis()), [1, 2, 3, 4, 56])

        decay = tttrlib.Decay(
            weights=[1., 2, 3, 4, 56]
        )
        self.assertListEqual(list(decay.get_weights()), [1, 2, 3, 4, 56])

        data = np.linspace(1, 22, 12)
        decay = tttrlib.Decay(
            decay_data=data,
            start=2, stop=32,
            correct_pile_up=True,
            excitation_period=123.2
        )
        self.assertEqual(
            np.allclose(1. / np.sqrt(data), decay.get_weights()),
            True
        )
        self.assertEqual(len(decay.get_irf()), len(data))
        self.assertEqual(len(decay.get_time_axis()), len(data))
        self.assertEqual(decay.get_convolution_start(), 2)
        self.assertEqual(decay.get_convolution_stop(), min(len(data), 12))
        self.assertEqual(decay.get_correct_pile_up(), True)
        self.assertEqual(decay.get_period(), 123.2)

    def test_convolve_lifetime_spectrum_variable_time_axis(self):
        time_axis = np.linspace(0, 25, 32)
        irf_position = 5.0
        irf_width = 1.0
        irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
        lifetime_spectrum = np.array([0.8, 1.1, 0.2, 4.0])
        model_decay = np.zeros_like(time_axis)
        fit2x.fconv_per_cs_time_axis(
            model_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            instrument_response_function=irf
        )
        reference = np.array([9.27891810e-07, 2.47480878e-05, 5.46047490e-04, 6.34298717e-03,
                              3.99865961e-02, 1.41117009e-01, 2.92769678e-01, 3.83528048e-01,
                              3.50102902e-01, 2.53687767e-01, 1.68661294e-01, 1.14128726e-01,
                              8.12945833e-02, 6.06527275e-02, 4.67915552e-02, 3.69092130e-02,
                              2.95268812e-02, 2.38266486e-02, 1.93277486e-02, 1.57274176e-02,
                              1.28215151e-02, 1.04639958e-02, 8.54548307e-03, 6.98137666e-03,
                              5.70483169e-03, 4.66231736e-03, 3.81060979e-03, 3.11463317e-03,
                              2.54583913e-03, 2.08095096e-03, 1.70097037e-03, 1.39038160e-03])
        self.assertEqual(
            np.allclose(reference, model_decay),
            True
        )

    # def test_convolve_lifetime_spectrum_periodic(self):
    #     time_axis = np.linspace(0, 25, 25)
    #     irf_position = 6.0
    #     irf_width = 1.0
    #     irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
    #     lifetime_spectrum = np.array([0.2, 1.1, 0.8, 4.0])
    #     model_decay = np.zeros_like(time_axis)
    #     tttrlib.Decay.convolve_lifetime_spectrum_periodic(
    #         model_decay,
    #         time_axis=time_axis,
    #         lifetime_spectrum=lifetime_spectrum,
    #         instrument_response_function=irf,
    #         period=16.0
    #     )
    #     reference = np.array(
    #         [0.00560653, 0.00432208, 0.00342868, 0.00603476, 0.0454072,
    #          0.21058509, 0.4551221, 0.55543513, 0.48165047, 0.36885986,
    #          0.27946424, 0.21331303, 0.16359693, 0.12577494, 0.09681668,
    #          0.07457228, 0.05745678, 0.04427657, 0.03412254, 0.02629821,
    #          0.02026841, 0.01562132, 0.01203976, 0.00927939, 0.0071519])
    #     self.assertEqual(
    #         np.allclose(reference, model_decay), True
    #     )

    def test_shift(self):
        time_axis = np.linspace(0, 12, 25)
        irf_position = 6.0
        irf_width = 1.0
        irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
        # integer shift
        shifted_irf_ip = tttrlib.Decay.shift_array(
            input=irf,
            shift=1.0
        )
        ref = np.array(
            [0.00000000e+00, 1.48671951e-06, 1.59837411e-05, 1.33830226e-04,
             8.72682695e-04, 4.43184841e-03, 1.75283005e-02, 5.39909665e-02,
             1.29517596e-01, 2.41970725e-01, 3.52065327e-01, 3.98942280e-01,
             3.52065327e-01, 2.41970725e-01, 1.29517596e-01, 5.39909665e-02,
             1.75283005e-02, 4.43184841e-03, 8.72682695e-04, 1.33830226e-04,
             1.59837411e-05, 1.48671951e-06, 1.07697600e-07, 6.07588285e-09,
             6.07588285e-09]
        )
        self.assertEqual(np.allclose(shifted_irf_ip, ref), True)
        shifted_irf_in = tttrlib.Decay.shift_array(
            input=irf,
            shift=-1.0
        )
        ref = np.array(
            [6.07588285e-09, 6.07588285e-09, 1.07697600e-07, 1.48671951e-06,
             1.59837411e-05, 1.33830226e-04, 8.72682695e-04, 4.43184841e-03,
             1.75283005e-02, 5.39909665e-02, 1.29517596e-01, 2.41970725e-01,
             3.52065327e-01, 3.98942280e-01, 3.52065327e-01, 2.41970725e-01,
             1.29517596e-01, 5.39909665e-02, 1.75283005e-02, 4.43184841e-03,
             8.72682695e-04, 1.33830226e-04, 1.59837411e-05, 1.48671951e-06,
             1.07697600e-07]
        )
        self.assertEqual(np.allclose(shifted_irf_in, ref), True)

        # floating shift
        shifted_irf_fp = tttrlib.Decay.shift_array(
            input=irf,
            shift=1.5
        )
        ref = np.array(
            [0.00000000e+00, 8.73523031e-06, 7.49069834e-05, 5.03256460e-04,
             2.65226555e-03, 1.09800745e-02, 3.57596335e-02, 9.17542811e-02,
             1.85744160e-01, 2.97018026e-01, 3.75503804e-01, 3.75503804e-01,
             2.97018026e-01, 1.85744160e-01, 9.17542811e-02, 3.57596335e-02,
             1.09800745e-02, 2.65226555e-03, 5.03256460e-04, 7.49069834e-05,
             8.73523031e-06, 7.97208558e-07, 5.68867416e-08, 6.07588285e-09,
             5.68867416e-08]
        )
        self.assertEqual(np.allclose(ref, shifted_irf_fp), True)

        shifted_irf_fn = tttrlib.Decay.shift_array(
            input=irf,
            shift=-1.5
        )
        ref = np.array(
            [6.07588285e-09, 5.68867416e-08, 7.97208558e-07, 8.73523031e-06,
             7.49069834e-05, 5.03256460e-04, 2.65226555e-03, 1.09800745e-02,
             3.57596335e-02, 9.17542811e-02, 1.85744160e-01, 2.97018026e-01,
             3.75503804e-01, 3.75503804e-01, 2.97018026e-01, 1.85744160e-01,
             9.17542811e-02, 3.57596335e-02, 1.09800745e-02, 2.65226555e-03,
             5.03256460e-04, 7.49069834e-05, 8.73523031e-06, 7.97208558e-07,
             5.68867416e-08]
        )
        self.assertEqual(np.allclose(ref, shifted_irf_fn), True)

        # rollover
        shifted_irf_irp = tttrlib.Decay.shift_array(
            input=irf,
            shift=26.0
        )
        self.assertEqual(
            np.allclose(shifted_irf_irp, shifted_irf_ip), True
        )

    def test_add_irf(self):
        time_axis = np.linspace(0, 10, 64)
        irf_position = 1.0
        irf_width = 0.5
        irf = scipy.stats.norm.pdf(
            time_axis,
            loc=irf_position,
            scale=irf_width
        )
        lifetime_spectrum = np.array([1.0, 4.0])
        model_decay = np.zeros_like(time_axis)
        fit2x.fconv_per_cs_time_axis(
            model_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            instrument_response_function=irf
        )
        model_incl_irf = tttrlib.Decay.add_curve(
            curve1=model_decay,
            curve2=irf,
            start=0,
            stop=-1,
            areal_fraction_curve2=0.9
        )
        ref = np.array(
            [0.34690072, 0.62173703, 1.01095837, 1.48560296, 1.97391665,
             2.37226495, 2.5796998, 2.53969438, 2.26563801, 1.83441674,
             1.352173, 0.91287367, 0.57136929, 0.33972356, 0.20072888,
             0.12605909, 0.08952876, 0.07268571, 0.06480473, 0.06056904,
             0.05769004, 0.05530237, 0.05311547, 0.05104114, 0.04905379,
             0.04714504, 0.04531079, 0.04354795, 0.04185369, 0.04022535,
             0.03866037, 0.03715627, 0.03571068, 0.03432134, 0.03298605,
             0.03170271, 0.0304693, 0.02928388, 0.02814458, 0.0270496,
             0.02599722, 0.02498578, 0.0240137, 0.02307943, 0.02218152,
             0.02131853, 0.02048913, 0.01969199, 0.01892586, 0.01818954,
             0.01748187, 0.01680173, 0.01614805, 0.0155198, 0.01491599,
             0.01433568, 0.01377794, 0.0132419, 0.01272672, 0.01223158,
             0.01175571, 0.01129834, 0.01085878, 0.01043631]
        )
        self.assertEqual(np.allclose(ref, model_incl_irf), True)

    def test_compute_decay(self):
        np.random.seed(0)
        time_axis = np.linspace(0, 10, 64)
        irf_position = 2.0
        irf_width = 0.5
        n_peak = 1000
        irf = scipy.stats.norm.pdf(
            time_axis,
            loc=irf_position,
            scale=irf_width
        )
        irf *= n_peak
        lifetime_spectrum = np.array([1.0, 4.])
        data_decay = np.zeros_like(time_axis)
        fit2x.fconv_per_cs_time_axis(
            data_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            instrument_response_function=irf
        )
        data_decay = np.random.poisson(
            np.clip(data_decay, 1e-9, None)
        )
        data_weight = 1. / np.clip(data_decay, 1, None)
        model = np.zeros_like(time_axis)
        irf += 0.0
        tttrlib.Decay.compute_decay(
            model_function=model,
            data=data_decay,
            squared_weights=data_weight,
            time_axis=time_axis,
            instrument_response_function=irf,
            lifetime_spectrum=lifetime_spectrum,
            irf_background_counts=0.0,
            irf_shift_channels=-4.5,
            scatter_areal_fraction=0.1,
            excitation_period=5.,
            constant_background=10,
            total_area=-1,
            use_amplitude_threshold=False
        )
        ref = np.array(
            [200.14178357, 193.01541012, 186.6902567, 181.919255,
             180.2246366, 184.21582932, 197.621866, 224.62048753,
             268.24316442, 328.28031062, 399.87860574, 474.21554589,
             541.63228454, 595.78940302, 636.23678854, 667.56363864,
             695.68428619, 723.87049036, 751.10880506, 773.40438611,
             786.57771931, 788.51156644, 779.7515296, 762.70243287,
             740.34886113, 715.2820183, 689.29929699, 663.44266721,
             638.22297216, 613.84669358, 590.37261652, 567.79789123,
             546.09766628, 525.24077317, 505.19512305, 485.92931803,
             467.41305141, 449.61716818, 432.51364251, 416.07553804,
             400.27696623, 385.09304576, 370.4998633, 356.4744359,
             342.99467477, 330.03935048, 317.58805956, 305.62119233,
             294.11990206, 283.06607524, 272.44230308, 262.23185408,
             252.4186477, 242.98722903, 233.92274441, 225.2109181,
             216.83802974, 208.7908928, 201.05683373, 193.62367211,
             186.47970135, 179.61367034, 173.01476571, 166.67259476]
        )
        self.assertEqual(np.allclose(ref, model), True)

    def test_decay_class(self):
        time_axis, data = np.load('./data/reference/img_decay_histogram.npy').T
        data[0] = 0
        time_axis *= 4
        irf_position = 1.6
        irf_width = 0.08
        n_peak = 10000
        irf = scipy.stats.norm.pdf(
            time_axis,
            loc=irf_position,
            scale=irf_width
        )
        irf *= n_peak
        irf += 1e-6

        weights = 1. / (np.sqrt(data))
        weights[0] = 0
        decay_object = tttrlib.Decay(
            decay_data=data,
            weights=weights,
            instrument_response_function=irf,
            time_axis=time_axis,
            constant_background=140.0
        )
        decay_object.evaluate(
            lifetime_spectrum=[1, 2]
        )
        m = decay_object.model
        wres = decay_object.weighted_residuals
        ref = np.array([ 140.00000006,  140.00000045,  140.00000081, 2344.53220855,
                         8139.54820575, 6332.78619895, 4934.09570572, 3851.31069245,
                         3013.08136965, 2364.17287072, 1861.82556729, 1472.93743639,
                         1171.88281279,  938.82379353,  758.40302532,  618.73173647,
                         510.60632983,  426.90191456,  362.10281367,  311.93911008,
                         273.10528178,  243.04238545,  219.76943567,  201.7528685 ,
                         187.8054877 ,  177.00823485,  168.64962827,  162.17888011,
                         157.16960238,  153.29171026,  150.28967121,  147.96566679,
                         146.16655743,  144.77379137,  143.6955927 ,  142.86091386,
                         142.21475395,  141.71453443,  141.32729351,  141.02751407,
                         140.79544222,  140.61578566,  140.47670597,  140.36903855,
                         140.28568868,  140.22116405,  140.17121283,  140.13254349,
                         140.10260793,  140.07943355,  140.0614933 ,  140.047605  ,
                         140.03685348,  140.02853028,  140.02208694,  140.01709888,
                         140.01323741,  140.01024809,  140.00793392,  140.00614244,
                         140.00475557,  140.00368194,  140.00285079,  140.00220737,
                         140.00170927,  140.00132367,  140.00102516,  140.00079407,
                         140.00061518,  140.00047668,  140.00036947,  140.00028648,
                         140.00022223,  140.00017249,  140.00013398,  140.00010417,
                         140.0000811 ,  140.00006323,  140.0000494 ,  140.0000387 ,
                         140.00003041,  140.00002399,  140.00001902,  140.00001518,
                         140.0000122 ,  140.0000099 ,  140.00000811,  140.00000673,
                         140.00000566,  140.00000484,  140.0000042 ,  140.0000037 ,
                         140.00000332,  140.00000302,  140.00000279,  140.00000261,
                         140.00000247,  140.00000237,  140.00000228,  140.00000222,
                         140.00000217])
        # import pylab as p
        # p.plot(m[::8] - ref)
        # p.show()
        self.assertEqual(
            np.allclose(m[::8], ref), True
        )
        wres_ref = np.array([ -0.        ,  -9.7010882 ,  -6.25656291,  30.70939668,
                              281.92169267, 226.87425997, 189.08683514, 159.53979761,
                              136.28861233, 118.93110622, 104.4235999 ,  90.33292967,
                              78.94600023,  69.66316889,  60.26153022,  53.12379246,
                              47.7774063 ,  42.81162417,  36.86148161,  32.67637617,
                              27.4406338 ,  25.72759958,  21.22527589,  17.76088218,
                              16.77410101,  14.35359234,  11.81458929,   9.84275823,
                              7.98011084,   6.36922016,   4.23828278,   2.31961497,
                              0.70951918,   0.3462245 ,  -1.96422667,  -2.08690637,
                              -3.18139644,  -3.35598071,  -4.13272935,  -5.11147168,
                              -5.76747679,  -6.17728865,  -5.73330468,  -6.445782  ,
                              -7.37407422,  -5.02740388,  -7.19902783,  -7.19462105,
                              -7.85376621,  -7.35041514,  -7.18652413,  -8.92418531,
                              -7.67662829,  -6.55872559,  -8.73396094,  -8.01589198,
                              -8.36818244,  -5.00541437,  -6.70909097,  -6.70889068,
                              -6.86357124,  -5.82336679,  -6.55587231,  -6.55580082,
                              -6.25675044,  -9.30276928,  -8.3667228 ,  -8.36669517,
                              -6.86310541,  -6.40508115,  -9.50004618,  -8.01391062,
                              -9.50002778, -10.76715014, -11.95038747,  -5.96559889,
                              -8.54740533,  -7.67235151,  -9.90601618,  -7.67234866,
                              -9.90601377,  -8.73128541,  -6.86303834,  -8.91837129,
                              -8.01387829,  -7.50555464,  -6.25656371, -12.20340525,
                              -6.70820457,  -8.91837003,  -7.67234464,  16.41207357,
                              6.19711648,  -8.36660063, -12.46249314,  -7.67234446,
                              -7.1795166 ,  -7.50555377,  -7.50555376,  -6.40502876,
                              -9.50000027])
        self.assertEqual(
            np.allclose(wres[::8], wres_ref), True
        )
