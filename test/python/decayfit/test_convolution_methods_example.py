"""Runs the convolution-methods tutorial and checks what it claims.

``examples/fluorescence_decay/plot_convolution_methods.py`` makes four factual
claims a reader will act on: the two backends agree to machine precision, the
recursion is faster and pulls further ahead with more rates, a wrapping response
splits them, and a sub-bin shift stays positive. An example that merely imports
is not covered — an API change can leave it running and quietly wrong — so each
claim is asserted here from the example's own namespace.
"""
import runpy
import unittest
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False

import numpy as np

EXAMPLE = (
    Path(__file__).resolve().parents[3]
    / "examples"
    / "fluorescence_decay"
    / "plot_convolution_methods.py"
)


class TestConvolutionMethodsExample(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        if not HAS_MATPLOTLIB:
            raise unittest.SkipTest("matplotlib not installed")
        if not EXAMPLE.exists():
            raise AssertionError(f"missing example: {EXAMPLE}")
        show = plt.show
        plt.show = lambda *a, **k: None      # keep the gallery from blocking
        try:
            cls.ns = runpy.run_path(str(EXAMPLE))
        finally:
            plt.show = show
            plt.close("all")

    def test_the_backends_agree_to_machine_precision(self):
        """The headline claim of the first figure."""
        self.assertLess(self.ns["agreement"], 1e-10)

    def test_the_recursion_is_faster_at_every_rate_count(self):
        speedup = np.asarray(self.ns["speedup"])
        self.assertTrue(np.all(speedup > 1.0),
                        "the text says the recursion wins everywhere: %s" % speedup)

    def test_the_gap_widens_with_the_rate_count(self):
        """The example's actual argument, not just "one is faster".

        Timings are noisy, so compare the ends rather than requiring monotonic
        growth: a single rate against sixty-four.
        """
        speedup = np.asarray(self.ns["speedup"])
        self.assertGreater(speedup[-1], 1.5 * speedup[0])

    def test_a_wrapping_response_separates_the_backends(self):
        """The example's reason to reach for the spectral path."""
        self.assertGreater(self.ns["wrapped_gap"], 1e-3)
        self.assertGreater(self.ns["wrapped_gap"], 1e6 * self.ns["agreement"])

    def test_a_sub_bin_shift_stays_positive_and_interpolates(self):
        """Positivity is the load-bearing claim — a Poisson fit needs it.

        The interpolation claim carries a tolerance on purpose: band-limited
        interpolation rings, so demanding strict betweenness would assert
        something the method does not do. The bound is on the *ringing* — small
        against the peak, and non-zero, because a shift that never overshoots is
        a roll and not an interpolation at all.
        """
        self.assertGreater(self.ns["shifted"].min(), 0.0)
        self.assertTrue(self.ns["interpolates"])
        self.assertGreater(self.ns["overshoot"], 0.0)
        self.assertLess(self.ns["overshoot"] / self.ns["unshifted"].max(), 1e-3)

    def test_a_whole_bin_shift_is_a_roll(self):
        """Pins the shift's meaning, which the fractional case rests on."""
        np.testing.assert_allclose(
            self.ns["one_bin"], np.roll(self.ns["unshifted"], 1),
            rtol=0, atol=1e-5 * self.ns["unshifted"].max())


if __name__ == "__main__":
    unittest.main()
