"""A/B of the three burst-dynamics features -- BVA, 2CDE and recurrence
analysis -- against independent references.

* **BVA** (Torella et al. 2011, Biophys. J. 100, 1568): per burst, the mean and
  standard deviation of the proximity ratio ``n_A / (n_A + n_D)`` over
  consecutive slices. Reference: NumPy written from that definition on the
  photon streams; conventions pinned and documented: slices are cut over *all*
  photons of the burst (a photon in neither stream still occupies a slot),
  the incomplete last slice is kept, the standard deviation is the population
  one, and a time-window slice includes photons up to ``start + window``
  inclusive. FRETBursts' BVA notebook drops the incomplete last chunk -- the
  reference is run both ways and the difference is shown to be the remainder
  only. The static line is ``sqrt(p(1-p)/n)`` (binomial), analytic.

* **2CDE** (Tomov et al. 2012, Biophys. J. 102, 1163): per burst, KDE-based
  FRET-2CDE ``110 - 100 [(E)_D + (1-E)_A]`` and ALEX-2CDE. Reference: the KDE
  core is **FRETBursts' own ``kde_laplace`` / ``kde_gaussian``** run live in
  ``benchmarks/.venvs/fretbursts`` on the same timestamps (the C++ is a port
  of it), assembled per burst with the paper's formulas; plus a NumPy
  transcription of the same kernels for when the venv is absent. Existing
  ``test/python/twocde/test_twocde.py`` already pins the transcription; this
  file adds the live one.

* **Recurrence analysis** (Hoffmann et al. 2011, PCCP 13, 1857): pair
  statistics of burst arrival times, ``P_same = 1 - 1/G``. References: NumPy
  pair counting from the definition, the Poisson expectation with the finite
  acquisition-time correction, a known answer -- a Poisson background of bursts
  plus molecules that recur at a lag inside a chosen window, where
  ``P_same`` in that window equals ``N_rec / (N_rec + expected random
  pairs)`` -- and ``recurrence_efficiencies`` against a NumPy pairing.
"""
import json
import os
import subprocess
import unittest

import numpy as np
import pytest

import tttrlib

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_FRETBURSTS_PY = os.path.join(_ROOT, "benchmarks", ".venvs", "fretbursts", "bin", "python")


# --------------------------------------------------------------------------
# synthetic TTTR
# --------------------------------------------------------------------------

def build_tttr(chan_per_burst, gap=1_000_000, macro_step=1):
    """In-memory TTTR; macro time increments by ``macro_step`` per photon.
    Returns (tttr, macro, chan, bounds[inclusive])."""
    macro, chan, bounds = [], [], []
    t = 0
    for ch in chan_per_burst:
        start = len(macro)
        for c in ch:
            t += macro_step
            macro.append(t)
            chan.append(int(c))
        t += gap
        bounds.append((start, len(macro) - 1))
    d = tttrlib.TTTR()
    d.append_events(
        np.asarray(macro, dtype=np.uint64),
        np.zeros(len(macro), dtype=np.uint16),
        np.asarray(chan, dtype=np.int8),
        np.zeros(len(macro), dtype=np.int8),
        False, 0,
    )
    d.header.set_macro_time_resolution(1.0)   # tau in "seconds" == ticks
    return d, np.asarray(macro, dtype=np.float64), np.asarray(chan), np.asarray(bounds, dtype=np.int64)


# --------------------------------------------------------------------------
# BVA
# --------------------------------------------------------------------------

def bva_reference(chan, bounds, donor, acceptor, n_per_slice, keep_remainder=True):
    """Mean / population-std of the slice proximity ratio, per burst."""
    is_d = np.isin(chan, donor)
    is_a = np.isin(chan, acceptor)
    means, stds = [], []
    for s, e in bounds:
        d = is_d[s:e + 1]
        a = is_a[s:e + 1]
        n = e - s + 1
        prs = []
        for k in range(0, n, n_per_slice):
            if not keep_remainder and k + n_per_slice > n:
                break
            dc = d[k:k + n_per_slice].sum()
            ac = a[k:k + n_per_slice].sum()
            prs.append(ac / (ac + dc) if (ac + dc) > 0 else 0.0)
        prs = np.asarray(prs, float)
        means.append(prs.mean())
        stds.append(prs.std())
    return np.asarray(means), np.asarray(stds)


def bva_reference_time_windows(macro, chan, bounds, donor, acceptor, window):
    is_d = np.isin(chan, donor)
    is_a = np.isin(chan, acceptor)
    means, stds = [], []
    for s, e in bounds:
        prs = []
        i = s
        while i <= e:
            thr = macro[i] + window
            j = i
            while j + 1 <= e and macro[j + 1] <= thr:
                j += 1
            dc = is_d[i:j + 1].sum()
            ac = is_a[i:j + 1].sum()
            prs.append(ac / (ac + dc) if (ac + dc) > 0 else 0.0)
            i = j + 1
        prs = np.asarray(prs, float)
        means.append(prs.mean())
        stds.append(prs.std())
    return np.asarray(means), np.asarray(stds)


class TestBvaAgainstTheDefinition(unittest.TestCase):

    def _bursts(self, seed, n_bursts=40, n_ph=300, extra_channel=False):
        rng = np.random.default_rng(seed)
        out = []
        for p in rng.uniform(0.1, 0.9, size=n_bursts):
            ch = (rng.random(n_ph) < p).astype(int)
            if extra_channel:
                ch[rng.random(n_ph) < 0.1] = 2      # a photon in neither stream
            out.append(ch)
        return out

    def test_photon_slices_match_numpy(self):
        for seed, n_slice in [(1, 5), (2, 7), (3, 4)]:
            with self.subTest(seed=seed, n_per_slice=n_slice):
                bursts = self._bursts(seed)
                d, macro, chan, bounds = build_tttr(bursts)
                bva = tttrlib.BVA(d)
                bva.set_donor([0])
                bva.set_acceptor([1])
                bva.compute(bounds, n_slice, 0.01)
                m_ref, s_ref = bva_reference(chan, bounds, [0], [1], n_slice)
                np.testing.assert_allclose(bva.proximity_ratio_mean, m_ref, rtol=0, atol=1e-12)
                np.testing.assert_allclose(bva.proximity_ratio_std, s_ref, rtol=0, atol=1e-12)

    def test_slices_are_cut_over_all_photons_of_the_burst(self):
        """Convention: a photon in neither stream still occupies a slot in the
        slicing (documented; FRETBursts' notebook slices the selected stream
        only, which is the same thing whenever every burst photon is D or A)."""
        bursts = self._bursts(4, extra_channel=True)
        d, macro, chan, bounds = build_tttr(bursts)
        bva = tttrlib.BVA(d)
        bva.set_donor([0])
        bva.set_acceptor([1])
        bva.compute(bounds, 5, 0.01)
        m_ref, s_ref = bva_reference(chan, bounds, [0], [1], 5)
        np.testing.assert_allclose(bva.proximity_ratio_mean, m_ref, rtol=0, atol=1e-12)
        np.testing.assert_allclose(bva.proximity_ratio_std, s_ref, rtol=0, atol=1e-12)

    def test_remainder_slice_is_kept(self):
        """Pins the incomplete-last-slice convention against the alternative
        (FRETBursts notebook: drop it). With 303 photons and n = 5 the two
        differ; with 300 they coincide."""
        bursts = self._bursts(5, n_ph=303)
        d, macro, chan, bounds = build_tttr(bursts)
        bva = tttrlib.BVA(d)
        bva.set_donor([0])
        bva.set_acceptor([1])
        bva.compute(bounds, 5, 0.01)
        m_keep, s_keep = bva_reference(chan, bounds, [0], [1], 5, keep_remainder=True)
        m_drop, s_drop = bva_reference(chan, bounds, [0], [1], 5, keep_remainder=False)
        np.testing.assert_allclose(bva.proximity_ratio_std, s_keep, rtol=0, atol=1e-12)
        self.assertGreater(np.max(np.abs(s_keep - s_drop)), 1e-6)   # the conventions do differ

    def test_time_windows_match_numpy(self):
        bursts = self._bursts(6)
        d, macro, chan, bounds = build_tttr(bursts, macro_step=3)
        bva = tttrlib.BVA(d)
        bva.set_donor([0])
        bva.set_acceptor([1])
        bva.compute(bounds, -1, 20.0)     # window of 20 ticks (resolution 1.0)
        m_ref, s_ref = bva_reference_time_windows(macro, chan, bounds, [0], [1], 20.0)
        np.testing.assert_allclose(bva.proximity_ratio_mean, m_ref, rtol=0, atol=1e-12)
        np.testing.assert_allclose(bva.proximity_ratio_std, s_ref, rtol=0, atol=1e-12)

    def test_static_line_is_the_binomial_standard_deviation(self):
        p = np.linspace(0.05, 0.95, 19)
        for n in (3, 5, 10):
            mean, std = tttrlib.BVA.compute_static_bva_line(p, n)
            np.testing.assert_allclose(np.asarray(mean), p, atol=1e-15)
            np.testing.assert_allclose(np.asarray(std), np.sqrt(p * (1 - p) / n), atol=1e-15)

    def test_static_species_sits_on_the_line(self):
        """Known answer: binomial slices of a static species average to the
        shot-noise line (E[std] < sqrt(p(1-p)/n) slightly, since the sample
        std of a few binomial draws is biased low)."""
        rng = np.random.default_rng(7)
        p, n = 0.4, 6
        bursts = [(rng.random(600) < p).astype(int) for _ in range(300)]
        d, macro, chan, bounds = build_tttr(bursts)
        bva = tttrlib.BVA(d)
        bva.set_donor([0])
        bva.set_acceptor([1])
        bva.compute(bounds, n, 0.01)
        line = np.sqrt(p * (1 - p) / n)
        self.assertAlmostEqual(np.mean(bva.proximity_ratio_std), line, delta=0.02)
        self.assertAlmostEqual(np.mean(bva.proximity_ratio_mean), p, delta=0.01)


# --------------------------------------------------------------------------
# 2CDE
# --------------------------------------------------------------------------

def kde_numpy(ts, tau, axis, kernel):
    """The FRETBursts kernels, transcribed (used when the venv is absent)."""
    ts = np.asarray(ts, float)
    r = np.zeros(len(axis))
    if kernel == "gaussian":
        lim, tau2 = 3.0 * tau, 2.0 * tau * tau
        for i, t in enumerate(axis):
            sel = ts[(ts - t < lim) & (t - ts <= lim)]
            r[i] = np.exp(-((sel - t) ** 2) / tau2).sum()
    else:
        lim = 5.0 * tau
        for i, t in enumerate(axis):
            sel = ts[(ts - t < lim) & (t - ts <= lim)]
            r[i] = np.exp(-np.abs(sel - t) / tau).sum()
    return r


def kde_fretbursts(ts, tau, axis, kernel):
    """FRETBursts' phrates.kde_laplace / kde_gaussian, live in its venv."""
    script = (
        "import sys, json, numpy as np\n"
        "import warnings; warnings.filterwarnings('ignore')\n"
        "from fretbursts.phtools import phrates\n"
        "d = json.load(sys.stdin)\n"
        "ts = np.array(d['ts'], dtype=np.int64); ax = np.array(d['axis'], dtype=np.int64)  # cython kernels take int64 ticks\n"
        "f = phrates.kde_gaussian if d['kernel'] == 'gaussian' else phrates.kde_laplace\n"
        "print('KDE=' + json.dumps([float(v) for v in f(ts, d['tau'], ax)]))\n"
    )
    payload = json.dumps({"ts": [int(v) for v in ts], "axis": [int(v) for v in axis], "tau": float(tau), "kernel": kernel})
    out = subprocess.run([_FRETBURSTS_PY, "-c", script], input=payload, capture_output=True, text=True, check=True)
    line = [l for l in out.stdout.splitlines() if l.startswith("KDE=")][-1]
    return np.array(json.loads(line[4:]))


def fret_2cde_from_kdes(kde_d, kde_a, mask_d, mask_a, bounds, kernel):
    out = []
    for s, e in bounds:
        sl = slice(int(s), int(e) + 1)
        if not mask_d[sl].any() or not mask_a[sl].any():
            out.append(np.nan)
            continue
        kde_adi = kde_a[sl][mask_d[sl]]
        kde_ddi = kde_d[sl][mask_d[sl]]
        kde_dai = kde_d[sl][mask_a[sl]]
        kde_aai = kde_a[sl][mask_a[sl]]
        n_d, n_a = mask_d[sl].sum(), mask_a[sl].sum()
        if kernel == "laplace":                # Tomov nbKDE self-correction
            kde_ddi = (1 + 2 / n_d) * (kde_ddi - 1)
            kde_aai = (1 + 2 / n_a) * (kde_aai - 1)
        ed = np.mean(kde_adi / (kde_adi + kde_ddi))
        ea = np.mean(kde_dai / (kde_dai + kde_aai))
        out.append(110 - 100 * (ed + ea))
    return np.array(out)


def alex_2cde_from_kdes(kde_dex, kde_aex, mask_dex, mask_aex, bounds):
    out = []
    for s, e in bounds:
        sl = slice(int(s), int(e) + 1)
        if not mask_dex[sl].any() or not mask_aex[sl].any():
            out.append(np.nan)
            continue
        kde_dexdex = kde_dex[sl][mask_dex[sl]]
        kde_aexdex = kde_aex[sl][mask_dex[sl]]
        kde_aexaex = kde_aex[sl][mask_aex[sl]]
        kde_dexaex = kde_dex[sl][mask_aex[sl]]
        n_aex, n_dex = mask_aex[sl].sum(), mask_dex[sl].sum()
        br_dex = np.sum(kde_aexdex / kde_dexdex) / n_aex
        br_aex = np.sum(kde_dexaex / kde_aexaex) / n_dex
        out.append(100 - 50 * (br_dex - br_aex))
    return np.array(out)


class TestTwoCdeAgainstFretbursts(unittest.TestCase):
    """The KDE core is FRETBursts' (live when the venv exists, transcribed
    otherwise); the burst formulas are Tomov's. Bursts are few and short so
    the live subprocess stays quick. FRETBursts' cython kernels take integer
    timestamps, which the synthetic TTTR has anyway."""

    def _kde(self, ts, tau, axis, kernel):
        if os.path.exists(_FRETBURSTS_PY):
            live = kde_fretbursts(ts, tau, axis, kernel)
            np.testing.assert_allclose(live, kde_numpy(ts, tau, axis, kernel), rtol=1e-12, atol=1e-12)
            return live
        return kde_numpy(ts, tau, axis, kernel)

    @pytest.mark.heavy
    def test_fret_2cde_both_kernels(self):
        rng = np.random.default_rng(21)
        bursts = [(rng.random(120) < p).astype(int) for p in rng.uniform(0.1, 0.9, size=12)]
        # macro steps of 4 ticks so tau = 30 spans a handful of photons
        d, macro, chan, bounds = build_tttr(bursts, gap=5000, macro_step=4)
        mask_d, mask_a = chan == 0, chan == 1
        for kernel, kflag in [("laplace", tttrlib.TwoCDE.LAPLACE), ("gaussian", tttrlib.TwoCDE.GAUSSIAN)]:
            with self.subTest(kernel=kernel):
                eng = tttrlib.TwoCDE(d)
                eng.set_donor([0])
                eng.set_acceptor([1])
                eng.compute(bounds, 30.0, tttrlib.TwoCDE.FRET_2CDE, kflag)
                kde_d = self._kde(macro[mask_d], 30.0, macro, kernel)
                kde_a = self._kde(macro[mask_a], 30.0, macro, kernel)
                ref = fret_2cde_from_kdes(kde_d, kde_a, mask_d, mask_a, bounds, kernel)
                np.testing.assert_allclose(np.asarray(eng.two_cde), ref, rtol=1e-9, atol=1e-7)

    @pytest.mark.slow
    def test_alex_2cde(self):
        rng = np.random.default_rng(22)
        bursts = []
        for _ in range(12):
            r = rng.random(120)
            bursts.append(np.where(r < 0.45, 0, np.where(r < 0.75, 1, 2)))
        d, macro, chan, bounds = build_tttr(bursts, gap=5000, macro_step=4)
        mask_dex = np.isin(chan, [0, 1])
        mask_aex = chan == 2
        eng = tttrlib.TwoCDE(d)
        eng.set_donor_excitation([0, 1])
        eng.set_acceptor_excitation([2])
        eng.compute(bounds, 30.0, tttrlib.TwoCDE.ALEX_2CDE, tttrlib.TwoCDE.LAPLACE)
        kde_dex = self._kde(macro[mask_dex], 30.0, macro, "laplace")
        kde_aex = self._kde(macro[mask_aex], 30.0, macro, "laplace")
        ref = alex_2cde_from_kdes(kde_dex, kde_aex, mask_dex, mask_aex, bounds)
        np.testing.assert_allclose(np.asarray(eng.two_cde), ref, rtol=1e-9, atol=1e-7)


# --------------------------------------------------------------------------
# recurrence analysis
# --------------------------------------------------------------------------

def pair_statistics_reference(times, edges, edge_correction=True):
    t = np.sort(np.asarray(times, float))
    n = len(t)
    T = t[-1] - t[0]
    rate = n / T
    counts, expected = [], []
    for a, b in zip(edges[:-1], edges[1:]):
        lo = np.searchsorted(t, t + a, side="left")
        hi = np.searchsorted(t, t + b, side="left")
        c = hi - lo
        self_in = (lo <= np.arange(n)) & (np.arange(n) < hi)
        counts.append(float((c - self_in).sum()))
        span = T - 0.5 * (a + b) if edge_correction else T
        expected.append(rate * rate * (b - a) * max(span, 0.0))
    return np.asarray(counts), np.asarray(expected)


class TestRecurrenceAgainstTheDefinition(unittest.TestCase):

    def test_pair_statistics_match_numpy(self):
        rng = np.random.default_rng(31)
        times = np.sort(rng.uniform(0, 100.0, 2000))
        edges = np.logspace(-3, 0, 21)
        for corr in (True, False):
            with self.subTest(edge_correction=corr):
                got = np.array(tttrlib.pair_statistics(times, edges, corr))
                c_ref, e_ref = pair_statistics_reference(times, edges, corr)
                np.testing.assert_allclose(got[:20], c_ref, rtol=0, atol=1e-9)
                np.testing.assert_allclose(got[20:], e_ref, rtol=1e-12)

    def test_same_molecule_probability_is_the_flat_layout_of_pair_statistics(self):
        rng = np.random.default_rng(32)
        times = np.sort(rng.uniform(0, 50.0, 1500))
        n_bins = 25
        got = np.array(tttrlib.same_molecule_probability(times, 1e-3, 1.0, n_bins, True))
        edges = np.logspace(-3, 0, n_bins + 1)
        c, e = pair_statistics_reference(times, edges, True)
        g = c / e
        np.testing.assert_allclose(got[:n_bins], np.sqrt(edges[:-1] * edges[1:]), rtol=1e-12)
        np.testing.assert_allclose(got[2 * n_bins:], g, rtol=1e-12)
        np.testing.assert_allclose(got[n_bins:2 * n_bins], np.clip(1 - 1 / g, 0, 1), rtol=1e-12)

    def test_poisson_bursts_have_no_recurrence(self):
        """Known answer: for a Poisson process G(tau) = 1, P_same = 0 up to
        shot noise (bins with ~1e4 pairs -> ~1% on G)."""
        rng = np.random.default_rng(33)
        times = np.sort(rng.uniform(0, 1000.0, 20000))    # 20 bursts/s
        n_bins = 12
        r = np.array(tttrlib.same_molecule_probability(times, 1e-2, 1.0, n_bins, True))
        g = r[2 * n_bins:]
        self.assertLess(np.max(np.abs(g - 1.0)), 0.05)
        self.assertLess(np.max(r[n_bins:2 * n_bins]), 0.05)

    def test_recurring_molecules_are_recovered(self):
        """Known answer: a Poisson background plus N_rec molecules whose second
        burst follows the first by a lag uniform in [tau1, tau2]. In that
        window G = 1 + N_rec / E_random and P_same = N_rec / (N_rec + E_random),
        both computable from the numbers that went in."""
        rng = np.random.default_rng(34)
        T = 2000.0
        bg = np.sort(rng.uniform(0, T, 20000))
        n_rec = 3000
        tau1, tau2 = 0.02, 0.05
        first = rng.uniform(0, T - tau2, n_rec)
        second = first + rng.uniform(tau1, tau2, n_rec)
        times = np.sort(np.concatenate([bg, first, second]))
        edges = np.array([tau1, tau2])
        ps = np.array(tttrlib.pair_statistics(times, edges, True))
        counts, expected = ps[0], ps[1]
        # the recurring pairs sit on top of the random pairs of ALL bursts
        p_same_est = 1 - expected / counts
        p_same_true = n_rec / (n_rec + expected)
        self.assertAlmostEqual(p_same_est, p_same_true, delta=0.02)
        # and outside the window there is nothing but chance
        ps_out = np.array(tttrlib.pair_statistics(times, np.array([0.1, 0.2]), True))
        self.assertAlmostEqual(ps_out[0] / ps_out[1], 1.0, delta=0.03)

    def test_recurrence_efficiencies_match_numpy_pairing(self):
        rng = np.random.default_rng(35)
        n = 3000
        times = rng.uniform(0, 100.0, n)          # deliberately unsorted
        eff = rng.uniform(0, 1, n)
        eff[rng.random(n) < 0.02] = np.nan
        e_min, e_max, dt_min, dt_max = 0.2, 0.4, 0.01, 0.05
        got = np.sort(np.array(tttrlib.recurrence_efficiencies(times, eff, e_min, e_max, dt_min, dt_max)))
        order = np.argsort(times)
        t, e = times[order], eff[order]
        ref = []
        for i in range(n):
            if not np.isfinite(e[i]) or e[i] < e_min or e[i] > e_max:
                continue
            lo = np.searchsorted(t, t[i] + dt_min, side="left")
            hi = np.searchsorted(t, t[i] + dt_max, side="right")
            for j in range(lo, hi):
                if j != i and np.isfinite(e[j]):
                    ref.append(e[j])
        np.testing.assert_allclose(got, np.sort(ref), rtol=0, atol=0)


if __name__ == "__main__":
    unittest.main()
