"""Runs the convolution-methods tutorial and checks what it claims.

What the example is about
-------------------------
Fitting a fluorescence decay means convolving a model decay with the measured
instrument response (IRF) once per iteration, so this convolution is the inner
loop of every lifetime fit. ``dfa_convolve(rates, weights, irf, n_bins,
shift_bins, method)`` builds the decay from a rate spectrum and convolves it,
and it offers two ways to do that:

``RECURSIVE`` (0)
    Time domain, and the default: one multiply-add per rate and per bin.
``SPECTRAL`` (1)
    Frequency domain: the periodic decay has a closed form there, costing one
    complex division per rate and per frequency.

Both are ``O(n_bins * n_rates)``, so the transform buys no better scaling --
only different constants and different edge behaviour. That is the tutorial's
point, and it argues against the received wisdom that an FFT is the obvious way
to convolve.

What is asserted here
---------------------
The example is a gallery script, so `setUpClass` executes it with `runpy` and
each test reads a value out of the namespace it left behind. Running it is not
enough on its own: an API change can leave a script running happily while its
figures say something false, so every factual claim it makes gets an assertion.

The names that come out of the example, and what they mean:

``agreement``
    max |recursive - spectral| / peak, for a compact IRF. Both backends compute
    the same mathematics, so this is a numerical-equivalence check.
``speedup``
    ``t_spectral / t_recursive`` at 1, 2, 4, ..., 64 rates -- **a ratio of
    times, not a factor of goodness**. It is above 1 when the spectral path is
    the SLOWER one, which is the case the example is making.
``wrapped_gap``
    The same normalised difference for a *broad* IRF whose tail wraps around the
    excitation period. The recursion starts at bin 0 as though nothing preceded
    it and cannot see that wrap, so here the two backends must NOT agree.
``shifted`` / ``unshifted`` / ``one_bin``
    The decay convolved with ``shift_bins`` of 0.5, 0.0 and 1.0. A fractional
    shift is how a sub-bin IRF offset is fitted without rounding it to a whole
    bin, which would bias the lifetime when bins are coarse.
``overshoot`` / ``interpolates``
    How far ``shifted`` strays outside its two integer neighbours, and whether
    that stays inside the example's tolerance.
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
        """Same mathematics, two algorithms: the answers must be the same.

        The first figure overlays the two convolutions and plots their
        difference. If this ever fails, one backend has a genuine numerical
        bug -- the choice between them is supposed to be about speed and edge
        cases, never about the answer. Compact IRF, so no wrap is involved.
        """
        self.assertLess(self.ns["agreement"], 1e-10)

    def test_the_recursion_is_faster_at_every_rate_count(self):
        """The tutorial's practical advice: use the default, the recursion.

        `speedup` is t_spectral / t_recursive at each rate count, so every entry
        must be above 1 for "the recursion wins everywhere" to hold. This is the
        claim a reader acts on when choosing a backend.

        This assertion was split in two on 2026-08-11 because it inverted at one
        and two rates under load (0.83x with a compile running) where the gap was
        a few percent. The gap was small for a reason that had nothing to do with
        either backend: `dfa_convolve` marshalled its arrays through the Python
        sequence protocol, and at small rate counts that wrapper *was* the
        runtime, so both backends were measuring the same conversion. With the
        NumPy typemaps the smallest point is **4.1x**, not 1.02x, and a loaded
        machine does not invert that. One claim again, strictly, everywhere.
        """
        speedup = np.asarray(self.ns["speedup"])
        self.assertTrue(np.all(speedup > 1.0),
                        "the text says the recursion wins everywhere: %s" % speedup)

    def test_the_spectral_penalty_never_goes_away(self):
        """The spectral path must stay clearly behind at every rate count.

        The tutorial's deeper argument is about the regime a real model lives
        in: a donor (x) FRET (x) anisotropy decay has a rate spectrum that is an
        outer product, so tens of rates is normal, and that is exactly where the
        frequency domain is at its worst. This asserts the part of that which is
        true on every machine -- the penalty is large and never shrinks away --
        rather than the exact shape of the curve.

        The text says the gap *widens* with the rate count, and it does on the
        machine it was written on: the ratio runs 4.0x at one rate to 7.1x at
        sixty-four, a head-to-tail factor of 1.74 (unchanged with
        OMP_NUM_THREADS=1, so it is not threading, and unchanged when the call
        overhead is amortised over 200 iterations, so it is not the timer).

        On GitHub's linux runners the same code gives 3.6x at one rate and 3.2x
        at sixty-four -- the head matches, the tail does not, so the spectral
        path scales *better* there and the factor is 0.88. That is a real
        difference between machines, not noise: it reproduced on all five
        pythons, while macOS and Windows agreed with the author.

        So head-to-tail is not a property of this library and is not asserted
        here. What every machine agrees on is that the penalty is large and
        never shrinks to nothing, which is what the example is really for: if
        the spectral path ever became competitive, this fires.
        """
        speedup = np.asarray(self.ns["speedup"])
        self.assertGreater(speedup.min(), 2.0,
                           "the spectral path should stay well behind: %s" % speedup)

    def test_a_wrapping_response_separates_the_backends(self):
        """When the transform is not merely slower but necessary.

        With a broad IRF whose tail wraps around the excitation period, the two
        backends must DISAGREE: the recursion begins at bin 0 as though nothing
        preceded it and so misses the wrapped tail, while the spectral path is
        periodic by construction and sees it.

        Both bounds matter. The absolute one says the disagreement is real
        rather than rounding; the comparison against `agreement` says it is a
        million times larger than the two backends' disagreement on a compact
        IRF, which is what makes it a capability difference and not noise.
        """
        self.assertGreater(self.ns["wrapped_gap"], 1e-3)
        self.assertGreater(self.ns["wrapped_gap"], 1e6 * self.ns["agreement"])

    def test_a_sub_bin_shift_stays_positive_and_interpolates(self):
        """A fractional IRF shift must behave like a shift, not like ringing.

        `shift_bins=0.5` is how a sub-bin instrument offset is fitted; rounding
        it to a whole bin biases the fitted lifetime when bins are coarse. The
        result has to stay usable as a model: strictly positive, because a
        Poisson likelihood takes its logarithm, and between its two integer
        neighbours, because that is what interpolating means.

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
        """The integer case, which gives `shift_bins` its meaning.

        A shift of exactly one bin must equal np.roll by one. Without this the
        fractional test above would be checking interpolation between two
        points that are themselves undefined.
        """
        np.testing.assert_allclose(
            self.ns["one_bin"], np.roll(self.ns["unshifted"], 1),
            rtol=0, atol=1e-5 * self.ns["unshifted"].max())


if __name__ == "__main__":
    unittest.main()
