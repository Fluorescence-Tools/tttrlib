import utils
import os
import unittest

TOPDIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        '..')
)
utils.set_search_paths(TOPDIR)

import tttrlib
import numpy as np


class Tests(unittest.TestCase):

    def test_bh132_read(self):
        data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')
        self.assertEqual(
            data.get_number_of_tac_channels(),
            4096
        )
        self.assertEqual(
            np.array_equal(
                data.get_macro_time()[:10],
                np.array(
                    [
                        56916, 92675,
                        341107, 371130,
                        405608, 414113,
                        496582, 525446,
                        548846, 582482
                    ], dtype=np.uint64
                )
            ),
            True
        )

    # def test_ht3_read(self):
    #     data = tttrlib.TTTR(
    #         "./data/PQ/HT3/PQ_HT3v1.0_HH_T3.ht3", 'HT3'
    #     )
    #
    # def test_ptu_read(self):
    #     data = tttrlib.TTTR(
    #         "./data/PQ/PTU/PPQ_PTU_HH_T3.ptu", 'PTU'
    #     )

    def test_auto_correlation(self):
        data = tttrlib.TTTR(
            './data/BH/BH_SPC132.spc',
            'SPC-130'
        )
        ch1_indeces = data.get_selection_by_channel(np.array([0]))
        ch2_indeces = data.get_selection_by_channel(np.array([8]))
        mt = data.get_macro_time()

        t1 = mt[ch1_indeces]
        w1 = np.ones_like(t1, dtype=np.float)
        cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
        w1[cr_selection] *= 0.0

        t2 = mt[ch2_indeces]
        w2 = np.ones_like(t2, dtype=np.float)
        cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
        w2[cr_selection] *= 0.0

        B = 10
        n_casc = 25

        correlator = tttrlib.Correlator()
        correlator.set_n_bins(B)
        correlator.set_n_casc(n_casc)
        correlator.set_events(t1, w1, t2, w2)
        correlator.run()

        x = correlator.get_x_axis_normalized()
        y = correlator.get_corr_normalized()

        #self.assertEqual(b2.name, 'B')

    def test_cross_correlation(self):
        data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

        # make a cross-correlation between the two green channels (ch 0, ch 8)
        ch1_indeces = data.get_selection_by_channel(np.array([0]))
        ch2_indeces = data.get_selection_by_channel(np.array([8]))
        ph1 = tttrlib.TTTR(data, ch1_indeces)
        ph2 = tttrlib.TTTR(data, ch2_indeces)


        t1 = ph1.get_macro_time()
        w1 = np.ones_like(t1, dtype=np.float)
        cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
        w1[cr_selection] *= 0.0

        t2 = ph2.get_macro_time()
        w2 = np.ones_like(t2, dtype=np.float)
        cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
        w2[cr_selection] *= 0.0

        B = 10
        n_casc = 25

        correlator = tttrlib.Correlator()
        correlator.set_n_bins(B)
        correlator.set_n_casc(n_casc)
        correlator.set_events(t1, w1, t2, w2)
        correlator.run()

        x = correlator.get_x_axis_normalized()
        y = correlator.get_corr_normalized()

        #self.assertEqual(b2.name, 'B')

    def test_time_window_selection(self):
        photons = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 2)
        mt = photons.get_macro_time()

        tws = tttrlib.ranges_by_time_window(mt, 1000000, -1, 400, -1)
        tws = tws.reshape([len(tws)/2, 2]) # bring the tws in shape

        # convert the tws to photon selections
        phs = list()
        for tw in tws:
            phs += range(tw[0], tw[1])

        # Use the tw selection for correlation

        B = 10
        n_casc = 25

        t1 = mt[phs]
        w1 = np.ones_like(t1, dtype=np.float)
        t2 = mt[phs]
        w2 = np.ones_like(t2, dtype=np.float)


        correlator = tttrlib.Correlator()
        correlator.set_n_bins(B)
        correlator.set_n_casc(n_casc)
        correlator.set_events(t1, w1, t2, w2)
        correlator.run()

        x = correlator.get_x_axis_normalized()
        y = correlator.get_corr_normalized()

