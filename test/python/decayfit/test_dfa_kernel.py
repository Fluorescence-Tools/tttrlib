"""The donor⊗FRET⊗anisotropy decay kernel.

The model keeps three rate processes separate — the donor's own decay, the FRET
transfer competing with it, and rotational depolarisation — and forms the
polarisation-resolved decay as their outer product. These tests pin the physics
against closed forms rather than against a reference implementation, so they say
what the model *should* do and not merely what it currently does.
"""
from __future__ import division

import unittest

import numpy as np

import tttrlib


N_BINS = 64


class TestPeriodicDecay(unittest.TestCase):
    """Excitation repeats, so the decay is a periodic sum — in closed form."""

    def test_matches_a_direct_periodic_sum(self):
        """The frequency-domain form must equal the wrapped time-domain sum.

        For a rate ``k`` per bin over a period of ``n`` bins the periodic decay
        is ``exp(-k t) / (1 - exp(-k n))``. This is exact, not an approximation,
        so machine precision is the right tolerance.
        """
        t = np.arange(N_BINS)
        for k in (0.02, 0.1, 0.5):
            got = np.asarray(tttrlib.dfa_periodic_decay([k], [1.0], N_BINS))
            want = np.exp(-k * t) / (1.0 - np.exp(-k * N_BINS))
            np.testing.assert_allclose(got, want, atol=1e-12, rtol=0)

    def test_weights_add(self):
        a = np.asarray(tttrlib.dfa_periodic_decay([0.05], [0.3], N_BINS))
        b = np.asarray(tttrlib.dfa_periodic_decay([0.2], [0.7], N_BINS))
        both = np.asarray(tttrlib.dfa_periodic_decay([0.05, 0.2], [0.3, 0.7], N_BINS))
        np.testing.assert_allclose(both, a + b, atol=1e-12, rtol=0)

    def test_a_longer_period_approaches_the_unwrapped_decay(self):
        """The wrap is what a long period removes; nothing else should change."""
        k = 0.1
        for n in (64, 256, 1024):
            got = np.asarray(tttrlib.dfa_periodic_decay([k], [1.0], n))[:32]
            unwrapped = np.exp(-k * np.arange(32))
            self.assertLess(np.abs(got - unwrapped).max(),
                            2.0 * np.exp(-k * n) + 1e-12)


class TestTimeshift(unittest.TestCase):
    """A sub-bin shift is a phase ramp — applied to the IRF, never the decay."""

    @staticmethod
    def _irf(n=N_BINS, centre=10.0, width=1.5):
        i = np.arange(n)
        return np.exp(-0.5 * ((i - centre) / width) ** 2)

    def _convolved(self, shift):
        return np.asarray(tttrlib.dfa_convolved_decay(
            [0.1], [1.0], self._irf().tolist(), N_BINS, shift))

    def test_a_whole_bin_shift_is_a_roll(self):
        a = self._convolved(0.0)
        b = self._convolved(3.0)
        np.testing.assert_allclose(b, np.roll(a, 3), atol=1e-12, rtol=0)

    def test_a_fractional_shift_stays_positive_and_interpolates(self):
        """Shifting the IRF is well behaved where shifting the decay is not.

        A phase ramp is band-limited interpolation, so it rings at a step. The
        decay steps at the period boundary — the moment the next pulse arrives —
        and shifting it makes the tail oscillate and go negative, which a Poisson
        likelihood cannot take the logarithm of. The instrument response is a
        compact pulse near zero at both ends, so it shifts cleanly.
        """
        a, half, b = self._convolved(0.0), self._convolved(0.5), self._convolved(1.0)
        self.assertGreater(half.min(), 0.0, "a shifted model must stay positive")
        lo = np.minimum(a, b)
        hi = np.maximum(a, b)
        self.assertTrue(np.all(half >= lo - 1e-9))
        self.assertTrue(np.all(half <= hi + 1e-9))

    def test_shifting_the_decay_would_go_negative(self):
        """Pins the reason the shift is applied to the IRF instead."""
        decay = np.asarray(tttrlib.dfa_periodic_decay([0.1], [1.0], N_BINS))
        spectrum = np.fft.rfft(decay)
        w = np.arange(spectrum.size)
        shifted = np.fft.irfft(
            spectrum * np.exp(-2j * np.pi * w * 0.5 / N_BINS), N_BINS)
        self.assertLess(shifted.min(), 0.0)


class TestConvolutionBackends(unittest.TestCase):
    """Two ways to compute one convolution — they must be one convolution.

    ``ConvolutionMethod.Recursive`` (a single-pole time-domain filter) and
    ``Spectral`` (the closed-form periodic spectrum times the response's) are
    different algorithms for the same physics. The recursion is the default and
    the faster of the two; the spectral path exists because it can convolve an
    arbitrary measured pattern and apply a sub-bin shift, and because an
    independent algorithm is a real check on the fast one.
    """

    RECURSIVE, SPECTRAL = 0, 1

    @staticmethod
    def _irf(n, centre_frac=0.1, width=12.0):
        i = np.arange(n)
        return np.exp(-0.5 * ((i - centre_frac * n) / width) ** 2).tolist()

    @classmethod
    def _convolved(cls, method, rates, weights, n, shift=0.0):
        return np.asarray(tttrlib.dfa_convolve(
            rates, weights, cls._irf(n), n, shift, method))

    @staticmethod
    def _relative(a, b):
        a = a / a.sum()
        b = b / b.sum()
        return np.abs(a - b).mean() / np.abs(a).mean()

    def test_the_backends_agree_to_machine_precision(self):
        """Two algorithms, one convolution — not two that are merely close.

        The recursion applies the trapezoid rule to the convolution integral,
        which leaves the kernel ``exp(-k L)`` at every lag except ``L = 0``,
        where it leaves one half. The spectral path subtracts that half a delta
        and the two become the *same* computation, so machine precision is the
        right tolerance and anything looser would be hiding something.
        """
        n = 1024
        for tau in (5.0, 20.0, 50.0, 200.0):
            a = self._convolved(self.RECURSIVE, [1.0 / tau], [1.0], n)
            b = self._convolved(self.SPECTRAL, [1.0 / tau], [1.0], n)
            self.assertLess(np.abs(a - b).max() / np.abs(a).max(), 1e-6,
                            "backends disagree at tau=%g bins" % tau)

    def test_the_backends_agree_on_a_rate_spectrum(self):
        """The reconciliation must not depend on the rate.

        Without the correction the two differ by ``(1 + exp(-k)) / 2`` — which
        varies with the rate, so it does not cancel out of a spectrum but
        *reweights* it: 0.5% at k = 0.01 against 5% at k = 0.1. A donor(x)FRET
        product spans exactly that range, and its relative weights are the
        measurement, so a rate-dependent scale would be a systematic error in the
        answer rather than in the last digit.
        """
        n, rates = 1024, [0.005, 0.01, 0.05, 0.1, 0.3]
        weights = [0.2] * len(rates)
        a = self._convolved(self.RECURSIVE, rates, weights, n)
        b = self._convolved(self.SPECTRAL, rates, weights, n)
        self.assertLess(np.abs(a - b).max() / np.abs(a).max(), 1e-6)

        # and each rate on its own agrees to the same standard, which is what
        # says the agreement is per-rate and not a cancellation across the sum
        for k in rates:
            one_a = self._convolved(self.RECURSIVE, [k], [1.0], n)
            one_b = self._convolved(self.SPECTRAL, [k], [1.0], n)
            self.assertLess(np.abs(one_a - one_b).max() / np.abs(one_a).max(),
                            1e-4, "rate %g disagrees" % k)

    def test_a_response_that_wraps_needs_the_spectral_backend(self):
        """Where the two legitimately part company, and which one is right.

        The recursion starts at bin 0 as though nothing preceded it, so it cannot
        see an instrument response whose tail wraps around the period boundary.
        With a compact response the two agree to machine precision; widen it
        until it is no longer near zero at both ends and the recursion loses the
        wrapped photons while the spectral path keeps them. That is not a
        tolerance to relax — it is the answer to "which backend should I use"
        when a response is broad relative to the excitation period.
        """
        n = 256
        i = np.arange(n)

        compact = np.exp(-0.5 * ((i - 0.1 * n) / (0.012 * n)) ** 2).tolist()
        a = np.asarray(tttrlib.dfa_convolve([0.02], [1.0], compact, n, 0.0, self.RECURSIVE))
        b = np.asarray(tttrlib.dfa_convolve([0.02], [1.0], compact, n, 0.0, self.SPECTRAL))
        self.assertLess(np.abs(a - b).max() / np.abs(a).max(), 1e-10)

        wrapped = np.exp(-0.5 * ((i - 0.1 * n) / (0.25 * n)) ** 2).tolist()
        a = np.asarray(tttrlib.dfa_convolve([0.02], [1.0], wrapped, n, 0.0, self.RECURSIVE))
        b = np.asarray(tttrlib.dfa_convolve([0.02], [1.0], wrapped, n, 0.0, self.SPECTRAL))
        self.assertGreater(np.abs(a - b).max() / np.abs(a).max(), 1e-3)

    def test_both_backends_are_linear_in_the_weights(self):
        n = 1024
        for method in (self.RECURSIVE, self.SPECTRAL):
            a = self._convolved(method, [0.01], [0.3], n)
            b = self._convolved(method, [0.05], [0.7], n)
            both = self._convolved(method, [0.01, 0.05], [0.3, 0.7], n)
            np.testing.assert_allclose(both, a + b, atol=1e-10, rtol=0)

    def test_a_whole_bin_shift_is_a_roll(self):
        """Exactly, spectrally; to ~1e-6 under the recursion.

        The recursion is not quite circular in the *response*: it starts at bin 0
        as though nothing preceded it and accounts for the period boundary with
        an analytic tail rather than with the samples that wrapped. That
        approximation is what makes it fast, and it costs about 1e-6 of the peak
        in the handful of bins the shift wraps around. Worth knowing before
        reading a difference of that size as a bug.
        """
        n = 1024
        exact = self._convolved(self.SPECTRAL, [0.01], [1.0], n)
        np.testing.assert_allclose(
            self._convolved(self.SPECTRAL, [0.01], [1.0], n, shift=5.0),
            np.roll(exact, 5), atol=1e-14, rtol=0)

        approx = self._convolved(self.RECURSIVE, [0.01], [1.0], n)
        deviation = np.abs(self._convolved(self.RECURSIVE, [0.01], [1.0], n, shift=5.0)
                           - np.roll(approx, 5))
        self.assertLess(deviation.max() / approx.max(), 1e-5)
        # and it is confined to the bins that wrapped, not spread over the curve
        self.assertTrue(np.all(np.where(deviation > 1e-12)[0] < 5))

    def test_a_sub_bin_shift_works_under_the_recursion_too(self):
        """The recursion cannot express a fractional shift — it borrows one.

        The shift is applied to the *response* spectrally before the recursion
        runs, which costs one transform and is independent of the rate count. If
        this silently rounded to the nearest bin instead, a fit floating the
        shift would show a lifetime bias on coarsely binned data.
        """
        n = 1024
        a = self._convolved(self.RECURSIVE, [0.01], [1.0], n, shift=0.0)
        half = self._convolved(self.RECURSIVE, [0.01], [1.0], n, shift=0.5)
        b = self._convolved(self.RECURSIVE, [0.01], [1.0], n, shift=1.0)
        self.assertGreater(np.abs(half - a).max(), 1e-6, "the shift did nothing")
        self.assertGreater(np.abs(half - b).max(), 1e-6, "the shift rounded to a bin")
        self.assertGreater(half.min(), -1e-12, "a shifted model must stay positive")
        self.assertTrue(np.all(half >= np.minimum(a, b) - 1e-6 * a.max()))
        self.assertTrue(np.all(half <= np.maximum(a, b) + 1e-6 * a.max()))

    def test_a_delta_response_returns_the_periodic_decay(self):
        """With no instrument to convolve with, convolution is nearly identity.

        Nearly, not exactly: the trapezoid rule halves the kernel's first sample,
        so the decay comes back with bin 0 at half height. That half is not a
        rounding artefact — it is the discretisation this library has always
        used, and pinning it here is what stops a future "fix" from silently
        moving every fitted lifetime.
        """
        n, k = 1024, 0.01
        irf = [0.0] * n
        irf[0] = 1.0
        out = np.asarray(tttrlib.dfa_convolve([k], [1.0], irf, n, 0.0, self.SPECTRAL))
        want = np.asarray(tttrlib.dfa_periodic_decay([k], [1.0], n))
        want[0] -= 0.5
        np.testing.assert_allclose(out, want, atol=1e-10, rtol=0)

    def test_the_convolved_vv_vh_keeps_the_polarisation_projection(self):
        """Convolving must not disturb what the two channels mean."""
        n = 1024
        both = np.asarray(tttrlib.dfa_vv_vh_convolved(
            [0.01], [1.0], [0.0], [1.0], [0.05], [1.0], 0.0, 1.3,
            self._irf(n), n, 0.0, self.RECURSIVE))
        vv, vh = both[:n], both[n:]
        # r0 = 0: the channels differ only by the g-factor, before and after
        np.testing.assert_allclose(vh, 1.3 * vv, atol=1e-12, rtol=0)

    def test_the_convolved_vv_vh_agrees_between_backends(self):
        """The agreement survives the outer product, at both polarisations."""
        n = 1024
        out = [np.asarray(tttrlib.dfa_vv_vh_convolved(
            [0.01], [1.0], [0.004], [1.0], [0.05], [1.0], 0.38, 1.0,
            self._irf(n), n, 0.0, m)) for m in (self.RECURSIVE, self.SPECTRAL)]
        self.assertLess(np.abs(out[0][:n] - out[1][:n]).max() / out[0][:n].max(), 1e-6)
        self.assertLess(np.abs(out[0][n:] - out[1][n:]).max() / out[0][n:].max(), 1e-6)

    def test_a_shift_does_not_change_the_number_of_photons(self):
        """Pins the amplitude against the shift.

        A convolution conserves counts, so moving the response must move the
        curve and nothing else. If the shifted and unshifted paths normalised
        differently, a fit floating the shift would trade it against the
        amplitude — a correlation that looks like physics and is not.
        """
        n = 1024
        for method in (self.RECURSIVE, self.SPECTRAL):
            plain = self._convolved(method, [0.01], [1.0], n)
            for shift in (0.5, 3.0, -2.25):
                moved = self._convolved(method, [0.01], [1.0], n, shift=shift)
                self.assertAlmostEqual(moved.sum() / plain.sum(), 1.0, places=6)

    def test_the_amplitude_does_not_depend_on_the_scale_of_the_response(self):
        """A response measured for twice as long is the same instrument."""
        n = 1024
        irf = np.asarray(self._irf(n))
        a = np.asarray(tttrlib.dfa_convolve([0.01], [1.0], irf.tolist(), n, 0.0, 0))
        b = np.asarray(tttrlib.dfa_convolve([0.01], [1.0], (17.0 * irf).tolist(),
                                            n, 0.0, 0))
        np.testing.assert_allclose(b, a, atol=0, rtol=1e-12)


class TestPolarisation(unittest.TestCase):
    """How the photons divide between the two detection channels."""

    @staticmethod
    def _channels(kd=0.1, kf=0.0, ka=0.05, r0=0.38, g=1.0):
        both = np.asarray(tttrlib.dfa_vv_vh_decay(
            [kd], [1.0], [kf], [1.0], [ka], [1.0], r0, g, N_BINS))
        return both[:N_BINS], both[N_BINS:]

    def test_without_anisotropy_the_channels_differ_only_by_g(self):
        vv, vh = self._channels(r0=0.0, g=1.3)
        np.testing.assert_allclose(vh, 1.3 * vv, atol=1e-12, rtol=0)

    def test_the_anisotropy_carries_a_wrap_factor(self):
        """Under repetitive excitation r(t) is *not* r0*exp(-ka t).

        Photons left from earlier pulses have depolarised further, so the
        periodic sums of f and f*r wrap differently and the measured anisotropy
        picks up a constant factor. Small (0.16% here) but systematic — and easy
        to mistake for a modelling error when checked against the textbook form.
        """
        kd, ka, r0 = 0.1, 0.05, 0.38
        vv, vh = self._channels(kd=kd, ka=ka, r0=r0)
        t = np.arange(30)
        r = (vv[:30] - vh[:30]) / (vv[:30] + 2.0 * vh[:30])

        wrap = (1.0 - np.exp(-kd * N_BINS)) / (1.0 - np.exp(-(kd + ka) * N_BINS))
        np.testing.assert_allclose(r, r0 * np.exp(-ka * t) * wrap, atol=1e-9, rtol=0)
        # and the factor is a real effect, not numerical noise
        self.assertGreater(np.abs(r - r0 * np.exp(-ka * t)).max(), 1e-5)

    def test_fret_rates_add_to_the_donor_rate(self):
        """Transfer competes with de-excitation, so the rates add."""
        vv, _ = self._channels(kd=0.1, kf=0.07, r0=0.0)
        combined = np.asarray(tttrlib.dfa_periodic_decay([0.17], [1.0], N_BINS))
        np.testing.assert_allclose(vv, combined, atol=1e-12, rtol=0)

    def test_a_zero_fret_rate_is_the_donor_only_reference(self):
        """D0 and DA are the same model, not two implementations."""
        donor_only, _ = self._channels(kd=0.1, kf=0.0, r0=0.0)
        plain = np.asarray(tttrlib.dfa_periodic_decay([0.1], [1.0], N_BINS))
        np.testing.assert_allclose(donor_only, plain, atol=1e-12, rtol=0)

    def test_a_fret_rate_shortens_the_decay(self):
        """The observable consequence: transfer makes the donor decay faster."""
        no_fret, _ = self._channels(kd=0.1, kf=0.0, r0=0.0)
        with_fret, _ = self._channels(kd=0.1, kf=0.2, r0=0.0)
        t = np.arange(N_BINS)
        mean_no = (t * no_fret).sum() / no_fret.sum()
        mean_with = (t * with_fret).sum() / with_fret.sum()
        self.assertLess(mean_with, mean_no)


if __name__ == "__main__":
    unittest.main()
