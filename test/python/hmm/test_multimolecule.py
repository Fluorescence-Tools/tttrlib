#!/usr/bin/env python3
"""Multi-molecule bursts: the assumption, and what breaks when it fails.

The HMM models **one molecule per burst**. Freely-diffusing burst data violate
that at any finite concentration, and the violation is not a mild bias — a burst
holding two molecules is a superposition of two independent chains, and a
single-chain model has exactly one way to explain interleaved photons from two
sources: rapid switching. So coincidence manufactures the signal these tools
exist to detect.

Every test here uses **static** species, so the truth contains no dynamics
whatsoever and any transition the engine reports is an artifact by construction.
That is what makes the measurement unambiguous: there is no "true" switching
rate to argue about.

These are characterisation tests. They assert what today's engine does, so the
numbers in the class documentation and the guide cannot drift away from it.
"""
import unittest

import numpy as np

import tttrlib
import pytest


P_LOW, P_HIGH = 0.25, 0.75      # two static species
N_BURSTS, N_PH = 60, 400


def _fit(times, syms, n_states=2):
    eng = tttrlib.HMM()
    eng.set_bursts(times, syms, 2)
    m = eng.fit(n_states, n_restarts=4, seed=0)
    order = np.argsort(m.obs_np[:, 1])
    return eng, m.trans_np[order][:, order], m.obs_np[order]


def _clean(rng):
    """One static molecule per burst -- a population mixture, no dynamics."""
    times, syms = [], []
    for b in range(N_BURSTS):
        p = P_LOW if b % 2 == 0 else P_HIGH
        t = np.cumsum(rng.integers(1, 40, size=N_PH)).astype(np.int64)
        times.append(t.tolist())
        syms.append((rng.random(N_PH) < p).astype(int).tolist())
    return times, syms


def _partly_coincident(rng, frac, overlap=0.5):
    """A second, different molecule enters part-way through `frac` of bursts."""
    times, syms = [], []
    for b in range(N_BURSTS):
        p = P_LOW if b % 2 == 0 else P_HIGH
        t = np.cumsum(rng.integers(1, 40, size=N_PH)).astype(np.int64)
        s = (rng.random(N_PH) < p).astype(int)
        if rng.random() < frac:
            k = int(N_PH * overlap)
            t2 = np.linspace(t[N_PH - k], t[-1], k).astype(np.int64)
            s2 = (rng.random(k) < (P_HIGH if b % 2 == 0 else P_LOW)).astype(int)
            t = np.concatenate([t, t2])
            s = np.concatenate([s, s2])
            o = np.argsort(t, kind="stable")
            t, s = t[o], s[o]
        times.append(t.tolist())
        syms.append(s.tolist())
    return times, syms


class TestTheControl(unittest.TestCase):
    """Without coincidence the engine is right -- otherwise nothing below means
    anything, because the damage could not be attributed to coincidence."""

    def test_static_species_yield_no_dynamics_and_two_states(self):
        eng, trans, obs = _fit(*_clean(np.random.default_rng(5)))
        np.testing.assert_allclose(obs[:, 1], [P_LOW, P_HIGH], atol=0.03)
        # Off-diagonals sit on the floor: the fit reports no switching at all.
        self.assertLess(max(trans[0, 1], trans[1, 0]), 1e-8)
        best = min(range(1, 5), key=lambda k: eng.fit(k, 3, seed=0).bic())
        self.assertEqual(best, 2)


class TestCoincidenceManufacturesDynamics(unittest.TestCase):

    @staticmethod
    def _over_seeds(frac, n_seeds=6):
        """Median switching rate and BIC state count over independent datasets.

        Aggregated deliberately. A single dataset lands in whichever EM optimum
        it lands in, and reading a trend off one realisation is how a noise
        excursion gets written down as a threshold -- so every claim below is a
        median, and the BIC counts are reported as a fraction of datasets.
        """
        rates, ks = [], []
        for sd in range(n_seeds):
            eng, trans, _ = _fit(*_partly_coincident(np.random.default_rng(100 + sd), frac))
            rates.append(max(trans[0, 1], trans[1, 0]))
            ks.append(min(range(1, 5), key=lambda k: eng.fit(k, 3, seed=0).bic()))
        return float(np.median(rates)), ks

    def test_five_percent_invents_switching_and_an_extra_state(self):
        """The headline, and the reason this matters in practice.

        A twentieth of bursts being coincident is ordinary at typical burst
        concentrations -- and it already turns "no dynamics" into measurable
        switching *and* makes the BIC scan report a third state, in every
        dataset tried rather than in an unlucky one.
        """
        rate, ks = self._over_seeds(0.05)
        self.assertGreater(rate, 1e-6, "expected spurious switching from static molecules")
        self.assertTrue(all(k > 2 for k in ks),
                        f"expected BIC to be fooled in every dataset, got {ks}")

    def test_clean_data_are_not_flagged(self):
        """The control for the above -- without coincidence, neither happens."""
        rate, ks = self._over_seeds(0.0)
        self.assertLess(rate, 1e-8)
        self.assertTrue(all(k == 2 for k in ks), ks)

    @pytest.mark.slow
    def test_apparent_switching_grows_with_contamination(self):
        """Monotone in the contamination, which is what identifies the cause."""
        rates = [self._over_seeds(f)[0] for f in (0.0, 0.10, 0.50)]
        self.assertLess(rates[0], 1e-8)
        self.assertGreater(rates[1], rates[0])
        self.assertGreater(rates[2], rates[1])


class TestFullOverlapDestroysTheStates(unittest.TestCase):
    """The other failure mode -- and the less intuitive one."""

    def test_the_two_species_are_no_longer_recovered(self):
        """Both molecules present throughout leaves no time structure at all.

        Every photon is then a draw from the *mixture*, so there is nothing for
        a hidden state to resolve. Worth separating from the partial-overlap
        case: here the states are destroyed rather than the kinetics invented,
        so a reader guarding only against spurious dynamics would miss it.

        Asserted as "the true species are not recovered" rather than as a
        specific collapsed value. Where exactly the fit lands varies with the
        dataset -- sometimes both states near the mixture, sometimes one of them
        degenerate -- and pinning one of those outcomes would be pinning an EM
        local optimum, not the phenomenon.
        """
        for sd in (3, 4, 5):
            rng = np.random.default_rng(sd)
            times, syms = [], []
            for _ in range(N_BURSTS):
                ta = np.cumsum(rng.integers(1, 80, size=N_PH // 2)).astype(np.int64)
                tb = np.cumsum(rng.integers(1, 80, size=N_PH // 2)).astype(np.int64)
                sa = (rng.random(N_PH // 2) < P_LOW).astype(int)
                sb = (rng.random(N_PH // 2) < P_HIGH).astype(int)
                t = np.concatenate([ta, tb])
                s = np.concatenate([sa, sb])
                o = np.argsort(t, kind="stable")
                times.append(t[o].tolist())
                syms.append(s[o].tolist())

            _, _, obs = _fit(times, syms)
            recovered = np.abs(obs[:, 1] - np.array([P_LOW, P_HIGH])).max()
            self.assertGreater(recovered, 0.1,
                               f"seed {sd}: species should NOT be recovered, got {obs[:, 1]}")


class TestBrightnessEnvelopeIsHarmless(unittest.TestCase):
    """The counterpart to coincidence -- and the answer goes the other way.

    A diffusing molecule's brightness depends on its position in the focal spot,
    so the photon rate rises and falls during every burst. That sounds like the
    same kind of problem as coincidence, and it is not: conditioning on photon
    *arrivals* removes the overall rate, because `P(symbol | state)` does not
    depend on it. The fit is invariant to the envelope.

    Worth pinning as a feature rather than leaving implicit, because the
    property reads like an omission -- "the engine ignores photon rate" -- and
    invites someone to add a state-dependent rate term. For diffusing data that
    would make the model *worse*: it would attribute the transit through the PSF
    to state changes, manufacturing dynamics the way coincidence does.
    """

    @staticmethod
    def _envelope_bursts(envelope, seed, n_ph=400):
        rng = np.random.default_rng(seed)
        times, syms = [], []
        for _ in range(N_BURSTS):
            u = np.linspace(-1.8, 1.8, n_ph)
            rate = envelope(u)
            gaps = np.maximum(1, rng.exponential(1.0, n_ph) * 30.0 / rate).astype(np.int64)
            times.append(np.cumsum(gaps).tolist())
            # The state never changes: one species, constant emission.
            syms.append((rng.random(n_ph) < 0.35).astype(int).tolist())
        return times, syms

    @staticmethod
    def _bic_states(eng):
        """State count, smoothed so the degenerate zero-emission optimum -- which
        inflates the likelihood and would swamp the effect under test -- cannot
        be selected instead."""
        best = (np.inf, 0)
        for k in (1, 2, 3):
            r = tttrlib.HmmRestraints(k, 2)
            r.set_alpha_obs(tttrlib.VectorDouble([1.05] * (k * 2)))
            m = eng.optimize(tttrlib.HMM.factory_model(k, 2, 1e-3, 0),
                             400, 1e-9, 1e-12, True, False, r)
            best = min(best, (m.bic(), k))
        return best[1]

    @pytest.mark.slow
    def test_a_huge_intensity_swing_adds_no_state(self):
        """425000x brighter at the centre than the edge, still one state."""
        narrow = lambda u: np.exp(-u ** 2 / 0.25)
        for sd in range(3):
            eng = tttrlib.HMM()
            eng.set_bursts(*self._envelope_bursts(narrow, 20 + sd), 2)
            self.assertEqual(self._bic_states(eng), 1, f"seed {sd}")

    def test_recovered_emission_is_unchanged_by_the_envelope(self):
        flat = lambda u: np.ones_like(u)
        gauss = lambda u: np.exp(-u ** 2)
        got = []
        for env in (flat, gauss):
            eng = tttrlib.HMM()
            eng.set_bursts(*self._envelope_bursts(env, 20), 2)
            m = eng.fit(1, 3, seed=0)
            got.append(m.obs_np[0, 1])
        self.assertAlmostEqual(got[0], 0.35, delta=0.03)
        self.assertAlmostEqual(got[1], got[0], delta=0.02)


class TestPerStateBrightnessIsNotShippable(unittest.TestCase):
    """Why there is no per-state brightness read-out, measured rather than asserted.

    Tempting feature: report how bright each state is, as
    ``(photons in state) / (time in state)``. The ratio between two states is
    the only identifiable part -- absolute brightness is not -- and the ratio
    was expected to be safe because state and position in the focus are
    independent, so the PSF envelope should cancel.

    **It does not cancel**, and that is the finding. Two independent errors bite,
    and a guard on switching rate -- which was the plan -- would have caught only
    one of them while leaving a number that looks trustworthy and is not.

    Note this does not contradict `TestBrightnessEnvelopeIsHarmless`: the
    envelope really is harmless to what the HMM infers, because
    `P(symbol | state)` is invariant to the photon rate. It is a brightness
    read-out bolted on afterwards that the envelope breaks.
    """

    TRUE_RATIO, B0 = 3.0, 0.02
    N_BURST, N_TICK = 40, 4000

    @classmethod
    def _simulate(cls, k, seed, width=1.0):
        """Tick-level 2-state chain, Poisson emission, optional PSF envelope."""
        rng = np.random.default_rng(seed)
        times, at_photon, tick_path = [], [], []
        u = np.linspace(-2.0, 2.0, cls.N_TICK)
        env = np.ones(cls.N_TICK) if width is None else np.exp(-u ** 2 / width)
        for _ in range(cls.N_BURST):
            state = rng.integers(0, 2)
            path = np.empty(cls.N_TICK, dtype=np.int8)
            flip = rng.random(cls.N_TICK) < k
            for i in range(cls.N_TICK):
                if flip[i]:
                    state = 1 - state
                path[i] = state
            rate = np.where(path == 1, cls.B0 * cls.TRUE_RATIO, cls.B0) * env
            t = np.flatnonzero(rng.random(cls.N_TICK) < rate)
            if len(t) < 20:
                continue
            times.append(t)
            at_photon.append(path[t])
            tick_path.append(path)
        return times, at_photon, tick_path

    @staticmethod
    def _ratio_from_ticks(times, tick_path):
        """Oracle: time taken from the TRUE tick-level path."""
        n, T = np.zeros(2), np.zeros(2)
        for t, p in zip(times, tick_path):
            for s in (0, 1):
                T[s] += (p == s).sum()
                n[s] += (p[t] == s).sum()
        return (n[1] / T[1]) / (n[0] / T[0])

    @staticmethod
    def _ratio_from_photons(times, at_photon):
        """Practical: state known only at photons; each gap charged to its start."""
        n, T = np.zeros(2), np.zeros(2)
        for t, s in zip(times, at_photon):
            gaps = np.diff(t)
            for k in (0, 1):
                n[k] += (s[1:] == k).sum()
                T[k] += gaps[s[:-1] == k].sum()
        return (n[1] / T[1]) / (n[0] / T[0])

    def test_the_true_tick_path_recovers_it_at_any_switching_rate(self):
        """There is no information ceiling -- so the loss is attribution, not physics.

        This corrects an earlier reading of these experiments, which recorded
        that even the true path degraded with switching rate. It does not; the
        earlier number came from the *photon-level* path.
        """
        for k in (1e-4, 1e-2):
            vals = [self._ratio_from_ticks(t, tp)
                    for t, _, tp in (self._simulate(k, sd) for sd in range(1, 7))]
            self.assertAlmostEqual(float(np.mean(vals)), self.TRUE_RATIO, delta=0.25,
                                   msg=f"switching {k}: oracle should be unbiased")

    def test_gap_attribution_collapses_when_switching_is_fast(self):
        """First error: a gap is charged to one state, but the chain switched inside it."""
        slow = [self._ratio_from_photons(t, s)
                for t, s, _ in (self._simulate(1e-4, sd, None) for sd in range(1, 7))]
        fast = [self._ratio_from_photons(t, s)
                for t, s, _ in (self._simulate(1e-2, sd, None) for sd in range(1, 7))]
        self.assertAlmostEqual(float(np.mean(slow)), self.TRUE_RATIO, delta=0.2)
        self.assertLess(float(np.mean(fast)), 2.4, "fast switching should collapse it")

    def test_the_psf_envelope_does_not_cancel(self):
        """Second error, and the one that rules the feature out.

        The envelope was expected to cancel in a ratio. Measured at *slow*
        switching, where the attribution error above is absent, the bias grows
        monotonically with envelope depth -- so a guard on switching rate would
        not have protected anyone.
        """
        got = [float(np.mean([self._ratio_from_photons(t, s) for t, s, _ in
                              (self._simulate(1e-4, sd, w) for sd in range(1, 9))]))
               for w in (None, 1.0, 0.25)]
        flat, medium, narrow = got
        self.assertAlmostEqual(flat, self.TRUE_RATIO, delta=0.2)
        self.assertLess(medium, flat - 0.15, f"envelope should bias it: {got}")
        self.assertLess(narrow, medium, f"deeper envelope, worse bias: {got}")
        self.assertLess(narrow, 2.5, f"a realistic confocal envelope is deep: {got}")


# ---------------------------------------------------------------------------
# Ground truth from the engine, not from construction.
#
# Everything above builds coincidence by hand, which is fine for measuring the
# damage but useless for judging a *detector*: a detector tested on bursts I
# concatenated myself only learns how I concatenated them. Below, `SimEngine`
# diffuses molecules through a focus and `emitting_molecule()` says which one
# emitted each photon, so overlap is decided by the physics and the burst search
# never sees the labels.
# ---------------------------------------------------------------------------

_SIM_CACHE = {}


def _vd(x):
    return tttrlib.VectorDouble([float(v) for v in x])


def _diffusing_bursts(population, seed=1):
    """Simulate, burst-search, and label each burst by the engine's own truth.

    Returns ``(is_coincident, duration, n_photons, peak_rate)`` per burst. A
    burst counts as coincident when a second molecule contributed at least 10%
    of its photons -- a molecule donating one stray photon is not the confound
    this is about.
    """
    if (population, seed) in _SIM_CACHE:
        return _SIM_CACHE[(population, seed)]

    system = tttrlib.SimSystem()
    for E in (P_LOW, P_HIGH):
        sp = tttrlib.SimSpecies()
        sp.D = 0.5
        sp.q = _vd([4000 * (1 - E), 4000 * E])
        t_ns = np.arange(256) * 0.0625
        sp.decay = tttrlib.SimDecay.from_pattern(
            _vd(np.exp(-t_ns / 4.0)), 0.0625, 0.0)
        system.add_species(sp)
    zero = np.zeros(4)
    system.set_rate_matrices(_vd(zero), _vd(zero))    # STATIC -- no dynamics
    system.set_background(_vd([0.0, 0.0]))
    system.set_box(2.0, 4.0)
    for i in range(2):
        system.set_population(i, population)

    integrator = tttrlib.SimIntegrator()
    integrator.dt = 1e-3
    integrator.n_channels = 2
    integrator.n_ph_max = 4_000_000
    integrator.max_windows = 2_000_000          # cap the CLOCK, not the photons
    # SimEngine is deterministic: without varying these, "replicates" would be
    # identical copies and any spread computed from them would be fiction.
    integrator.seed_diffusion = 12345 + 7919 * seed
    integrator.seed_emission = 54321 + 6997 * seed
    engine = tttrlib.SimEngine(
        system, tttrlib.SimGrid.gaussian3d(0.6, 2.0, 2.0, 4.0, 0.05, 1.0),
        tttrlib.VectorSimGrid([]), integrator)
    engine.run()

    macro = np.asarray(engine.macro_window(), dtype=np.uint64)
    molecule = np.asarray(engine.emitting_molecule())

    data = tttrlib.TTTR()
    data.append_events(macro, np.asarray(engine.micro_time(), dtype=np.uint16),
                       np.asarray(engine.channel(), dtype=np.int8),
                       np.zeros(len(macro), dtype=np.int8), False, 0)
    header = data.get_header()
    header.set_macro_time_resolution(1e-3)
    header.set_number_of_micro_time_channels(2048)
    header.set_micro_time_resolution(8e-12)

    bf = tttrlib.BurstFilter(data)
    bf.set_burst_parameters(min_photons=40, window_photons=10, window_time_max=5e-3)
    bf.find_bursts()
    bursts = np.asarray(bf.get_bursts()).reshape(-1, 2)

    lab, dur, npho, peak = [], [], [], []
    for start, stop in bursts:
        n = stop - start + 1
        _, share = np.unique(molecule[start:stop + 1], return_counts=True)
        lab.append(int((share / n >= 0.10).sum() > 1))
        t = macro[start:stop + 1].astype(float)
        dur.append(t[-1] - t[0])
        npho.append(n)
        k = 10
        peak.append((k / np.maximum(t[k:] - t[:-k], 1.0)).max() if n > k
                    else n / max(t[-1] - t[0], 1.0))
    out = (np.array(lab), np.array(dur, float),
           np.array(npho, float), np.array(peak, float))
    _SIM_CACHE[(population, seed)] = out
    return out


SEEDS = (1, 2, 3, 4)


def _auc(score, label):
    """Rank-based AUC: P(a coincident burst scores above a clean one)."""
    r = score.argsort().argsort().astype(float)
    n1 = int(label.sum())
    if n1 == 0 or n1 == len(label):
        return float("nan")
    return (r[label == 1].mean() - (n1 - 1) / 2) / (label == 0).sum()


def _pooled_auc(population, key):
    """AUC over bursts pooled across independent seeds.

    Single-run AUCs at 5% prevalence ranged 0.36-0.63 when this was measured, so
    a one-seed assertion here would be testing the seed.
    """
    runs = [_diffusing_bursts(population, s) for s in SEEDS]
    label = np.concatenate([r[0] for r in runs])
    score = np.concatenate([r[key] for r in runs])
    return _auc(score, label)


class TestPerBurstDetectionDoesNotWork(unittest.TestCase):
    """The negative result, and the reason no detector ships.

    The obvious plan is to flag coincident bursts and drop them. It does not
    work: on data where the engine knows the answer, every statistic anyone
    would reach for is close to chance, and gets *worse* at the concentration
    where coincidence actually matters. Pinned here so the guide cannot quietly
    start recommending a filter that does not discriminate.
    """

    def test_every_obvious_statistic_is_near_chance(self):
        for name, key in (("duration", 1), ("n_photons", 2), ("peak_rate", 3)):
            auc = _pooled_auc(0.25, key)
            self.assertLess(auc, 0.70,
                            f"{name} AUC {auc:.3f} -- if this ever got good, "
                            f"a real detector became possible and the docs are wrong")

    def test_replicates_are_not_identical(self):
        """Guards the guard.

        `SimEngine` is deterministic, so if the seeds ever stopped being wired
        through, every test above would silently become a single-run test --
        which is the exact error that put AUC 0.87 into the documentation.
        """
        a = _diffusing_bursts(0.25, 1)
        b = _diffusing_bursts(0.25, 2)
        self.assertFalse(len(a[1]) == len(b[1]) and np.allclose(a[1], b[1]),
                         "seeds are not reaching SimEngine -- replicates are copies")

    def test_discrimination_degrades_where_it_is_needed_most(self):
        """The cruel part: the filter fades exactly as contamination rises.

        At higher occupancy the 'clean' bursts are contaminated too, so the
        contrast the statistic relies on washes out.
        """
        lo = np.mean([_diffusing_bursts(0.25, s)[0].mean() for s in SEEDS])
        hi = np.mean([_diffusing_bursts(1.0, s)[0].mean() for s in SEEDS])
        self.assertGreater(hi, lo)
        # Crowding does not make the statistic better, which is the point: the
        # filter is no help in the regime that needs it.
        self.assertLess(_pooled_auc(1.0, 1), 0.70)


class TestConcentrationIsTheRealLever(unittest.TestCase):
    """What to do instead: lower the concentration and measure longer.

    Selection trades away clean bursts almost as fast as coincident ones, so it
    buys little. Occupancy is the knob that actually moves the number.
    """

    def test_coincidence_grows_with_concentration(self):
        rates = [np.mean([_diffusing_bursts(p, s)[0].mean() for s in SEEDS])
                 for p in (0.25, 0.5, 1.0)]
        self.assertLess(rates[0], rates[1])
        self.assertLess(rates[1], rates[2])
        # Roughly proportional -- halving occupancy roughly halves coincidence,
        # which is what makes it a usable design rule.
        self.assertGreater(rates[2] / max(rates[0], 1e-9), 2.0)

    def test_selection_is_a_bad_trade(self):
        """Discarding half the bursts barely halves the contamination.

        This is the number that decides the recommendation, so it is asserted
        rather than described: the clean bursts lost per coincident burst
        removed is large enough that the filter costs more than it returns.
        """
        ratios, before, after = [], [], []
        for s in SEEDS:
            lab, dur = _diffusing_bursts(0.25, s)[0], _diffusing_bursts(0.25, s)[1]
            keep = dur <= np.quantile(dur, 0.5)      # drop the longest half
            removed = int(lab.sum() - lab[keep].sum())
            if removed > 0:
                ratios.append(int((~keep & (lab == 0)).sum()) / removed)
            before.append(lab.mean())
            after.append(lab[keep].mean())
        self.assertGreater(np.mean(ratios), 5.0,
                           "if this ever drops, selection became worthwhile")
        # Halving the dataset barely moves the contamination.
        self.assertGreater(np.mean(after), 0.6 * np.mean(before))


class TestSelectionIsTheMitigation(unittest.TestCase):

    def test_coincident_bursts_are_longer_and_brighter(self):
        """Why duration and count-rate filtering works upstream.

        Not a property of the engine but of the data, and it is the basis of the
        recommended mitigation, so it is worth pinning next to the damage it
        mitigates rather than left as folklore.
        """
        rng = np.random.default_rng(5)
        clean_t, clean_s = _clean(rng)
        both_t, both_s = _partly_coincident(np.random.default_rng(5), 1.0)

        def stats(times, syms):
            dur = np.array([t[-1] - t[0] for t in times], float)
            n = np.array([len(s) for s in syms], float)
            return dur.mean(), (n / np.maximum(dur, 1)).mean()

        d_clean, r_clean = stats(clean_t, clean_s)
        d_both, r_both = stats(both_t, both_s)
        self.assertGreaterEqual(d_both, d_clean * 0.99)
        self.assertGreater(r_both, r_clean * 1.1)


if __name__ == "__main__":
    unittest.main()
