import unittest
import numpy as np
import tttrlib


class TestRecurrenceAnalysis(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        np.random.seed(42)
        cls.n = 500
        cls.times = list(np.sort(np.cumsum(
            np.random.exponential(0.01, cls.n))))

    def test_same_molecule_probability_returns_flat(self):
        res = tttrlib.same_molecule_probability(self.times, 1e-3, 1.0, 50)
        arr = np.asarray(res)
        self.assertEqual(len(arr), 150)  # 3 * 50

    def test_p_same_in_range(self):
        res = np.asarray(tttrlib.same_molecule_probability(
            self.times, 1e-3, 1.0, 50))
        nb = len(res) // 3
        p_same = res[nb:2 * nb]
        self.assertTrue(np.all((p_same >= 0) & (p_same <= 1)))

    def test_recurrence_efficiencies(self):
        effs = list(np.random.uniform(0, 1, self.n))
        rec = tttrlib.recurrence_efficiencies(
            self.times, effs, 0.3, 0.7, 1e-3, 0.1)
        arr = np.asarray(rec)
        self.assertTrue(len(arr) > 0)
        self.assertTrue(np.all((arr >= 0) & (arr <= 1)))


if __name__ == '__main__':
    unittest.main()
