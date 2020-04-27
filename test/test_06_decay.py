from __future__ import division
import unittest

import scipy.stats
import tttrlib
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

        decay.set_irf_shift_channels(11)
        self.assertEqual(decay.get_irf_shift_channels(), 11)
        decay.set_irf_shift_channels(2.2)
        self.assertEqual(decay.get_irf_shift_channels(), 2.2)

        decay.set_total_area(11)
        self.assertEqual(decay.get_total_area(), 11)
        decay.set_total_area(2.2)
        self.assertEqual(decay.get_total_area(), 2.2)

        decay.set_areal_fraction_scatter(0.2)
        self.assertEqual(decay.get_areal_fraction_scatter(), 0.2)
        decay.set_areal_fraction_scatter(0.8)
        self.assertEqual(decay.get_areal_fraction_scatter(), 0.8)

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
            data=[1, 2, 3, 4, 56]
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
            data=data,
            start=2, stop=32,
            correct_pile_up=True,
            period=123.2
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
        tttrlib.Decay.convolve_lifetime_spectrum(
            model_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            instrument_response_function=irf
        )
        reference = np.array(
            [-5.15187263e-316, 2.47480797e-005, 5.46047484e-004,
             6.34298716e-003, 3.99865961e-002, 1.41117009e-001,
             2.92769678e-001, 3.83528048e-001, 3.50102902e-001,
             2.53687767e-001, 1.68661294e-001, 1.14128726e-001,
             8.12945833e-002, 6.06527275e-002, 4.67915552e-002,
             3.69092130e-002, 2.95268812e-002, 2.38266486e-002,
             1.93277486e-002, 1.57274176e-002, 1.28215151e-002,
             1.04639958e-002, 8.54548307e-003, 6.98137666e-003,
             5.70483169e-003, 4.66231736e-003, 3.81060979e-003,
             3.11463317e-003, 2.54583913e-003, 2.08095096e-003,
             1.70097037e-003, 1.39038160e-003])
        self.assertEqual(
            np.allclose(reference, model_decay),
            True
        )

    def test_convolve_lifetime_spectrum_periodic(self):
        time_axis = np.linspace(0, 25, 25)
        irf_position = 6.0
        irf_width = 1.0
        irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
        lifetime_spectrum = np.array([0.2, 1.1, 0.8, 4.0])
        model_decay = np.zeros_like(time_axis)
        tttrlib.Decay.convolve_lifetime_spectrum_periodic(
            model_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            instrument_response_function=irf,
            period=16.0
        )
        reference = np.array(
            [0.00560653, 0.00432208, 0.00342868, 0.00603476, 0.0454072,
             0.21058509, 0.4551221, 0.55543513, 0.48165047, 0.36885986,
             0.27946424, 0.21331303, 0.16359693, 0.12577494, 0.09681668,
             0.07457228, 0.05745678, 0.04427657, 0.03412254, 0.02629821,
             0.02026841, 0.01562132, 0.01203976, 0.00927939, 0.0071519])
        self.assertEqual(
            np.allclose(reference, model_decay), True
        )

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
        tttrlib.Decay.convolve_lifetime_spectrum(
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
            [0.34495683, 0.62126476, 1.01019228, 1.48447939, 1.97242676,
             2.37047872, 2.57776359, 2.5377968, 2.26395659, 1.83306968,
             1.35119728, 0.91223467, 0.57099093, 0.33952101, 0.20063084,
             0.12601618, 0.08951178, 0.07267963, 0.06480277, 0.06056846,
             0.05768989, 0.05530233, 0.05311547, 0.05104114, 0.04905379,
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
        tttrlib.Decay.convolve_lifetime_spectrum(
            data_decay,
            time_axis=time_axis,
            lifetime_spectrum=lifetime_spectrum,
            instrument_response_function=irf
        )
        data_decay = np.random.poisson(
            np.clip(data_decay, 1e-9, None)
        )
        data_weight = 1. / np.sqrt(np.clip(data_decay, 1, None))
        model = np.zeros_like(time_axis)
        irf += 0.0
        tttrlib.Decay.compute_decay(
            model_function=model,
            data=data_decay,
            weights=data_weight,
            time_axis=time_axis,
            instrument_response_function=irf,
            lifetime_spectrum=lifetime_spectrum,
            irf_background_counts=0.0,
            irf_shift_channels=-4.5,
            areal_fraction_scatter=0.1,
            period=5.,
            constant_background=10,
            total_area=-1,
            use_amplitude_threshold=False
        )
        ref = np.array(
            [186.91809416, 180.27794331, 174.3655104, 169.8463374,
             168.06392585, 171.31440106, 182.82577875, 206.09239358,
             243.37000948, 293.74073248, 351.91480308, 409.109722,
             456.37304095, 488.90331545, 508.67344053, 523.33251084,
             541.79417747, 569.25550923, 604.70284343, 642.06787995,
             673.75431945, 694.12021192, 701.16360989, 696.17746124,
             682.26859123, 662.83345324, 640.62135526, 617.45285213,
             594.34966015, 571.80153507, 550.00369802, 529.01171415,
             508.82378766, 489.41786285, 470.76610014, 452.83980932,
             435.61091305, 419.05230898, 403.13792459, 387.84269713,
             373.14253811, 359.01429613, 345.43572046, 332.38542607,
             319.84285991, 307.78826855, 296.20266709, 285.06780926,
             274.36615863, 264.08086107, 254.19571815, 244.69516166,
             235.56422907, 226.78853998, 218.35427346, 210.24814629,
             202.45739205, 194.969741, 187.77340076, 180.85703774,
             174.20975927, 167.8210965, 161.68098784, 155.77976318]
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
        decay = tttrlib.Decay(
            data=data,
            weights=weights,
            instrument_response_function=irf,
            time_axis=time_axis,
            constant_background=140.0
        )
        decay.evaluate(
            lifetime_spectrum=[1, 2]
        )
        m = decay.get_model()
        wres = decay.get_weighted_residuals()
        ref = np.array(
            [140.00000102, 140.00000512, 140.00000891, 23805.84546016,
             86015.84742602, 66620.09976124, 51605.03537217, 39981.24385258,
             30982.77899157, 24016.68968083, 18623.94758152, 14449.19959129,
             11217.35195963, 8715.44306865, 6778.61039207, 5279.22692907,
             4118.49126186, 3219.91706628, 2524.29306869, 1985.78133711,
             1568.89680459, 1246.16899148, 996.33184661, 802.9224272,
             653.19607833, 537.28662771, 447.55625731, 378.09221164,
             324.31717851, 282.68766845, 250.46051754, 225.51212746,
             206.19853162, 191.24706651, 179.67250985, 170.71215976,
             163.7755767, 158.40567662, 154.2486116, 151.0304531,
             148.53914154, 146.6105127, 145.11748017, 143.96166103,
             143.06689292, 142.37421538, 141.83798462, 141.42286588,
             141.10150504, 140.85272613, 140.66013594, 140.51104379,
             140.39562529, 140.30627499, 140.23710518, 140.18355792,
             140.14210474, 140.11001409, 140.08517137, 140.06593959,
             140.05105145, 140.03952592, 140.03060352, 140.02369632,
             140.01834917, 140.01420971, 140.01100518, 140.00852442,
             140.00660396, 140.00511725, 140.00396633, 140.00307535,
             140.00238561, 140.00185165, 140.00143829, 140.00111829,
             140.00087056, 140.00067879, 140.00053033, 140.0004154,
             140.00032643, 140.00025755, 140.00020423, 140.00016295,
             140.000131, 140.00010626, 140.00008711, 140.00007229,
             140.00006081, 140.00005192, 140.00004505, 140.00003972,
             140.0000356, 140.00003241, 140.00002994, 140.00002803,
             140.00002655, 140.0000254, 140.00002451, 140.00002383,
             140.00002329]
        )
        self.assertEqual(
            np.allclose(m[::8], ref), True
        )
        wres_ref = np.array(
            [-0., -9.70108879, -6.2565638, -293.71808614,
             29.34002198, -12.35734415, -30.72190531, -40.25307952,
             -43.41090675, -39.95248478, -35.3238449, -33.91722965,
             -30.57433884, -26.08216002, -24.58905563, -20.90830062,
             -15.77361366, -11.78570906, -11.27045374, -8.75739304,
             -9.3722196, -4.60063206, -5.69718172, -5.69297609,
             -2.25713549, -1.80710223, -2.01029904, -1.78162478,
             -1.76813376, -1.73386905, -2.64147919, -3.47647963,
             -4.11236502, -3.46101454, -5.22142371, -4.62936564,
             -5.24654872, -4.96957433, -5.42486116, -6.15435017,
             -6.5976885, -6.83136699, -6.2308479, -6.84252079,
             -7.69310023, -5.251875, -7.3889741, -7.34166693,
             -7.97067828, -7.43911788, -7.25474579, -8.98080337,
             -7.71833466, -6.58958611, -8.76003573, -8.03550936,
             -8.38358503, -5.0158157, -6.71772638, -6.7155762,
             -6.86877993, -5.82723194, -6.55895595, -6.55818848,
             -6.2585769, -9.30436759, -8.36791564, -8.36761913,
             -6.8637792, -6.40559362, -9.50049579, -8.01423929,
             -9.5002982, -10.76737063, -11.95056663, -5.96570889,
             -8.54750037, -7.67242306, -9.90607726, -7.67239245,
             -9.90605136, -8.73131373, -6.86305917, -8.91838935,
             -8.01389229, -7.50556577, -6.25657238, -12.20341434,
             -6.70821073, -8.91837578, -7.67234939, 16.41207198,
             6.19711437, -8.36660414, -12.46249694, -7.67234742,
             -7.17951934, -7.50555643, -7.50555633, -6.40503114,
             -9.50000291]
        )
        self.assertEqual(
            np.allclose(wres[::8], wres_ref), True
        )
