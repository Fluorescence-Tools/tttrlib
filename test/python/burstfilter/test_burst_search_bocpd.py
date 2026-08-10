import unittest
import numpy as np
import tttrlib
import os
import json


class TestBurstSearchBOCPD(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        data_root = os.environ.get('TTTRLIB_DATA', './tttr-data')
        cls.test_file = os.path.join(data_root, 'pq', 'ptu', 'pq_ptu_hh_t3.ptu')
        if os.path.exists(cls.test_file):
            cls.data = tttrlib.TTTR(cls.test_file, 'PTU')
        else:
            cls.data = None

    def test_registry_has_bocpd(self):
        algs = json.loads(tttrlib.TTTR.burst_search_algorithms_json())
        self.assertIn('bocpd', algs)
        self.assertEqual(algs['bocpd']['method'], 'burst_search_bocpd')
        props = algs['bocpd']['params_schema']['properties']
        self.assertIn('changepoint_prob', props)
        self.assertIn('prior_count', props)

    def test_bocpd_in_coincident_enum(self):
        algs = json.loads(tttrlib.TTTR.burst_search_algorithms_json())
        enum = algs['coincident']['params_schema']['properties']['algorithm']['enum']
        self.assertIn('bocpd', enum)

    def test_bocpd_returns_pairs(self):
        if self.data is None:
            self.skipTest("Test data not available")
        bursts = self.data.burst_search_bocpd(
            L=20, dt=1e-3, changepoint_prob=0.5, max_run=256, per_channel=False
        )
        arr = np.asarray(bursts)
        # burst_search_bocpd returns flat [s0, e0, s1, e1, ...]
        self.assertEqual(len(arr) % 2, 0)
        if len(arr) > 0:
            # starts <= stops, sorted, non-overlapping
            starts = arr[::2]
            stops = arr[1::2]
            self.assertTrue(np.all(starts <= stops))
            self.assertTrue(np.all(np.diff(starts) > 0))

    def test_bocpd_defaults_match_registry(self):
        algs = json.loads(tttrlib.TTTR.burst_search_algorithms_json())
        props = algs['bocpd']['params_schema']['properties']
        # The TTTR method defaults must match the registry
        import inspect
        sig = inspect.signature(tttrlib.TTTR.burst_search_bocpd)
        for name, prop in props.items():
            if 'default' in prop and name in sig.parameters:
                self.assertEqual(
                    sig.parameters[name].default,
                    prop['default'],
                    f"Default mismatch for {name}"
                )

    def test_bocpd_by_name(self):
        if self.data is None:
            self.skipTest("Test data not available")
        bursts = self.data.burst_search_by_name(
            "bocpd",
            L=20, dt=1e-3, changepoint_prob=0.5, per_channel=False
        )
        arr = np.asarray(bursts)
        self.assertTrue(arr.ndim == 2)
        if arr.shape[0] > 0:
            self.assertEqual(arr.shape[1], 2)


if __name__ == '__main__':
    unittest.main()
