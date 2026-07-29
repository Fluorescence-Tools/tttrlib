"""Smoke test for the decay-fit interface tutorial.

Runs ``examples/fluorescence_decay/plot_decay_fit_interface.py`` end to end
(headless) and checks that it recovers the lifetime it simulates. A documentation
example that only *imports* is not covered — an API change can leave it running
and quietly wrong — so this asserts the numbers the text tells the reader to
expect.
"""
import runpy
import unittest
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

import numpy as np

EXAMPLE = (
    Path(__file__).resolve().parents[3]
    / "examples"
    / "fluorescence_decay"
    / "plot_decay_fit_interface.py"
)


class TestDecayFitInterfaceExample(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        if not EXAMPLE.exists():
            raise AssertionError(f"missing example: {EXAMPLE}")
        show = plt.show
        plt.show = lambda *a, **k: None      # keep the gallery from blocking
        try:
            cls.ns = runpy.run_path(str(EXAMPLE))
        finally:
            plt.show = show
            plt.close("all")

    def test_recovers_the_simulated_lifetime(self):
        self.assertAlmostEqual(
            self.ns["outcome"].parameters[0], self.ns["true_tau"], delta=0.05)

    def test_reports_a_good_fit(self):
        """2I* near 1 is what the text tells the reader a good fit looks like."""
        named = self.ns["named"]
        self.assertLess(named["twoIstar"], 2.0)
        self.assertIs(named["converged"], True)

    def test_results_are_named_not_positional(self):
        self.assertIn("r_scatter", self.ns["named"])
        self.assertIn("twoIstar", self.ns["named"])

    def test_the_batch_agrees_with_the_truth(self):
        taus = np.asarray(self.ns["taus"])
        self.assertEqual(taus.size, self.ns["n_rows"])
        self.assertAlmostEqual(taus.mean(), self.ns["true_tau"], delta=0.02)
        # A spread of exactly zero would mean every row got identical data.
        self.assertGreater(taus.std(), 0.0)

    def test_the_second_model_actually_fits(self):
        """fit24 must produce a finite score, not the NaN of a refused fit.

        Its likelihood carries a background term that is undefined against an
        all-zero background, in which case it returns the start values with a
        NaN score — which would make the "swap the model" section teach a broken
        call.
        """
        out24 = self.ns["out24"]
        self.assertTrue(np.isfinite(out24.objective))
        self.assertGreater(out24.objective, 0.0)


if __name__ == "__main__":
    unittest.main()
