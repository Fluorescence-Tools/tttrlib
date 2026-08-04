#!/usr/bin/env python3
"""The product alphabet: a symbol is `stream * n_micro_bins + micro_bin`.

The engine's recursions read `obs[i*p + y]` and never ask what `y` means, so a
micro-time axis is a *wider emission table*, not a second code path.  Two things
have to hold for that claim to be worth anything, and both are tested here.

**One bin must change nothing.**  `set_bursts_micro(..., n_micro_bins=1)` has to
be bit-identical to `set_bursts` — same fit, same numbers.  It is the same
guarantee the flat-restraint test makes for MAP-vs-MLE, and for the same reason:
one shared loop is only safe if the degenerate case is provably the old case.

**More bins must buy something specific.**  The negative control is a dark
acceptor against real FRET.  Both states emit donor and acceptor in exactly the
same *ratio*, so on the stream-only alphabet they are not merely hard to tell
apart — they are identical, and no amount of data would separate them.  Only the
donor *lifetime* differs: unquenched at 4 ns, quenched to 2 ns by transfer.  The
lifetime axis is the entire signal, which is what makes this the right test for
whether the axis works at all.
"""
import unittest

import numpy as np

import tttrlib


N_BINS = 32
DT_NS = 0.5          # 16 ns axis -- 4 unquenched donor lifetimes
TAU_DARK = 4.0       # donor unquenched: acceptor is dark
TAU_FRET = 2.0       # donor quenched by transfer (E = 0.5)
TAU_ACCEPTOR = 2.5


def _true_model(p_acceptor=0.5):
    """Two states with an identical stream ratio and different donor lifetimes."""
    spec = tttrlib.HmmEmissionSpec.uniform(2, 2, N_BINS, DT_NS, TAU_DARK)
    for state, tau in enumerate((TAU_DARK, TAU_FRET)):
        spec.set_stream_probability(state, 0, 1.0 - p_acceptor)
        spec.set_stream_probability(state, 1, p_acceptor)
        spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
        spec.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))
    model = tttrlib.HmmModel([0.5, 0.5], [0.999, 0.001, 0.001, 0.999], spec.build())
    model.n_micro_bins = N_BINS
    return model


def _simulate(model, seed=3, n_bursts=40, burst_len=400):
    """Simulate photons **and keep the state path**.

    `HMM.simulate_bursts` discards the trajectory, and the trajectory is the
    only thing that can measure whether an alphabet carries state information:
    without it the best one can do is watch EM and hope, and on a
    ratio-degenerate problem EM has degenerate optima to escape into.  So the
    chain is walked here, propagating with `A^dt` exactly as the engine does.
    """
    rng = np.random.default_rng(seed)
    prior = model.prior_np
    A = model.trans_np
    obs = model.obs_np
    n = model.n_states()

    times, symbols, paths = [], [], []
    powers = {}
    for _ in range(n_bursts):
        t = np.cumsum(rng.integers(1, 40, size=burst_len)).astype(np.int64)
        state = rng.choice(n, p=prior)
        bs, bp = [], []
        for k in range(burst_len):
            if k:
                dt = int(t[k] - t[k - 1])
                if dt not in powers:
                    powers[dt] = np.linalg.matrix_power(A, dt)
                state = rng.choice(n, p=powers[dt][state])
            bp.append(state)
            bs.append(int(rng.choice(obs.shape[1], p=obs[state])))
        times.append(t.tolist())
        symbols.append(bs)
        paths.append(bp)
    return times, symbols, np.concatenate(paths)


def _accuracy(path, truth):
    """Per-photon accuracy, best of both labelings -- states are exchangeable."""
    path = np.asarray(path)
    return max(float((path == truth).mean()), float((1 - path == truth).mean()))


def _split(symbols, n_micro_bins):
    """Product symbols back into (stream, bin), the way a loader would."""
    streams = [[y // n_micro_bins for y in b] for b in symbols]
    bins = [[y % n_micro_bins for y in b] for b in symbols]
    return streams, bins


class TestOneBinIsTheOldEngine(unittest.TestCase):
    """`n_micro_bins == 1` is the stream-only alphabet, bit for bit."""

    def test_bit_identical_to_set_bursts(self):
        rng = np.random.default_rng(7)
        true = tttrlib.HmmModel([0.5, 0.5],
                                [0.999, 0.001, 0.002, 0.998],
                                [0.80, 0.20, 0.25, 0.75])
        times = [np.cumsum(rng.integers(1, 60, size=200)).astype(np.int64).tolist()
                 for _ in range(30)]
        streams = [list(s) for s in tttrlib.HMM.simulate_bursts(true, times, 8)]
        zeros = [[0] * len(b) for b in streams]

        plain = tttrlib.HMM()
        plain.set_bursts(times, streams, 2)
        micro = tttrlib.HMM()
        micro.set_bursts_micro(times, streams, zeros, 2, 1)

        self.assertEqual(micro.get_n_symbols(), 2)
        self.assertEqual(micro.get_n_micro_bins(), 1)
        np.testing.assert_array_equal(plain.get_streams(), micro.get_streams())

        init = tttrlib.HMM.factory_model(2, 2, 1e-3, 0)
        a = plain.optimize(init, 300, 1e-10)
        b = micro.optimize(init, 300, 1e-10)
        self.assertEqual(a.loglik, b.loglik)
        np.testing.assert_array_equal(a.obs_np, b.obs_np)
        np.testing.assert_array_equal(a.trans_np, b.trans_np)


class TestProductAlphabet(unittest.TestCase):

    def test_symbols_are_stream_major(self):
        """The layout is `stream * n_micro_bins + bin`, and nothing else works.

        `obs_micro_np` unflattens on that assumption, so if the loader ever
        packed bin-major the two would disagree silently -- the fit would still
        converge, to a model whose states mean nothing.
        """
        eng = tttrlib.HMM()
        times = [[0, 10, 20, 30]]
        streams = [[0, 1, 0, 1]]
        bins = [[0, 5, 31, 12]]
        eng.set_bursts_micro(times, streams, bins, 2, N_BINS)
        expect = [s * N_BINS + b for s, b in zip(streams[0], bins[0])]
        np.testing.assert_array_equal(eng.get_streams(), expect)
        self.assertEqual(eng.get_n_symbols(), 2 * N_BINS)

    def test_out_of_range_bins_are_clamped_not_dropped(self):
        """A photon past the axis is still a photon.

        Dropping it would not just lose one count -- it would merge two gaps
        into one longer gap, and the transition matrix is inferred from exactly
        those gaps.  Clamping is wrong in the last bin; dropping is wrong in the
        kinetics.
        """
        eng = tttrlib.HMM()
        eng.set_bursts_micro([[0, 10, 20]], [[0, 0, 1]], [[-3, N_BINS + 9, 4]],
                             2, N_BINS)
        self.assertEqual(eng.get_n_photons(), 3)
        np.testing.assert_array_equal(
            eng.get_streams(), [0, N_BINS - 1, N_BINS + 4])

    def test_model_alphabet_must_match_the_data(self):
        """A stream-only model on a micro-time engine reads past its own table."""
        eng = tttrlib.HMM()
        eng.set_bursts_micro([[0, 10, 20]], [[0, 0, 1]], [[1, 2, 3]], 2, N_BINS)
        with self.assertRaises(Exception) as ctx:
            eng.optimize(tttrlib.HMM.factory_model(2, 2), 10, 1e-8)
        self.assertIn("emission columns", str(ctx.exception))


class TestDarkStateVsFret(unittest.TestCase):
    """The negative control: identical ratios, different lifetimes."""

    def setUp(self):
        self.true = _true_model()
        self.times, self.symbols, self.path = _simulate(self.true)
        self.streams, self.bins = _split(self.symbols, N_BINS)

    def _mean_bin(self, model):
        """Mean donor micro-time bin per state -- monotone in the lifetime."""
        donor = model.obs_micro_np[:, 0, :]
        donor = donor / donor.sum(axis=1, keepdims=True)
        return (donor * np.arange(N_BINS)).sum(axis=1)

    def test_stream_only_decodes_at_chance(self):
        """Projected onto streams the two states are the *same* distribution.

        Not "similar" -- identical by construction, so no decoder and no amount
        of data can beat a coin flip.  Measured against the simulated path, not
        against a fit, because a fit of an unidentifiable model reports whatever
        local optimum EM wandered into and would prove nothing either way.
        """
        eng = tttrlib.HMM()
        eng.set_bursts(self.times, self.streams, 2)
        flat = tttrlib.HmmModel(
            list(self.true.prior_np), list(self.true.trans_np.ravel()),
            [0.5, 0.5, 0.5, 0.5])
        path, _ = eng.viterbi_path(flat)
        acc = _accuracy(path, self.path)
        self.assertLess(acc, 0.6, f"streams alone should decode at chance: {acc}")

    def test_micro_time_decodes_well_above_chance(self):
        """The same photons, the same states -- with the lifetime axis added."""
        eng = tttrlib.HMM()
        eng.set_bursts_micro(self.times, self.streams, self.bins, 2, N_BINS, DT_NS)
        self.assertEqual(eng.get_n_symbols(), 2 * N_BINS)
        self.assertAlmostEqual(eng.get_micro_time_bin_width_ns(), DT_NS)

        path, _ = eng.viterbi_path(self.true)
        acc = _accuracy(path, self.path)
        self.assertGreater(acc, 0.75, f"micro-time should separate the states: {acc}")

    def test_em_from_the_truth_stays_at_the_truth(self):
        """The optimum is where the truth is -- so the *scoring* is wired right.

        This checks the likelihood, not the optimiser, which is why it seeds at
        the truth and why it uses a coarse alphabet: 32 bins is the benign
        regime.  It is emphatically **not** a claim that EM is safe here -- see
        `TestFreeEmMayDestroyACorrectSolution`, which pins the opposite at finer
        binning.
        """
        eng = tttrlib.HMM()
        eng.set_bursts_micro(self.times, self.streams, self.bins, 2, N_BINS, DT_NS)
        fit = eng.optimize(self.true, 500, 1e-9)

        self.assertEqual(fit.n_micro_bins, N_BINS)
        self.assertEqual(fit.n_streams(), 2)
        tab = fit.obs_micro_np
        self.assertEqual(tab.shape, (2, 2, N_BINS))
        np.testing.assert_allclose(tab.sum(axis=(1, 2)), 1.0, atol=1e-9)
        self.assertGreaterEqual(fit.loglik, self.true.loglik)

        # The states stay ratio-degenerate, so the fit did not quietly separate
        # them on intensity -- the lifetimes below are the whole difference.
        ratio = tab.sum(axis=2)[:, 1]
        self.assertLess(abs(ratio[0] - ratio[1]), 0.06, f"ratios: {ratio}")

        np.testing.assert_allclose(
            np.sort(self._mean_bin(fit)), np.sort(self._mean_bin(self.true)),
            atol=0.4)


class TestFreeCategoricalMicroTimeIsNotADecayModel(unittest.TestCase):
    """Micro-time columns are not free parameters, and fitting them as if they
    were is the error -- the bin count is not the mechanism.

    A decay is one smooth function of about two parameters, sampled onto the TAC
    grid.  A free categorical throws that away and lets every bin move
    independently, which admits models no decay can produce: an exact zero in
    the *interior* of an exponential, asserting with infinite confidence that a
    photon cannot arrive at 3 ns while allowing it at 2 and 4.  EM finds those
    because they are in the family -- a state that declares a symbol impossible
    pays nothing for photons it never has to explain, and cannot climb back out.

    So the failure is structural, not a resolution threshold.  Measured over
    four macro-time seeds x four bin counts, three of sixteen runs fell from
    ~0.78 per-photon accuracy to ~0.51 -- and **not monotonically in bins**
    (128 fine, 256 collapsed, 512 mixed, 1024 fine), which is precisely why no
    bin count should be read as a safe one.  More bins only widen an already
    wrong family.

    The tests below therefore assert the *signature* -- interior zeros in a
    decay -- rather than a threshold.  The fix is to keep the emission inside
    the lifetime-spectrum family, where the degenerate solution is unreachable
    because no spectrum can put a zero mid-decay.  **When the parameterised
    M-step lands, invert this**: keep it as the free-path characterisation and
    assert the parameterised path is stable on the same configuration.
    """

    def _run(self, n_bins, seed):
        dt = 16.0 / n_bins
        spec = tttrlib.HmmEmissionSpec.uniform(2, 2, n_bins, dt, TAU_DARK)
        for state, tau in enumerate((TAU_DARK, TAU_FRET)):
            spec.set_stream_probability(state, 0, 0.5)
            spec.set_stream_probability(state, 1, 0.5)
            spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            spec.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))
        true = tttrlib.HmmModel([0.5, 0.5], [0.999, 0.001, 0.001, 0.999],
                                spec.build())
        true.n_micro_bins = n_bins

        times, syms, path = _simulate(true, seed=seed, n_bursts=30, burst_len=400)
        streams, bins = _split(syms, n_bins)
        eng = tttrlib.HMM()
        eng.set_bursts_micro(times, streams, bins, 2, n_bins, dt)
        after = eng.optimize(true, 500, 1e-9)
        return (_accuracy(eng.viterbi_path(true)[0], path),
                _accuracy(eng.viterbi_path(after)[0], path), after)

    @staticmethod
    def _interior_zeros(model, n_bins):
        """Zeros with a non-zero bin on both sides -- impossible for a decay.

        A decay may legitimately underflow in its *tail*, where the density
        really has gone to nothing.  A zero with support either side of it is
        the signature of a free column that happened to receive no expected
        counts, and it cannot be produced by any lifetime spectrum.
        """
        tab = model.obs_micro_np
        n = 0
        for state in range(tab.shape[0]):
            for stream in range(tab.shape[1]):
                row = tab[state, stream]
                nz = np.nonzero(row)[0]
                if nz.size < 2:
                    continue
                n += int((row[nz[0]:nz[-1] + 1] == 0.0).sum())
        return n

    def test_scoring_the_true_table_is_good_at_every_resolution(self):
        """The control: a decay-shaped table is fine wherever it is evaluated.

        Resolution is not the variable -- this is the same physics sampled more
        finely, and it decodes equally well throughout.
        """
        for n_bins in (128, 256, 1024):
            at_truth, _, _ = self._run(n_bins, seed=11)
            self.assertGreater(at_truth, 0.72, f"{n_bins} bins")

    def test_free_em_produces_decays_with_holes_in_them(self):
        """The structural failure, named by its signature rather than a threshold.

        This configuration is one of the measured collapses, and what makes it a
        collapse is not the number it lands on -- it is that the fitted
        "decay" has zeros in its interior, which no exponential has.  Fixed
        seeds, so this characterises deterministically; if it ever stops
        happening the free M-step changed, which is worth knowing either way.
        """
        n_bins = 256
        at_truth, after, model = self._run(n_bins, seed=11)
        self.assertGreater(at_truth, 0.72)
        self.assertLess(after, 0.6)
        self.assertGreater(self._interior_zeros(model, n_bins), 10,
                           "a collapsed fit should have holes inside its decays")

    def _spec(self, n_bins, dt, taus):
        spec = tttrlib.HmmEmissionSpec.uniform(2, 2, n_bins, dt, TAU_DARK)
        for state, tau in enumerate(taus):
            spec.set_stream_probability(state, 0, 0.5)
            spec.set_stream_probability(state, 1, 0.5)
            spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            spec.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))
        return spec

    def test_parameterised_m_step_survives_where_the_free_one_collapses(self):
        """The fix, on the exact configuration that breaks free EM.

        Same data, same E-step; only the M-step differs.  And it starts from a
        *deliberately wrong* seed (8.0 / 0.8 ns against a truth of 4.0 / 2.0),
        so this is not the truth-seeded comparison -- the free path cannot even
        hold the truth here, while the parameterised one finds it from far away.
        """
        n_bins, dt = 256, 16.0 / 256
        at_truth, after_free, _ = self._run(n_bins, seed=11)
        self.assertLess(after_free, 0.6, "expected the known free-EM collapse")

        spec = self._spec(n_bins, dt, (8.0, 0.8))
        init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
        init.n_micro_bins = n_bins

        # Rebuild the same dataset the free path just failed on.
        true = tttrlib.HmmModel([0.5, 0.5], [0.999, 0.001, 0.001, 0.999],
                                self._spec(n_bins, dt, (TAU_DARK, TAU_FRET)).build())
        true.n_micro_bins = n_bins
        times, syms, path = _simulate(true, seed=11, n_bursts=30, burst_len=400)
        streams, bins = _split(syms, n_bins)
        eng = tttrlib.HMM()
        eng.set_bursts_micro(times, streams, bins, 2, n_bins, dt)

        fit = eng.optimize(init, 200, 1e-9, 1e-12, True, False, None, None, spec)

        self.assertGreater(_accuracy(eng.viterbi_path(fit)[0], path), at_truth - 0.02)
        self.assertEqual(self._interior_zeros(fit, n_bins), 0)
        # ...and the spec carries the fitted lifetimes back out.
        got = sorted(spec.spectrum[i * 2 + 0].lifetimes[0] for i in range(2))
        self.assertAlmostEqual(got[0], TAU_FRET, delta=0.35)
        self.assertAlmostEqual(got[1], TAU_DARK, delta=0.45)

    def test_a_spectrum_table_never_has_interior_holes(self):
        """The contrast, and the reason parameterising fixes this by construction.

        Whatever the binning, a lifetime spectrum is positive wherever it has
        support, so the degenerate family is simply not reachable from it.
        """
        for n_bins in (128, 256, 1024):
            _, _, _ = self._run(n_bins, seed=11)
            dt = 16.0 / n_bins
            spec = tttrlib.HmmEmissionSpec.uniform(2, 2, n_bins, dt, TAU_DARK)
            for state, tau in enumerate((TAU_DARK, TAU_FRET)):
                spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            m = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
            m.n_micro_bins = n_bins
            self.assertEqual(self._interior_zeros(m, n_bins), 0, f"{n_bins} bins")


class TestLowCountsBiteTheMstepNotTheScoring(unittest.TestCase):
    """Where photon-counting statistics actually hurt, and where they do not.

    **Scoring needs no Poisson term.**  The likelihood is a product over photons
    of `P(symbol | state)` -- no micro-time histogram is ever formed, so there
    are no bin counts to carry Poisson noise.  Conditioning on the photon count
    `N`, the Poisson likelihood factorises into `Poisson(N)` times exactly this
    per-photon categorical, so the counting statistics *are* handled; the only
    piece left out is the information in `N` itself, which is the photon-rate
    (brightness) term H2MM drops by construction.  Resolution is therefore a
    scoring-accuracy knob and costs nothing statistically.

    **Estimating a free categorical is a different matter**, and this is where a
    sparse histogram reappears: one free number per bin, fitted from expected
    counts that at fine binning are mostly far below 1.  This test pins the
    measurement, because it is the entire justification for parameterising the
    emission rather than leaving the columns free.
    """

    N_BURSTS, BURST_LEN = 20, 300

    def _model(self, n_bins, dt):
        spec = tttrlib.HmmEmissionSpec.uniform(2, 2, n_bins, dt, TAU_DARK)
        for state, tau in enumerate((TAU_DARK, TAU_FRET)):
            spec.set_stream_probability(state, 0, 0.5)
            spec.set_stream_probability(state, 1, 0.5)
            spec.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            spec.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))
        m = tttrlib.HmmModel([0.5, 0.5], [0.999, 0.001, 0.001, 0.999], spec.build())
        m.n_micro_bins = n_bins
        return m

    def _engine(self, n_bins):
        dt = 16.0 / n_bins                     # one 16 ns axis, re-binned
        true = self._model(n_bins, dt)
        times, syms, path = _simulate(true, seed=5, n_bursts=self.N_BURSTS,
                                      burst_len=self.BURST_LEN)
        streams, bins = _split(syms, n_bins)
        eng = tttrlib.HMM()
        eng.set_bursts_micro(times, streams, bins, 2, n_bins, dt)
        return eng, true, path

    def test_spectrum_emission_is_flat_across_resolution(self):
        """Two parameters per state, so there is nothing for sparsity to erode.

        The decay is evaluated, not estimated per bin, so going 32 -> 1024 bins
        changes the photons-per-bin by 32x and the answer by nothing.
        """
        acc = {}
        for n_bins in (32, 1024):
            eng, true, path = self._engine(n_bins)
            decoded, _ = eng.viterbi_path(true)
            acc[n_bins] = _accuracy(decoded, path)
        self.assertGreater(min(acc.values()), 0.75, acc)
        self.assertLess(abs(acc[32] - acc[1024]), 0.05, acc)

    def test_free_categorical_collapses_as_bins_outrun_photons(self):
        """The same photons, the same states, a free emission table -- chance.

        Not a tolerance being missed: the fit lands at coin-flip accuracy while
        a large fraction of its emission table is *exactly zero* (measured ~46%
        of 4096 entries), which is a sparse histogram's MLE and an infinitely
        confident claim that no photon can ever arrive in those bins.
        """
        eng, _, path = self._engine(1024)
        free = eng.fit(2, n_restarts=2, seed=0)

        zeros = int((free.obs_np == 0.0).sum())
        self.assertGreater(zeros, 0.4 * free.obs_np.size,
                           f"expected a largely empty table at 1024 bins: {zeros}")
        self.assertLess(_accuracy(eng.viterbi_path(free)[0], path), 0.6)

    def test_dirichlet_restraints_remove_the_zeros_but_not_the_collapse(self):
        """Smoothing fixes the numerics and does not fix the problem.

        Worth pinning precisely because it is the intuitive remedy: pseudo-counts
        do eliminate every zero, so the `log(0)` hazard is genuinely gone -- and
        accuracy still sits near chance.  The obstacle is not sparsity, it is
        that 4096 free emission parameters describe a surface EM cannot
        navigate.  Smoothing buys numerical safety, never findability; only
        cutting the parameter count does that.
        """
        n_bins = 1024
        eng, _, path = self._engine(n_bins)
        r = tttrlib.HmmRestraints(2, 2 * n_bins)
        r.set_alpha_obs(tttrlib.VectorDouble([1.05] * (2 * 2 * n_bins)))
        init = tttrlib.HMM.factory_model(2, 2 * n_bins, 1e-4, 0, n_bins)
        smoothed = eng.optimize(init, 500, 1e-9, 1e-12, True, False, r)

        self.assertEqual(int((smoothed.obs_np == 0.0).sum()), 0)
        self.assertLess(_accuracy(eng.viterbi_path(smoothed)[0], path), 0.7)


class TestTttrLoaderBinsMicroTimes(unittest.TestCase):
    """The path real data takes: TAC channels binned onto the micro-time axis."""

    N_TAC = 4096

    TAC_RES_S = 16e-12          # 16 ps per TAC channel

    def _tttr(self, micro, chan, header=True):
        d = tttrlib.TTTR()
        n = len(micro)
        d.append_events(np.arange(n, dtype=np.uint64) * 10,
                        np.asarray(micro, dtype=np.uint16),
                        np.asarray(chan, dtype=np.int8),
                        np.zeros(n, dtype=np.int8), False, 0)
        if header:
            hdr = d.get_header()
            hdr.set_number_of_micro_time_channels(self.N_TAC)
            hdr.set_micro_time_resolution(self.TAC_RES_S)
        return d

    def _channels(self):
        g = tttrlib.Channel('g'); g.add_component(0, 0, 65535)
        r = tttrlib.Channel('r'); r.add_component(1, 0, 65535)
        return [g, r]

    def test_bins_the_tac_axis_over_its_full_range(self):
        """`bin = mt * n_micro_bins / n_tac`, over the *file's* range.

        Not over whatever micro-time window the stream Channels select: two
        streams with different windows have to land on one shared axis, or a
        single decay could not be scored across both.
        """
        n_bins = 16
        micro = [0, 255, 256, 2048, self.N_TAC - 1, 1000]
        chan = [0, 0, 0, 1, 1, 0]
        d = self._tttr(micro, chan)

        eng = tttrlib.HMM()
        eng.set_bursts_from_tttr(d, np.array([[0, len(micro) - 1]], dtype=np.int64),
                                 self._channels(), 3, 1, n_bins)

        self.assertEqual(eng.get_n_micro_bins(), n_bins)
        self.assertEqual(eng.get_n_symbols(), 2 * n_bins)
        expect = [c * n_bins + (m * n_bins) // self.N_TAC
                  for m, c in zip(micro, chan)]
        np.testing.assert_array_equal(eng.get_streams(), expect)

        # The bin width comes back in ns, from the header, on the binned axis.
        self.assertAlmostEqual(eng.get_micro_time_bin_width_ns(),
                               self.TAC_RES_S * 1e9 * self.N_TAC / n_bins)

    def test_a_header_without_a_tac_range_falls_back_to_the_data(self):
        """A file reporting one effective channel must not collapse the axis.

        Taken literally, one channel bins every photon into the last bin --
        every micro-time identical, the whole axis silently worthless, and a fit
        that runs and converges to nonsense.  The observed maximum is the honest
        fallback, and it has to give the *same* binning as the header path here.
        """
        n_bins = 16
        micro = [0, 255, 256, 2048, self.N_TAC - 1, 1000]
        chan = [0, 0, 0, 1, 1, 0]
        bursts = np.array([[0, len(micro) - 1]], dtype=np.int64)

        bare = tttrlib.HMM()
        bare.set_bursts_from_tttr(self._tttr(micro, chan, header=False), bursts,
                                  self._channels(), 3, 1, n_bins)
        full = tttrlib.HMM()
        full.set_bursts_from_tttr(self._tttr(micro, chan), bursts,
                                  self._channels(), 3, 1, n_bins)

        np.testing.assert_array_equal(bare.get_streams(), full.get_streams())
        self.assertGreater(len(set(bare.get_streams())), 1)
        # ...but with no resolution to read, the width is reported as unknown
        # rather than guessed.
        self.assertEqual(bare.get_micro_time_bin_width_ns(), 0.0)

    def test_default_is_the_stream_only_alphabet(self):
        """Existing callers must be untouched -- same symbols, no micro-time."""
        micro = [0, 900, 2048, 4000]
        chan = [0, 1, 0, 1]
        d = self._tttr(micro, chan)
        bursts = np.array([[0, len(micro) - 1]], dtype=np.int64)

        eng = tttrlib.HMM()
        eng.set_bursts_from_tttr(d, bursts, self._channels(), 3, 1)
        self.assertEqual(eng.get_n_micro_bins(), 1)
        self.assertEqual(eng.get_n_symbols(), 2)
        self.assertEqual(eng.get_micro_time_bin_width_ns(), 0.0)
        np.testing.assert_array_equal(eng.get_streams(), chan)


class TestEmissionSpec(unittest.TestCase):

    def test_single_bin_is_the_categorical_table(self):
        """At one bin the decay integrates to 1 and only the split survives."""
        spec = tttrlib.HmmEmissionSpec.uniform(2, 2, 1, 0.008, 4.0)
        spec.set_stream_probability(0, 0, 0.8)
        spec.set_stream_probability(0, 1, 0.2)
        spec.set_stream_probability(1, 0, 0.3)
        spec.set_stream_probability(1, 1, 0.7)
        np.testing.assert_allclose(
            np.asarray(spec.build()).reshape(2, 2), [[0.8, 0.2], [0.3, 0.7]])

    def test_rows_are_stochastic_and_split_is_preserved(self):
        spec = tttrlib.HmmEmissionSpec.uniform(3, 2, N_BINS, DT_NS, 3.0)
        spec.set_stream_probability(0, 0, 0.9)
        spec.set_stream_probability(0, 1, 0.1)
        tab = spec.build_np()
        np.testing.assert_allclose(tab.sum(axis=(1, 2)), 1.0)
        np.testing.assert_allclose(tab[0].sum(axis=1), [0.9, 0.1], atol=1e-12)

    def test_multi_exponential_is_not_its_own_average(self):
        """A two-component donor is not the mono-exponential at its mean tau.

        The species-average lifetime is what the photon *ratio* follows, while
        the decay shape carries more -- so a model that collapses a spectrum to
        one number is wrong in a way this asserts is measurable, not cosmetic.
        """
        multi = tttrlib.HmmEmissionSpec.uniform(1, 1, N_BINS, DT_NS, 1.0)
        multi.set_spectrum(0, 0, tttrlib.HmmLifetimeSpectrum(
            [0.5, 0.5], [1.0, 5.0]))
        mono = tttrlib.HmmEmissionSpec.uniform(1, 1, N_BINS, DT_NS, 3.0)
        self.assertGreater(
            np.abs(multi.build_np()[0, 0] - mono.build_np()[0, 0]).max(), 1e-3)

    def test_bins_are_integrated_not_point_sampled(self):
        """A photon's bin probability is the *integral* over the bin.

        The distinction is invisible for one lifetime -- a constant factor
        cancels in the normalisation -- and is a bias for a spectrum, because
        the factor `tau*(1 - exp(-dt/tau))` differs per component and so
        reweights them.  Since the integral is analytic for exponentials, the
        table can be exact at *any* resolution instead of converging towards
        exactness as bins are added.

        Checked by re-binning a very fine table down: an exact coarse table must
        equal the fine one summed over the bins it merges.
        """
        amps, taus = [0.6, 0.4], [1.0, 5.0]
        span = 16.0
        fine_bins = 4096

        def table(n_bins):
            s = tttrlib.HmmEmissionSpec.uniform(1, 1, n_bins, span / n_bins, 1.0)
            s.set_spectrum(0, 0, tttrlib.HmmLifetimeSpectrum(amps, taus))
            return s.build_np()[0, 0]

        fine = table(fine_bins)
        for n_bins in (32, 128, 512):
            coarse = fine.reshape(n_bins, -1).sum(axis=1)
            np.testing.assert_allclose(table(n_bins), coarse, rtol=2e-6,
                                       err_msg=f"{n_bins} bins")

    def test_a_single_lifetime_is_unaffected_by_the_integration(self):
        """...which is why point sampling looked fine for so long.

        With one component the bin-integral factor is common to every bin and
        divides out, so a mono-exponential table is identical either way.  The
        bias only exists where two components are weighted against each other.
        """
        mono = tttrlib.HmmEmissionSpec.uniform(1, 1, 32, 0.5, 3.0)
        got = mono.build_np()[0, 0]
        want = np.exp(-np.arange(32) * 0.5 / 3.0)
        np.testing.assert_allclose(got, want / want.sum(), rtol=1e-12)

    def test_irf_delays_the_decay(self):
        """Convolution has to move the peak; a no-op IRF would pass every
        shape check while quietly making lifetimes come out short."""
        plain = tttrlib.HmmEmissionSpec.uniform(1, 1, N_BINS, DT_NS, 2.0)
        with_irf = tttrlib.HmmEmissionSpec.uniform(1, 1, N_BINS, DT_NS, 2.0)
        with_irf.set_irf(tttrlib.SimDecay.gaussian_irf(N_BINS, DT_NS, 2.0, 0.5))
        self.assertGreater(int(np.argmax(with_irf.build_np()[0, 0])),
                           int(np.argmax(plain.build_np()[0, 0])))

    def test_analytic_irf_agrees_with_convolving_a_sampled_one(self):
        """The closed form is the same physics as the convolution it replaces.

        A photon's micro-time is the sum of the memoryless excited-state time
        and everything the instrument adds, so its density is an exponential
        convolved with a Gaussian.  Sampling that Gaussian onto the bin grid and
        convolving discretely approximates it; the exponentially-modified
        Gaussian *is* it.  The two must therefore converge as the grid is
        refined -- which is the only way to check the closed form against
        something independent of itself.
        """
        tau, mu, fwhm, span = 2.0, 1.2, 0.09, 16.0

        def both(n_bins):
            dt = span / n_bins
            a = tttrlib.HmmEmissionSpec.uniform(1, 1, n_bins, dt, tau)
            a.irf_center, a.irf_fwhm = mu, fwhm
            p = tttrlib.HmmEmissionSpec.uniform(1, 1, n_bins, dt, tau)
            p.set_irf(tttrlib.SimDecay.gaussian_irf(n_bins, dt, mu, fwhm))
            return a.build_np()[0, 0], p.build_np()[0, 0]

        errs = [np.abs(np.subtract(*both(n))).max() for n in (256, 1024, 4096)]
        self.assertLess(errs[-1], 1e-5)
        self.assertLess(errs[-1], errs[0] / 100.0, f"should converge: {errs}")

    def test_analytic_irf_is_exact_at_any_resolution(self):
        """...and unlike the convolution, it does not need a fine grid.

        A CDF difference is the bin's probability exactly, so a coarse table
        equals a fine one summed over the bins it merges.  That is the point of
        the closed form: at 256 bins the discrete convolution is already ~1e-3
        wrong, and no binning makes this one wrong at all.
        """
        tau, mu, fwhm, span = 2.0, 1.2, 0.09, 16.0

        def table(n_bins):
            s = tttrlib.HmmEmissionSpec.uniform(1, 1, n_bins, span / n_bins, tau)
            s.irf_center, s.irf_fwhm = mu, fwhm
            return s.build_np()[0, 0]

        fine = table(8192)
        for n_bins in (32, 128, 512):
            np.testing.assert_allclose(table(n_bins), fine.reshape(n_bins, -1).sum(1),
                                       rtol=1e-9, err_msg=f"{n_bins} bins")

    def test_analytic_irf_shifts_the_decay_by_its_centre(self):
        """A no-op IRF would pass every shape check while biasing lifetimes."""
        tau, mu, span, n_bins = 2.0, 1.2, 16.0, 512
        plain = tttrlib.HmmEmissionSpec.uniform(1, 1, n_bins, span / n_bins, tau)
        shifted = tttrlib.HmmEmissionSpec.uniform(1, 1, n_bins, span / n_bins, tau)
        shifted.irf_center, shifted.irf_fwhm = mu, 0.09

        t = np.arange(n_bins) * span / n_bins
        moved = (shifted.build_np()[0, 0] * t).sum() - (plain.build_np()[0, 0] * t).sum()
        self.assertAlmostEqual(moved, mu, delta=0.02)

    def test_two_irf_routes_are_mutually_exclusive(self):
        """Both at once would convolve the Gaussian in twice, and silently."""
        s = tttrlib.HmmEmissionSpec.uniform(1, 1, 64, 0.25, 2.0)
        s.irf_center, s.irf_fwhm = 1.0, 0.1
        s.set_irf(tttrlib.SimDecay.gaussian_irf(64, 0.25, 1.0, 0.1))
        with self.assertRaises(Exception):
            s.build()

    def test_background_flattens_every_state_equally(self):
        """Background is the part of the emission that says nothing about state."""
        spec = tttrlib.HmmEmissionSpec.uniform(2, 2, N_BINS, DT_NS, 4.0)
        spec.set_spectrum(1, 0, tttrlib.HmmLifetimeSpectrum(1.0))
        clean = spec.build_np()
        spec.background_fraction = 1.0
        muddy = spec.build_np()
        np.testing.assert_allclose(muddy[0], muddy[1])         # all state info gone
        np.testing.assert_allclose(muddy.sum(axis=(1, 2)), 1.0)
        self.assertGreater(np.abs(clean[0] - clean[1]).max(), 1e-3)

    def test_rejects_a_mismatched_spec(self):
        spec = tttrlib.HmmEmissionSpec.uniform(2, 2, N_BINS, DT_NS, 4.0)
        spec.background_fraction = 1.5
        with self.assertRaises(Exception):
            spec.build()


class TestMeasuredPatterns(unittest.TestCase):
    """A decay usually arrives as a measured **pattern**, not as (a, tau) pairs.

    `SimDecay` treats a pattern as the first-class representation and the
    multi-exponential helpers as optional constructors, because an experiment
    yields a measured donor-only decay, a scatter pattern, an IRF -- not a list
    of amplitudes and lifetimes. `HmmEmissionSpec` accepts the same.

    A supplied pattern is the shape, not a starting guess: nothing about it is
    fitted or sampled, only the stream split. That is *more* robust than fitting
    a lifetime, since there are no decay parameters left to be unidentifiable.
    """

    NB, SPAN, NATIVE = 64, 16.0, 4096

    def _native(self, tau):
        t = np.arange(self.NATIVE) * (self.SPAN / self.NATIVE)
        return np.exp(-t / tau)

    def _spec(self):
        s = tttrlib.HmmEmissionSpec.uniform(2, 2, self.NB, self.SPAN / self.NB, 4.0)
        for state, tau in enumerate((TAU_DARK, TAU_FRET)):
            s.set_stream_probability(state, 0, 0.5)
            s.set_stream_probability(state, 1, 0.5)
            s.set_pattern(state, 0, tttrlib.VectorDouble(list(self._native(tau))))
            s.set_pattern(state, 1,
                          tttrlib.VectorDouble(list(self._native(TAU_ACCEPTOR))))
        return s

    def test_aggregation_from_instrument_resolution_is_exact(self):
        """4096 TAC channels binned to 64 equals the analytic table.

        Exact because a bin's probability *is* the sum of the source channels
        inside it -- so a caller can hand over an instrument-resolution decay
        and let `build()` coarsen, with nothing lost that binning would not
        have lost anyway.
        """
        from_pattern = self._spec().build_np()

        analytic = tttrlib.HmmEmissionSpec.uniform(2, 2, self.NB, self.SPAN / self.NB, 4.0)
        for state, tau in enumerate((TAU_DARK, TAU_FRET)):
            analytic.set_stream_probability(state, 0, 0.5)
            analytic.set_stream_probability(state, 1, 0.5)
            analytic.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            analytic.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))

        np.testing.assert_allclose(from_pattern, analytic.build_np(), atol=1e-12)

    def test_the_fit_leaves_the_shape_alone_but_fits_the_split(self):
        """The defining property: a measured shape is data, not a parameter."""
        dt = self.SPAN / self.NB
        analytic = tttrlib.HmmEmissionSpec.uniform(2, 2, self.NB, dt, 4.0)
        for state, tau in enumerate((TAU_DARK, TAU_FRET)):
            analytic.set_spectrum(state, 0, tttrlib.HmmLifetimeSpectrum(tau))
            analytic.set_spectrum(state, 1, tttrlib.HmmLifetimeSpectrum(TAU_ACCEPTOR))
        true = tttrlib.HmmModel([0.5, 0.5], [0.999, 0.001, 0.001, 0.999],
                                analytic.build())
        true.n_micro_bins = self.NB
        times, syms, path = _simulate(true, seed=11, n_bursts=30, burst_len=400)
        streams, bins = _split(syms, self.NB)
        eng = tttrlib.HMM()
        eng.set_bursts_micro(times, streams, bins, 2, self.NB, dt)

        spec = self._spec()
        before = spec.build_np().copy()
        init = tttrlib.HmmModel([0.5, 0.5], [0.99, 0.01, 0.01, 0.99], spec.build())
        init.n_micro_bins = self.NB
        fit = eng.optimize(init, 200, 1e-9, 1e-12, True, False, None, None, spec)
        after = spec.build_np()

        # Shape: untouched, to round-off.
        for state in range(2):
            b = before[state, 0] / before[state, 0].sum()
            a = after[state, 0] / after[state, 0].sum()
            np.testing.assert_allclose(a, b, atol=1e-12)
        # Split: fitted, so it moved off the 50:50 it started at.
        self.assertTrue(np.any(np.abs(after.sum(2) - 0.5) > 1e-4))
        # And it decodes at least as well as fitting the lifetimes did.
        self.assertGreater(_accuracy(eng.viterbi_path(fit)[0], path), 0.70)

    def test_a_wrong_length_pattern_is_rebinned_not_rejected(self):
        """Any length is accepted; the axis is the spec's, not the caller's."""
        s = tttrlib.HmmEmissionSpec.uniform(1, 1, 32, 0.5, 3.0)
        s.set_pattern(0, 0, tttrlib.VectorDouble([1.0] * 100))   # not a multiple
        tab = s.build_np()[0, 0]
        np.testing.assert_allclose(tab.sum(), 1.0)
        np.testing.assert_allclose(tab, np.full(32, 1.0 / 32), atol=1e-12)


class TestSimDecayScores(unittest.TestCase):
    """One object samples and scores, so the two cannot drift apart."""

    def test_pdf_matches_the_sampling_histogram(self):
        d = tttrlib.SimDecay.multi_exponential([1.0], [2.0], N_BINS, DT_NS)
        pdf = np.asarray(d.pdf())
        self.assertEqual(len(pdf), N_BINS)
        np.testing.assert_allclose(pdf.sum(), 1.0)
        # The density is the normalised pattern it was built from.
        want = np.exp(-np.arange(N_BINS) * DT_NS / 2.0)
        np.testing.assert_allclose(pdf, want / want.sum(), rtol=1e-12)
        self.assertEqual(d.pdf(-1), 0.0)
        self.assertEqual(d.pdf(N_BINS), 0.0)


if __name__ == "__main__":
    unittest.main()
