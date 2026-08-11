"""2D-FDC as a *method*, against simulated streams whose answer is known first.

PRD-036 makes this a requirement rather than a nicety, and the reason is
specific: a kernel checked only against the implementation it replaces cannot
tell a faithful port from a shared mistake. PRD-035 walked into exactly that —
a fixture recorded from the code being replaced encoded a live `nan` bug, and
reproducing it would have been "passing".

So the streams here come from this library's own `SimEngine` with the answer
fixed in advance: known lifetimes, a known rate matrix, and known equilibrium.
The kernel never sees which state emitted which photon.

**The statistic.** A 2D-FDC matrix `M` is a joint distribution over the two
micro-times of a photon pair. If the emitter's state cannot change between the
two photons, the pair is correlated and `M` differs from the product of its own
marginals; once the state has fully relaxed, `M` *is* that product. So

    D(dT) = total variation between M/N and outer(row marginal, column marginal)

is zero when the two photons are independent and positive when they are not,
without assuming anything about lifetimes, bin edges or normalisation. Its decay
along `dT` is the interconversion, which is the quantity 2D-FLC exists to
measure.

**What is deliberately not tested here.** PRD-036 lists "invert the diagonal and
see two peaks at the simulated lifetimes". The inversions — Tikhonov, MEM, the
rate-matrix fit — are explicitly *not* moving into this library, so a test of
them would be a test of SciPy wearing this file's name. What is tested instead
is the property that makes such an inversion possible at all: the matrix
separates the two lifetimes, checked as a difference in conditional means. If
the inversions ever do land here, the peak test belongs with them.

**Single molecule, on purpose.** With several emitters in the focus at once
most pairs come from *different* molecules, which are independent by
construction, and the coupling drops into the noise — measured at 8 emitters
before these tests were written. 2D-FLC is a single-molecule method and the
simulation reflects that.
"""

import unittest

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
if not hasattr(tttrlib, "SimEngine"):
    pytest.skip("tttrlib built without the photon simulator", allow_module_level=True)

N_MICRO = 256          # micro-time channels
MICRO_DT = 0.032       # ns per channel
WINDOW_DT = 0.01       # time units per macro window
L = 12                 # log bins on each axis of the matrix
DDT = 8                # lag window width, in macro windows


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def simulate(lifetimes, k01=0.0, k10=0.0, initial=0, windows=150_000, seed=5):
    """One immobile molecule, `lifetimes` states, exchanging at (k01, k10).

    Rates are per time unit and a window is `WINDOW_DT`, so the relaxation
    `1/(k01+k10)` is `1/((k01+k10)*WINDOW_DT)` windows -- the number the lag
    axis has to straddle for any of this to be visible.
    """
    system = tttrlib.SimSystem()
    for tau in lifetimes:
        species = tttrlib.SimSpecies()
        species.D = 0.0
        species.q = _vd([200.0])
        species.decay = tttrlib.SimDecay.multi_exponential(
            _vd([1.0]), _vd([tau]), N_MICRO, MICRO_DT)
        system.add_species(species)
    n = len(lifetimes)
    system.set_rate_matrices(
        _vd([0.0] * n * n),                                  # no light-driven transitions
        _vd([0.0, k01, k10, 0.0]) if n == 2 else _vd([0.0]))  # row-major i->j
    system.set_background(_vd([0.0]))
    system.set_box(50.0, 50.0)
    system.add_fluorophore(0.0, 0.0, 0.0, initial, False)

    integrator = tttrlib.SimIntegrator()
    integrator.dt = WINDOW_DT
    integrator.n_channels = 1
    integrator.n_ph_max = 10 ** 12
    integrator.max_windows = windows
    integrator.n_microtime_channels = N_MICRO
    integrator.microtime_resolution = MICRO_DT
    integrator.laser_period = N_MICRO * MICRO_DT
    integrator.seed_diffusion = seed
    integrator.seed_emission = seed + 1

    engine = tttrlib.SimEngine(
        system, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
        tttrlib.VectorSimGrid([]), integrator)
    engine.run()
    return (np.asarray(engine.macro_window(), dtype=np.int64),
            np.asarray(engine.micro_time(), dtype=np.int64))


def matrices(macro, micro, lags, ddT=DDT, n_chunks=4):
    lags = np.asarray(lags, dtype=np.int64)
    out = np.zeros(lags.size * L * L, dtype=np.int64)
    tttrlib.fdc_scan_log(macro, micro, lags, ddT, 0, N_MICRO - 1, L, n_chunks, out)
    return out.reshape(lags.size, L, L)


def coupling(matrix):
    """Total variation between the pair distribution and its own marginals."""
    total = matrix.sum()
    assert total > 10_000, "too few pairs for this to mean anything"
    p = matrix.astype(float) / total
    return 0.5 * np.abs(p - np.outer(p.sum(1), p.sum(0))).sum()


def fitted_relaxation(lags, d, baseline):
    """Log-linear fit of `d - baseline` over the points that stand above noise."""
    y = d - baseline
    usable = y > 0.25 * y[0]
    assert usable.sum() >= 3, "the coupling decayed too fast to fit"
    slope, intercept = np.polyfit(np.asarray(lags)[usable], np.log(y[usable]), 1)
    return -1.0 / slope


#: All lags exceed DDT/2, so a reference photon is never inside its own window --
#: self-pairs would otherwise put a spike on the diagonal that has nothing to do
#: with the kinetics. Found by measuring, not by reasoning.
LAGS = [10, 25, 50, 100, 200, 400, 800, 1600]


class TestTheMethod(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        # One molecule per state, run separately and laid end to end: an
        # ensemble of two *frozen* populations. A single k=0 molecule would
        # never leave its starting state, which is a one-state sample wearing a
        # two-state configuration -- it produced numbers identical to the
        # single-state control, which is how the mistake was caught.
        a = simulate([1.0, 4.0], initial=0, seed=5)
        b = simulate([1.0, 4.0], initial=1, seed=9)
        cls.frozen = (np.concatenate([a[0], b[0] + a[0][-1] + 10_000]),
                      np.concatenate([a[1], b[1]]))
        cls.slow = simulate([1.0, 4.0], k01=0.5, k10=0.5)      # relax = 100 windows
        cls.fast = simulate([1.0, 4.0], k01=1.0, k10=1.0)      # relax =  50 windows
        cls.one_state = simulate([1.0])

    def d_curve(self, stream):
        return np.asarray([coupling(m) for m in matrices(*stream, LAGS)])

    def test_a_single_state_never_produces_a_cross_peak(self):
        """The negative control. Without it, a kernel that fabricates
        correlation passes every other test in this file."""
        d = self.d_curve(self.one_state)
        self.assertLess(d.max(), 0.005,
                        "one state, yet the pair distribution is not the "
                        "product of its marginals: %s" % np.round(d, 4))

    def test_two_frozen_states_stay_correlated_at_every_lag(self):
        """Nothing interconverts, so the correlation cannot decay -- and it is
        the same measurement that shows a decay when the states do exchange."""
        d = self.d_curve(self.frozen)
        self.assertGreater(d.min(), 0.05, "frozen states lost their correlation")
        self.assertLess(d.max() - d.min(), 0.2 * d.mean(),
                        "a frozen sample should show no lag dependence: %s"
                        % np.round(d, 4))

    def test_the_cross_peak_appears_only_when_the_states_interconvert(self):
        exchanging = self.d_curve(self.slow)
        control = self.d_curve(self.one_state)
        self.assertGreater(exchanging[0], 10 * control.max(),
                           "no correlation at short lag in an exchanging sample")
        self.assertLess(exchanging[-1], 0.1 * exchanging[0],
                        "the correlation never decayed: %s" % np.round(exchanging, 4))

    def test_the_lag_dependence_recovers_the_simulated_rate(self):
        """The measurement 2D-FLC exists for: relaxation from the lag axis."""
        for label, stream, k in (("slow", self.slow, 0.5), ("fast", self.fast, 1.0)):
            with self.subTest(case=label):
                d = self.d_curve(stream)
                expected = 1.0 / (2 * k) / WINDOW_DT        # windows
                got = fitted_relaxation(LAGS, d, baseline=d[-1])
                self.assertLess(abs(got - expected) / expected, 0.4,
                                "fitted relaxation %.0f windows against a "
                                "simulated %.0f" % (got, expected))

    def test_halving_the_dwell_time_halves_the_relaxation(self):
        """Ratios survive what absolute calibration does not -- a systematic
        error in the statistic cancels here and a wrong lag axis does not."""
        slow = fitted_relaxation(LAGS, self.d_curve(self.slow), baseline=0.0015)
        fast = fitted_relaxation(LAGS, self.d_curve(self.fast), baseline=0.0015)
        self.assertLess(abs(slow / fast - 2.0), 0.6,
                        "relaxation %.0f vs %.0f windows, expected a factor 2"
                        % (slow, fast))

    def test_the_matrix_separates_the_two_lifetimes(self):
        """What makes an inversion possible, without performing one.

        Conditioned on a reference photon in an early micro-time bin, its
        partners arrive earlier on average than when conditioned on a late bin.
        A matrix that mixed the states would show the same conditional mean
        everywhere.
        """
        m = matrices(*self.frozen, [25])[0].astype(float)
        rows = m.sum(axis=1)
        occupied = np.flatnonzero(rows > 0.02 * rows.max())
        bins = np.arange(L)
        conditional = np.asarray([(m[i] * bins).sum() / m[i].sum() for i in occupied])
        self.assertGreater(conditional[-1] - conditional[0], 0.5,
                           "the partner's mean micro-time bin does not depend on "
                           "the reference's: %s" % np.round(conditional, 2))


if __name__ == "__main__":
    unittest.main()
