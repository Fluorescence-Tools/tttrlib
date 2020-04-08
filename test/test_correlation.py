from __future__ import division

import unittest
import tttrlib
import numpy as np
import scipy.spatial


class Tests(unittest.TestCase):

    data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

    def test_correlation(self):
        # this compares the result of the implemented correlators
        data = self.data

        ch1_indeces = data.get_selection_by_channel(np.array([8]))
        ch2_indeces = data.get_selection_by_channel(np.array([0]))
        mt = data.macro_times

        t1 = mt[ch1_indeces]
        w1 = np.ones_like(t1, dtype=np.float)

        t2 = mt[ch2_indeces]
        w2 = np.ones_like(t2, dtype=np.float)

        correlator = tttrlib.Correlator()
        correlator.n_bins = 17
        correlator.n_casc = 25
        correlator.set_macrotimes(t1, t2)
        correlator.set_weights(w1, w2)
        x_peulen = correlator.x_axis
        y_peulen = correlator.correlation

        correlator.method = 'lamb'
        x_lamb = correlator.x_axis
        y_lamb = correlator.correlation

        d = {
            'n_bins': 17,
            'n_casc': 25,
            'macro_times': (t1, t2),
            'weights': (w1, w2),
        }
        correlator2 = tttrlib.Correlator(**d)

        n_min = min(len(x_peulen), len(x_lamb))
        d = scipy.spatial.distance.directed_hausdorff(
            u=(
                np.vstack([y_peulen, x_peulen]).T[0:n_min]
            ),
            v=(
                np.vstack([y_lamb, x_lamb]).T[0:n_min]
            )
        )
        self.assertEqual(
            d[0] < 5.3,
            True
        )
        self.assertEqual(
            np.allclose(correlator2.correlation, y_peulen),
            True
        )

    def compare_to_kristine(self):
        # This test compares tttrlib correlations to the
        # correlations performed by the Seidel software Kristine

        # import tttrlib
        # import pylab as p
        data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

        correlator = tttrlib.Correlator(
            method='default',
            n_casc=20,
            n_bins=10,
            tttr=(
                tttrlib.TTTR(data, data.get_selection_by_channel([0])),
                tttrlib.TTTR(data, data.get_selection_by_channel([8]))
            ),
            make_fine=False
        )
        x = correlator.x_axis
        y = correlator.correlation

        # load a file correlated using Kristine as a reference
        ref = np.loadtxt("./data/BH/correlator_references/BH_SPC132/08/MCSg_s--g_p.cor").T
        x_kristine, y_kristine, cr, err = ref
        # p.semilogx(x, y)
        # p.semilogx(x_kristine, y_kristine)

        n_min = min(len(x_kristine), len(x))
        d = scipy.spatial.distance.directed_hausdorff(
            u=(
                np.vstack([y, x]).T[0:n_min-1]
            ),
            v=(
                np.vstack([y_kristine, x_kristine]).T[0:n_min-1]
            )
        )
        self.assertEqual(
            d[0] < 6.3,
            True
        )

    def test_correlation(self):
        # correlations providing macro times and weights
        data = self.data
        ch1_indeces = data.get_selection_by_channel(np.array([0]))
        ch2_indeces = data.get_selection_by_channel(np.array([8]))
        mt = data.get_macro_time()

        t1 = mt[ch1_indeces]
        w1 = np.ones_like(t1, dtype=np.float)
        t2 = mt[ch2_indeces]
        w2 = np.ones_like(t2, dtype=np.float)
        # zero low count rate regions
        cr_selection = tttrlib.selection_by_count_rate(
            t1, 1200000, 30
        )
        w1[cr_selection] *= 0.0
        cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
        w2[cr_selection] *= 0.0

        B = 10  # the number of bins in each bloch
        n_casc = 25  # the number of blocks

        correlator = tttrlib.Correlator()
        correlator.n_bins = B
        correlator.n_casc = n_casc
        correlator.set_macrotimes(t1, t2)
        correlator.set_weights(w1, w2)

        # the correlator is not aware of the macro time spacing
        x_1 = correlator.x_axis
        y_1 = correlator.correlation

        # use the header od the data to scale the correlation axis
        header = data.get_header()
        # Here macro_time_resolution is in nano seconds
        x_ms = x_1 * header.macro_time_resolution / (1000.0 * 1000.0)

        # when the TTTR object is passed upon initialization the
        # x-axis is scaled in milliseconds
        correlator_2 = tttrlib.Correlator(
            tttr=data,
            n_casc=n_casc,
            n_bins=B
        )
        correlator_2.set_macrotimes(t1, t2)
        correlator_2.set_weights(w1, w2)
        x_2 = correlator_2.x_axis
        y_2 = correlator_2.correlation
        self.assertEqual(
            np.allclose(x_ms, x_2), True
        )
        self.assertEqual(
            np.allclose(y_1, y_2), True
        )

    def test_auto_correlation_2(self):
        # this tests the direct use of tttr objects
        data = self.data

        # import tttrlib
        # data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

        # if a correlator is initialized with a TTTR object the
        # correlation corresponds to the macro time correlation of all events
        correlator = tttrlib.Correlator(tttr=data)
        # the correlation is computed when the attributes are accessed
        x = correlator.x_axis
        y = correlator.correlation
        # p.semilogx(x, y)

        correlator_2 = tttrlib.Correlator(data)
        t1 = data.macro_times
        t2 = t1
        w1 = np.ones_like(t1, dtype=np.float)
        w2 = np.ones_like(t2, dtype=np.float)
        correlator_2.set_macrotimes(t1, t2)
        correlator_2.set_weights(w1, w2)

        # p.semilogx(correlator.x_axis, correlator_2.correlation)

        self.assertEqual(
            np.allclose(correlator_2.correlation, y),
            True
        )
        self.assertEqual(
            np.allclose(correlator_2.x_axis, x),
            True
        )

    def test_correlation_with_tttr_selection(self):
        data = self.data

        # import tttrlib
        # import pylab as p
        # data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

        # get the indices of the two channels
        ch1_indeces = data.get_selection_by_channel([8])
        ch2_indeces = data.get_selection_by_channel([0])

        # use the indices to create new TTTR objects
        tttr_ch1 = tttrlib.TTTR(data, ch1_indeces)
        tttr_ch2 = tttrlib.TTTR(data, ch2_indeces)
        # correlate with the new TTTR objects
        correlator = tttrlib.Correlator(
            method='default',
            n_casc=35,
            n_bins=6,
            tttr=(tttr_ch1, tttr_ch2),
            make_fine=True
        )
        # Alternatively instead of the constructor use method below
        # correlator.set_tttr(data_ch1, data_ch2, make_fine=True)
        x = correlator.x_axis
        y = correlator.correlation

        # use pylab to plot the data
        # p.semilogx(x, y)
        # p.show()

        # the above should produce the same result as the code below
        correlator_ref = tttrlib.Correlator(
            tttr=data,
            n_casc=35,
            n_bins=6
        )
        t1 = data.macro_times[ch1_indeces]
        t2 = data.macro_times[ch2_indeces]
        mt1 = data.micro_times[ch1_indeces]
        mt2 = data.micro_times[ch2_indeces]
        w1 = np.ones_like(t1, dtype=np.float)
        w2 = np.ones_like(t2, dtype=np.float)
        correlator_ref.set_macrotimes(t1, t2)
        correlator_ref.set_weights(w1, w2)
        n_microtime_channels = data.get_number_of_micro_time_channels()
        correlator_ref.set_microtimes(mt1, mt2, n_microtime_channels)
        x_ref = correlator_ref.x_axis
        y_ref = correlator_ref.correlation

        # p.semilogx(x_ref, y_ref)
        # p.show()

        self.assertEqual(
            np.allclose(x, x_ref), True
        )
        self.assertEqual(
            np.allclose(y, y_ref), True
        )

    def test_correlation_with_channel_tttr_selection(self):
        data = self.data

        # import tttrlib
        # data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

        # Below as a reference is a correlator with TTTR objects as input
        # get the indices of the two channels
        ch1, ch2 = [8], [0]
        tttr_ch1 = tttrlib.TTTR(data, data.get_selection_by_channel(ch1))
        tttr_ch2 = tttrlib.TTTR(data, data.get_selection_by_channel(ch2))
        correlator = tttrlib.Correlator(
            method='default',
            n_casc=35,
            n_bins=6,
            tttr=(tttr_ch1, tttr_ch2),
            make_fine=True
        )
        x = correlator.x_axis
        y = correlator.correlation

        # The above should produce the same result as the code below
        correlator_ref = tttrlib.Correlator(
            tttr=data,
            n_casc=35,
            n_bins=6,
            channels=(ch1, ch2),
            make_fine=True
        )
        x_ref = correlator_ref.x_axis
        y_ref = correlator_ref.correlation

        self.assertEqual(
            np.allclose(x, x_ref), True
        )
        self.assertEqual(
            np.allclose(y, y_ref), True
        )
    #
    # def test_cross_correlation(self):
    #     data = self.data
    #
    #     # make a cross-correlation between the two green channels (ch 0, ch 8)
    #     ph1 = tttrlib.TTTR(data, data.get_selection_by_channel([0]))
    #     ph2 = tttrlib.TTTR(data, data.get_selection_by_channel([8]))
    #
    #     t1 = ph1.macro_times
    #     w1 = np.ones_like(t1, dtype=np.float)
    #     cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
    #     w1[cr_selection] *= 0.0
    #
    #     t2 = ph2.macro_times
    #     w2 = np.ones_like(t2, dtype=np.float)
    #     cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
    #     w2[cr_selection] *= 0.0
    #
    #     correlator = tttrlib.Correlator()
    #     correlator.n_bins = 10
    #     correlator.n_casc = 25
    #     correlator.set_macrotimes(t1, t2)
    #     correlator.set_weights(w1, w2)
    #
    #     x = correlator.x_axis
    #     y = correlator.correlation
    #
    #     #self.assertEqual(b2.name, 'B')

    # def test_time_window_selection(self):
    #     photons = self.data
    #     mt = photons.macro_times
    #
    #     tws = tttrlib.ranges_by_time_window(mt, 1000000, -1, 400, -1)
    #     tws = tws.reshape([len(tws) // 2, 2]) # bring the tws in shape
    #
    #     # convert the tws to photon selections
    #     phs = list()
    #     for tw in tws:
    #         phs += range(tw[0], tw[1])
    #
    #     # Use the tw selection for correlation
    #
    #     B = 10
    #     n_casc = 25
    #
    #     t1 = mt[phs]
    #     w1 = np.ones_like(t1, dtype=np.float)
    #     t2 = mt[phs]
    #     w2 = np.ones_like(t2, dtype=np.float)
    #
    #     correlator = tttrlib.Correlator()
    #     correlator.n_bins = B
    #     correlator.n_casc = n_casc
    #     correlator.set_events(t1, w1, t2, w2)
    #
    #     x = correlator.x_axis
    #     y = correlator.correlation
    #
