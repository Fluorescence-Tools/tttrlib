from __future__ import division

import unittest
import tttrlib
import numpy as np
import scipy.spatial


class Tests(unittest.TestCase):

    data = tttrlib.TTTR('./data/BH/BH_SPC132.spc', 'SPC-130')

    def test_bh132_read(self):
        data = self.data
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

        d = {
            'n_bins': 17,
            'n_casc': 25,
            'macro_times': (t1, t2),
            'weights': (w1, w2)
        }
        correlator2 = tttrlib.Correlator(**d)

        correlator.method = "lamb"
        x_lamb = correlator.x_axis
        y_lamb = correlator.correlation

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
            d[0] < 3.6,
            True
        )
        self.assertEqual(
            np.allclose(correlator2.correlation, y_peulen),
            True
        )

    # def compare_to_kristine(self):
    #     header = data.get_header()
    #     # Here macro_time_resolution is in nano seconds
    #     x_wahl_ms = x_wahl * header.macro_time_resolution / (1000.0 * 1000.0)
    #
    #     # load a file correlated using Kristine as a reference
    #     ref = np.loadtxt("./data/BH/correlator_references/BH_SPC132/08/MCSg_s--g_p.cor").T
    #     x_kristine, y_kristine, cr, err = ref
    #     n_min = min(len(x_kristine), len(x_wahl))
    #     d = scipy.spatial.distance.directed_hausdorff(
    #         u=(
    #             np.vstack([y_wahl, x_wahl_ms]).T[0:n_min-1]
    #         ),
    #         v=(
    #             np.vstack([y_kristine, x_kristine]).T[0:n_min-1]
    #         )
    #     )
    #     p.semilogx(x_wahl_ms, y_wahl)
    #     p.semilogx(x_kristine, y_kristine)
    #     p.show()

    def test_auto_correlation(self):
        data = self.data

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
        correlator.n_bins = B
        correlator.n_casc = n_casc
        correlator.set_macrotimes(t1, t2)
        correlator.set_weights(w1, w2)

        x = correlator.x_axis
        y = correlator.correlation

        header = data.get_header()
        # Here macro_time_resolution is in nano seconds
        x_ms = x * header.macro_time_resolution / (1000.0 * 1000.0)

        # load a file correlated using Kristine as a reference
        ref = np.loadtxt("./data/BH/correlator_references/BH_SPC132/08/MCSg_s--g_p.cor").T
        x_kristine, y_kristine, cr, err = ref
        n_kristine = x_kristine.shape[0]
        x_corr = x_ms[1:n_kristine+1]
        y_corr = y[1:n_kristine+1]

    def test_cross_correlation(self):
        data = self.data

        # make a cross-correlation between the two green channels (ch 0, ch 8)
        ch1_indeces = data.get_selection_by_channel(np.array([0]))
        ch2_indeces = data.get_selection_by_channel(np.array([8]))
        ph1 = tttrlib.TTTR(data, ch1_indeces)
        ph2 = tttrlib.TTTR(data, ch2_indeces)

        t1 = ph1.macro_times
        w1 = np.ones_like(t1, dtype=np.float)
        cr_selection = tttrlib.selection_by_count_rate(t1, 1200000, 30)
        w1[cr_selection] *= 0.0

        t2 = ph2.macro_times
        w2 = np.ones_like(t2, dtype=np.float)
        cr_selection = tttrlib.selection_by_count_rate(t2, 1200000, 30)
        w2[cr_selection] *= 0.0

        B = 10
        n_casc = 25

        correlator = tttrlib.Correlator()
        correlator.n_bins = B
        correlator.n_casc = n_casc
        correlator.set_macrotimes(t1, t2)
        correlator.set_weights(w1, w2)

        x = correlator.x_axis
        y = correlator.correlation

        #self.assertEqual(b2.name, 'B')

    def test_time_window_selection(self):
        photons = self.data
        mt = photons.macro_times

        tws = tttrlib.ranges_by_time_window(mt, 1000000, -1, 400, -1)
        tws = tws.reshape([len(tws) // 2, 2]) # bring the tws in shape

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
        correlator.n_bins = B
        correlator.n_casc = n_casc
        correlator.set_events(t1, w1, t2, w2)

        x = correlator.x_axis
        y = correlator.correlation

