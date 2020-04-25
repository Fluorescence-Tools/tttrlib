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
            np.allclose(1./np.sqrt(data), decay.get_weights()),
            True
        )
        self.assertEqual(len(decay.get_irf()), len(data))
        self.assertEqual(len(decay.get_time_axis()), len(data))
        self.assertEqual(decay.get_convolution_start(), 2)
        self.assertEqual(decay.get_convolution_stop(), min(len(data), 12))
        self.assertEqual(decay.get_correct_pile_up(), True)
        self.assertEqual(decay.get_period(), 123.2)

    #
    # def test_convolve_lifetime_spectrum_variable_time_axis(self):
    #     time_axis = np.linspace(0, 25, 50)
    #     irf_position = 5.0
    #     irf_width = 1.0
    #     irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
    #     lifetime_spectrum = np.array([0.8, 1.1, 0.2, 4.0])
    #     model_decay = np.zeros_like(time_axis)
    #     tttrlib.Decay.convolve_lifetime_spectrum(
    #         model_decay,
    #         time_axis=time_axis,
    #         lifetime_spectrum=lifetime_spectrum,
    #         instrument_response_function=irf
    #     )
    #
    # def test_convolve_lifetime_spectrum_periodic(self):
    #     import scipy.stats
    #     import tttrlib
    #     import numpy as np
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
    #
    # def test_shift(self):
    #     import scipy.stats
    #     import tttrlib
    #     import numpy as np
    #
    #     time_axis = np.linspace(0, 12, 25)
    #     irf_position = 6.0
    #     irf_width = 1.0
    #     irf = scipy.stats.norm.pdf(time_axis, loc=irf_position, scale=irf_width)
    #     # integer shift
    #     shifted_irf_ip = tttrlib.Decay.shift_array(
    #         input=irf,
    #         shift=1.0
    #     )
    #     shifted_irf_in = tttrlib.Decay.shift_array(
    #         input=irf,
    #         shift=-1.0
    #     )
    #     # floating shift
    #     shifted_irf_fp = tttrlib.Decay.shift_array(
    #         input=irf,
    #         shift=1.2
    #     )
    #     shifted_irf_fn = tttrlib.Decay.shift_array(
    #         input=irf,
    #         shift=-1.2
    #     )
    #     p.plot(irf, label="no shift")
    #     p.plot(shifted_irf_ip, label="integer positive")
    #     p.plot(shifted_irf_in, label="integer negative")
    #     p.plot(shifted_irf_fp, label="float positive")
    #     p.plot(shifted_irf_fn, label="float positive")
    #     p.legend()
    #     p.show()
    #
    #     self.assertEqual(
    #         np.allclose(
    #             model_decay, data_decay
    #         ),
    #         True
    #     )
    #
    # def test_add_irf(self):
    #     import scipy.stats
    #     import tttrlib
    #     import numpy as np
    #
    #     time_axis = np.linspace(0, 10, 100)
    #     irf_position = 1.0
    #     irf_width = 0.5
    #     irf = scipy.stats.norm.pdf(
    #         time_axis,
    #         loc=irf_position,
    #         scale=irf_width
    #     )
    #     lifetime_spectrum = np.array([1.0, 4.0])
    #     model_decay = np.zeros_like(time_axis)
    #     tttrlib.Decay.convolve_lifetime_spectrum(
    #         model_decay,
    #         time_axis=time_axis,
    #         lifetime_spectrum=lifetime_spectrum,
    #         instrument_response_function=irf
    #     )
    #     model_incl_irf = tttrlib.Decay.add_curve(
    #         curve1=model_decay,
    #         curve2=irf,
    #         start=0,
    #         stop=-1,
    #         fraction_curve2=0.9
    #     )
    #     p.semilogy(irf)
    #     p.semilogy(model_decay)
    #     p.semilogy(model_incl_irf)
    #     p.show()
    #
    # def test_compute_decay(self):
    #     import scipy.stats
    #     import tttrlib
    #     import numpy as np
    #
    #     time_axis = np.linspace(0, 10, 512)
    #     irf_position = 2.0
    #     irf_width = 0.5
    #     n_peak = 10000
    #     irf = scipy.stats.norm.pdf(
    #         time_axis,
    #         loc=irf_position,
    #         scale=irf_width
    #     )
    #     irf *= n_peak
    #     lifetime_spectrum = np.array([1.0, 4.])
    #     data_decay = np.zeros_like(time_axis)
    #     tttrlib.Decay.convolve_lifetime_spectrum(
    #         data_decay,
    #         time_axis=time_axis,
    #         lifetime_spectrum=lifetime_spectrum,
    #         instrument_response_function=irf
    #     )
    #     data_decay = np.random.poisson(
    #         np.clip(data_decay, 1e-9, None)
    #     )
    #     data_weight = 1. / np.sqrt(np.clip(data_decay, 1, None))
    #     model = np.zeros_like(time_axis)
    #     irf += 0.0
    #     tttrlib.Decay.compute_decay(
    #         model_function=model,
    #         data=data_decay,
    #         weights=data_weight,
    #         time_axis=time_axis,
    #         instrument_response_function=irf,
    #         lifetime_spectrum=lifetime_spectrum,
    #         irf_background_counts=0.0,
    #         irf_shift_channels=-4.5,
    #         irf_areal_fraction=0.1,
    #         period=5.,
    #         constant_background=10,
    #         total_area=-1,
    #         use_amplitude_threshold=False
    #     )
    #     p.semilogy(irf)
    #     p.semilogy(data_decay)
    #     p.semilogy(model)
    #     p.show()
    #
    # def test_decay_class(self):
    #     import scipy.stats
    #     import numpy as np
    #     import tttrlib
    #
    #     time_axis, data = np.load('./data/reference/img_decay_histogram.npy').T
    #     irf_position = 2.0
    #     irf_width = 0.5
    #     n_peak = 10000
    #     irf = scipy.stats.norm.pdf(
    #         time_axis,
    #         loc=irf_position,
    #         scale=irf_width
    #     )
    #     irf *= n_peak
    #
    #     weights = 1. / (np.sqrt(data) + 1e-10)
    #     decay = tttrlib.Decay(
    #         data=data,
    #         weights=weights,
    #         instrument_response_function=irf,
    #         time_axis=time_axis
    #     )
    #     decay.set_lifetime_spectrum([1, 4])
    #     decay.evaluate(
    #         constant_background=10
    #     )
    #     m = decay.get_model_function()
    #     p.semilogy(time_axis, m)
    #     p.semilogy(time_axis, data)
    #     p.semilogy(time_axis, irf)
    #     p.show()
    #
    # def test_sm_FRET(self):
    #     import tttrlib
    #     # load file
    #     spc132_filename = './data/bh/bh_spc132.spc'
    #     data = tttrlib.TTTR(spc132_filename, 'SPC-130')
    #     ch0_indeces = data.get_selection_by_channel([0])
    #     data_ch0 = data[ch0_indeces]
    #
    #     ##### Make IRF
    #     # use count rate selection to select region with no bursts
    #     cr_selection = tttrlib.selection_by_count_rate(
    #         data_ch0.macro_times,
    #         6.0, n_ph_max=3,
    #         macro_time_calibration=data.header.macro_time_resolution / 1e6
    #     )
    #     low_count_selection = data_ch0[cr_selection]
    #     # create histogram for IRF
    #     irf, bins = np.histogram(low_count_selection.micro_times, bins=512)
    #
    #     ##### Select bursts
    #     cr_selection = tttrlib.selection_by_count_rate(
    #         data_ch0.macro_times,
    #         0.01, n_ph_max=2,
    #         macro_time_calibration=data.header.macro_time_resolution / 1e6,
    #         invert=True
    #     )
    #     high_count_selection = data_ch0[cr_selection]
    #     data_decay, bins = np.histogram(high_count_selection.micro_times, bins=512)
    #     time_axis = np.arange(0, 512) * data.header.micro_time_resolution * 4096 / 512
    #
    #     weights = 1. / (np.sqrt(data_decay) + 1e-10)
    #     decay = tttrlib.Decay(
    #         data=data_decay.astype(np.float64),
    #         instrument_response_function=irf.astype(np.float64),
    #         time_axis=time_axis,
    #         weights=weights,
    #         period=data.header.macro_time_resolution
    #     )
    #     decay.set_lifetime_spectrum([1., 4.])
    #     decay.evaluate()
    #     p.semilogy(time_axis, irf)
    #     p.semilogy(time_axis, data_decay)
    #     p.semilogy(
    #         time_axis,
    #         decay.get_model_function()
    #     )
    #     p.show()
    #
    #
