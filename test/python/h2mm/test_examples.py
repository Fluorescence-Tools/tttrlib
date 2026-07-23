#!/usr/bin/env python3
"""Smoke test for the burstH2MM-style H2MM analysis example.

Runs ``examples/single_molecule/plot_h2mm_analysis.py`` end-to-end (headless) and
checks that the state-count scan / BIC selection / Viterbi decoding recover the
simulated three-state ground truth. This keeps the tutorial from silently
breaking when the :class:`tttrlib.H2MM` API changes.
"""
import runpy
import unittest
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

EXAMPLE = (
    Path(__file__).resolve().parents[3]
    / "examples"
    / "single_molecule"
    / "plot_h2mm_analysis.py"
)


class TestH2MMExample(unittest.TestCase):
    def test_example_runs_and_recovers_states(self):
        self.assertTrue(EXAMPLE.exists(), f"missing example: {EXAMPLE}")
        _show = plt.show
        plt.show = lambda *a, **k: None  # keep the gallery script from blocking
        try:
            ns = runpy.run_path(str(EXAMPLE))
        finally:
            plt.show = _show
            plt.close("all")

        # BIC selected the true three-state model.
        self.assertEqual(ns["best_k"], 3)
        # Three separated, ascending FRET states near the ground truth.
        E = ns["E_sorted"]
        self.assertEqual(len(E), 3)
        self.assertTrue(E[0] < E[1] < E[2])
        self.assertLess(abs(E[0] - 0.15), 0.1)
        self.assertLess(abs(E[2] - 0.80), 0.1)
        # Dwells and transitions were extracted from the Viterbi path.
        self.assertGreater(ns["dwell_state"].size, 0)
        self.assertGreater(len(ns["trans_from"]), 0)


if __name__ == "__main__":
    unittest.main()
