#!/usr/bin/env python3
r"""Two-Channel KDE dynamics filters (2CDE) — parity against the reference.

FRET-2CDE and ALEX-2CDE (Tomov et al., Biophys. J. 2012) quantify within-burst
kinetics from per-photon kernel-density estimates.  ``tttrlib.TwoCDE`` is a
C++ port of the reference FRETBursts implementation; these tests reimplement
that reference in plain NumPy (bit-for-bit the FRETBursts ``kde_laplace`` /
``kde_gaussian`` two-pointer kernels and the ``calc_fret_2cde`` /
``ALEX-2CDE`` burst loops) and check the C++ engine reproduces it to numerical
precision, plus the expected static-vs-dynamic separation.
"""
import unittest

import numpy as np

import tttrlib


# --- Reference NumPy implementation (transcribed from FRETBursts) -----------

def kde_ref(ts, tau, axis, kernel="laplace"):
    """Exact port of FRETBursts kde_laplace/kde_gaussian (ascending-index sum)."""
    ts = np.asarray(ts, dtype=np.float64)
    axis = np.asarray(axis, dtype=np.float64)
    r = np.zeros(axis.size, dtype=np.float64)
    if ts.size == 0:
        return r
    if kernel == "gaussian":
        lim = 3.0 * tau
        tau2 = 2.0 * tau * tau
        for i, t in enumerate(axis):
            sel = ts[(ts - t < lim) & (t - ts <= lim)]
            r[i] = np.exp(-((sel - t) ** 2) / tau2).sum()
    else:
        lim = 5.0 * tau
        for i, t in enumerate(axis):
            sel = ts[(ts - t < lim) & (t - ts <= lim)]
            r[i] = np.exp(-np.abs(sel - t) / tau).sum()
    return r


def fret_2cde_ref(tau, macro, mask_d, mask_a, bursts, kernel="laplace"):
    kde_d = kde_ref(macro[mask_d], tau, macro, kernel)
    kde_a = kde_ref(macro[mask_a], tau, macro, kernel)
    out = []
    for s, e in bursts:
        sl = slice(int(s), int(e) + 1)
        if not mask_d[sl].any() or not mask_a[sl].any():
            out.append(np.nan)
            continue
        kde_adi = kde_a[sl][mask_d[sl]]
        kde_ddi = kde_d[sl][mask_d[sl]]
        kde_dai = kde_d[sl][mask_a[sl]]
        kde_aai = kde_a[sl][mask_a[sl]]
        n_chd = mask_d[sl].sum()
        n_cha = mask_a[sl].sum()
        if kernel == "laplace":
            kde_ddi = (1 + 2 / n_chd) * (kde_ddi - 1)
            kde_aai = (1 + 2 / n_cha) * (kde_aai - 1)
        ed = np.mean(kde_adi / (kde_adi + kde_ddi))
        ea = np.mean(kde_dai / (kde_dai + kde_aai))
        out.append(110 - 100 * (ed + ea))
    return np.array(out)


def alex_2cde_ref(tau, macro, mask_dex, mask_aex, bursts):
    kde_dex = kde_ref(macro[mask_dex], tau, macro, "laplace")
    kde_aex = kde_ref(macro[mask_aex], tau, macro, "laplace")
    out = []
    for s, e in bursts:
        sl = slice(int(s), int(e) + 1)
        if not mask_dex[sl].any() or not mask_aex[sl].any():
            out.append(np.nan)
            continue
        kde_dexdex = kde_dex[sl][mask_dex[sl]]
        kde_aexdex = kde_aex[sl][mask_dex[sl]]
        kde_aexaex = kde_aex[sl][mask_aex[sl]]
        kde_dexaex = kde_dex[sl][mask_aex[sl]]
        n_chaex = mask_aex[sl].sum()
        n_chdex = mask_dex[sl].sum()
        br_dex = np.sum(kde_aexdex / kde_dexdex) / n_chaex
        br_aex = np.sum(kde_dexaex / kde_aexaex) / n_chdex
        out.append(100 - 50 * (br_dex - br_aex))
    return np.array(out)


# --- Synthetic TTTR builders ------------------------------------------------

def build_tttr(chan_per_burst, gap=1_000_000):
    """Build an in-memory TTTR; macro time increments by 1 per photon."""
    macro, chan, bounds = [], [], []
    t = 0
    for ch in chan_per_burst:
        start = len(macro)
        for c in ch:
            t += 1
            macro.append(t)
            chan.append(int(c))
        t += gap
        bounds.append((start, len(macro) - 1))  # inclusive stop
    d = tttrlib.TTTR()
    d.append_events(
        np.asarray(macro, dtype=np.uint64),
        np.zeros(len(macro), dtype=np.uint16),
        np.asarray(chan, dtype=np.int8),
        np.zeros(len(macro), dtype=np.int8),
        False, 0,
    )
    # Unit macro-time resolution -> tau in "seconds" equals tau in ticks.
    d.header.set_macro_time_resolution(1.0)
    return d, np.asarray(macro, dtype=np.float64), np.asarray(chan), \
        np.asarray(bounds, dtype=np.int64)


class TestTwoCDEParity(unittest.TestCase):

    def _run(self, chan_per_burst, tau_ticks, variant, kernel_name, donor=(0,),
             acceptor=(1,)):
        d, macro, chan, bounds = build_tttr(chan_per_burst)
        # macro_time_resolution == 1.0 (set in build_tttr) so tau in "seconds"
        # equals tau in macro-time ticks.
        tau_s = tau_ticks
        k = tttrlib.TwoCDE.GAUSSIAN if kernel_name == "gaussian" else tttrlib.TwoCDE.LAPLACE
        eng = tttrlib.TwoCDE(d)
        if variant == "alex":
            eng.set_donor_excitation(list(donor))
            eng.set_acceptor_excitation(list(acceptor))
            eng.compute(bounds, tau_s, tttrlib.TwoCDE.ALEX_2CDE, k)
            mask_d = np.isin(chan, donor)
            mask_a = np.isin(chan, acceptor)
            ref = alex_2cde_ref(tau_ticks, macro, mask_d, mask_a, bounds)
        else:
            eng.set_donor(list(donor))
            eng.set_acceptor(list(acceptor))
            eng.compute(bounds, tau_s, tttrlib.TwoCDE.FRET_2CDE, k)
            mask_d = np.isin(chan, donor)
            mask_a = np.isin(chan, acceptor)
            ref = fret_2cde_ref(tau_ticks, macro, mask_d, mask_a, bounds, kernel_name)
        got = eng.two_cde
        return got, ref

    def test_fret_2cde_laplace_parity(self):
        rng = np.random.default_rng(1)
        bursts = [(rng.random(300) < p).astype(int)
                  for p in rng.uniform(0.1, 0.9, size=40)]
        got, ref = self._run(bursts, tau_ticks=30.0, variant="fret", kernel_name="laplace")
        np.testing.assert_allclose(got, ref, rtol=1e-9, atol=1e-7)

    def test_fret_2cde_gaussian_parity(self):
        rng = np.random.default_rng(2)
        bursts = [(rng.random(300) < p).astype(int)
                  for p in rng.uniform(0.1, 0.9, size=40)]
        got, ref = self._run(bursts, tau_ticks=30.0, variant="fret", kernel_name="gaussian")
        np.testing.assert_allclose(got, ref, rtol=1e-9, atol=1e-7)

    def test_alex_2cde_parity(self):
        # channels: 0 = DexDem, 1 = DexAem, 2 = AexAem
        rng = np.random.default_rng(3)
        bursts = []
        for _ in range(40):
            n = 300
            r = rng.random(n)
            ch = np.where(r < 0.45, 0, np.where(r < 0.75, 1, 2))
            bursts.append(ch)
        got, ref = self._run(bursts, tau_ticks=30.0, variant="alex", kernel_name="laplace",
                             donor=(0, 1), acceptor=(2,))
        np.testing.assert_allclose(got, ref, rtol=1e-9, atol=1e-7)

    def test_static_vs_dynamic_separation(self):
        """FRET-2CDE ~10 for static bursts, higher for bursts with ms dynamics."""
        rng = np.random.default_rng(4)
        static = [(rng.random(400) < 0.5).astype(int) for _ in range(40)]
        dynamic = []
        for _ in range(40):
            blocks = [(rng.random(80) < (0.15 if i % 2 else 0.85)).astype(int)
                      for i in range(5)]
            dynamic.append(np.concatenate(blocks))
        got_s, _ = self._run(static, tau_ticks=40.0, variant="fret", kernel_name="laplace")
        got_d, _ = self._run(dynamic, tau_ticks=40.0, variant="fret", kernel_name="laplace")
        self.assertLess(np.nanmean(got_s), np.nanmean(got_d))

    def test_missing_stream_is_nan(self):
        """Bursts with only donor (or only acceptor) photons yield NaN."""
        bursts = [np.zeros(50, dtype=int), np.ones(50, dtype=int),
                  np.r_[np.zeros(25), np.ones(25)].astype(int)]
        got, _ = self._run(bursts, tau_ticks=20.0, variant="fret", kernel_name="laplace")
        self.assertTrue(np.isnan(got[0]))  # donor only
        self.assertTrue(np.isnan(got[1]))  # acceptor only
        self.assertFalse(np.isnan(got[2]))  # both present


if __name__ == "__main__":
    unittest.main()
