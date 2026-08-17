"""A/B of the photon correlator against independent references.

What is compared, and against what:

* **Unnormalized correlation** (`Correlator.get_corr`) — the weighted pair
  count per lag bin — against a plain NumPy pair counter written from the
  *definition* of each lag structure (not from the C++):

  - ``laurence``: pairs with ``x[k] <= t2 - t1 < x[k+1]`` (Laurence et al.
    2006, Opt. Lett. 31, 829: arbitrary bin edges, exact pair counting).
  - ``wahl``: cascade ``c`` coarsens both streams by ``2**c`` and counts pairs
    at *coarse* lag ``(x[c*n_bins] >> c) + d``, ``d = 1..n_bins`` (Wahl et al.
    2003, Opt. Express 11, 3383: multi-tau with time coarsening).
  - ``felekyan``: block 0 at full resolution for lags ``0..n_bins``, block
    ``k >= 1`` at coarsening ``2**(k-1)`` (Felekyan et al. 2005, Rev. Sci.
    Instrum. 76, 083104).

  A second, external pair counter — ``pycorrelate.pcorrelate`` in
  ``benchmarks/.venvs/pycorrelate`` — is run through a subprocess on the same
  data and must agree with the NumPy counter and with tttrlib.

* **Normalized correlation** (`get_corr_normalized`) against the *analytic*
  autocorrelation of a simulated two-state blinking emitter,
  ``G(tau) = 1 + ((1-p)/p) exp(-(k_on + k_off) tau)`` — a known answer that
  does not depend on any correlator implementation, and that exercises the
  normalization (count rates, ``T - tau``, coarse-bin width) end to end. The
  same trace split at random into two detectors gives the cross-correlation
  known answer.

* ``wahl`` against ``multipletau.autocorrelate`` (Schaetzel/Wahl multi-tau on
  the binned intensity trace) at overlapping lags — a bounded comparison,
  since the two schemes place their lag bins differently.

Two things this A/B found (2026-08-17), both fixed the same day and pinned:

* ``laurence`` cross-correlation **was** broken: `ccf_laurence` formed
  ``p2.times[j] - ti`` in unsigned arithmetic, so a channel-2 photon earlier
  than the current channel-1 photon wrapped to ~1.8e19 and the ``jmin`` pointer
  of the first bin could never advance. A second stream starting before the
  first gave all-zero bins; starting after, the first populated bin got the
  cumulative count of all earlier partners. The comparison now moves the bin
  edges onto the partner's axis (``t2 < t1 + tau``). Autocorrelation bin 0
  (self pairs) is still zeroed by the normalization.
* ``felekyan`` **was labelled with the wahl axis**: block ``k >= 1`` counts
  coarse lags at coarsening ``2**(k-1)`` while the shared axis advanced by
  ``2**k`` per bin, so every block-k value sat at up to twice its true lag
  (25% off at the end of a block, and the lags between two blocks never
  counted). The method now has its own contiguous axis in
  ``CorrelatorCurve::update_axis`` -- block 0 at spacing 1, block k at
  ``2**(k-1)`` -- which is what ``ccf_felekyan`` counts and
  ``normalize_ccf_felekyan`` divides by; the analytic bracket below passes.

Also recorded: the ``wahl`` label is the *upper* end of its coarse bin by up to
one coarse step (``x[c*n_bins] mod 2**c`` is dropped by the integer offset),
which is why the analytic comparison brackets each estimate between
``G(x)`` and ``G(x - step)`` instead of demanding ``G(x)`` itself.
"""
import json
import os
import subprocess
import sys
import unittest

import numpy as np
import pytest

import tttrlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
_PYCORRELATE_PY = os.path.join(_ROOT, "benchmarks", ".venvs", "pycorrelate", "bin", "python")


# --------------------------------------------------------------------------
# NumPy references written from the definitions
# --------------------------------------------------------------------------

def multi_tau_axis(n_bins, n_casc):
    """x[0] = 0, then a step that doubles every ``n_bins`` bins (tttrlib axis)."""
    n = n_bins * n_casc + 1
    x = np.zeros(n, dtype=np.uint64)
    step = 1
    for j in range(1, n):
        x[j] = x[j - 1] + step
        if j % n_bins == 0:
            step <<= 1
    return x


def _lag_table(T2, w2, extra):
    """W2[t] = sum of weights of channel-2 events at (coarse) time t."""
    T2 = T2.astype(np.int64)
    return np.bincount(T2, weights=w2, minlength=int(T2.max()) + extra + 2)


def pairs_at_lag(T1, w1, W2, lag):
    idx = T1.astype(np.int64) + lag
    m = idx < len(W2)
    return float(np.sum(w1[m] * W2[idx[m]]))


def wahl_reference(t1, w1, t2, w2, n_bins, n_casc):
    x = multi_tau_axis(n_bins, n_casc)
    corr = np.zeros(len(x))
    for c in range(n_casc):
        T1 = t1 >> np.uint64(c)
        T2 = t2 >> np.uint64(c)
        off = int(x[c * n_bins]) >> c
        W2 = _lag_table(T2, w2, off + n_bins)
        for d in range(1, n_bins + 1):
            corr[c * n_bins + d] = pairs_at_lag(T1, w1, W2, off + d)
    return x, corr


def felekyan_reference(t1, w1, t2, w2, n_bins, n_casc, x=None):
    """Block k counts n_bins lags at coarse spacing pw = 2^(k-1) (block 0: 1)
    from the block start x[k*n_bins]/pw. `x` is the correlator's own felekyan
    axis (block starts n_bins*2^(k-1)); the wahl axis is used only if none is
    given, which reproduces the pre-2026-08-17 mislabelled structure."""
    if x is None:
        x = multi_tau_axis(n_bins, n_casc)
    x = np.asarray(x, dtype=np.uint64)
    corr = np.zeros(len(x))
    for k in range(n_casc):
        pw = 1 if k == 0 else 1 << (k - 1)
        T1 = t1 // np.uint64(pw)
        T2 = t2 // np.uint64(pw)
        off = int(x[k * n_bins]) // pw
        W2 = _lag_table(T2, w2, off + n_bins)
        ds = range(0, n_bins + 1) if k == 0 else range(1, n_bins + 1)
        for d in ds:
            corr[k * n_bins + d] += pairs_at_lag(T1, w1, W2, off + d)
    return x, corr


def laurence_reference(t1, w1, t2, w2, edges):
    """corr[k] = sum w1_i w2_j over pairs with edges[k] <= t2 - t1 < edges[k+1]."""
    t1 = t1.astype(np.int64)
    t2 = t2.astype(np.int64)
    c2 = np.concatenate([[0.0], np.cumsum(w2)])
    corr = np.zeros(len(edges))
    e = np.asarray(edges, dtype=np.int64)
    for k in range(len(e) - 1):
        lo = np.searchsorted(t2, t1 + e[k], side="left")
        hi = np.searchsorted(t2, t1 + e[k + 1], side="left")
        corr[k] = float(np.sum(w1 * (c2[hi] - c2[lo])))
    return corr


def run_correlator(method, t1, w1, t2, w2, n_bins, n_casc):
    c = tttrlib.Correlator(method=method, n_bins=n_bins, n_casc=n_casc)
    c.set_macrotimes(np.asarray(t1, np.uint64), np.asarray(t2, np.uint64))
    c.set_weights(np.asarray(w1, float), np.asarray(w2, float))
    return np.array(c.x_axis), np.array(c.get_corr()), np.array(c.get_corr_normalized())


def random_streams(rng, n1, n2, span=200000, weights=True):
    t1 = np.sort(rng.integers(0, span, n1)).astype(np.uint64)
    t2 = np.sort(rng.integers(0, span, n2)).astype(np.uint64)
    if weights:
        w1 = rng.random(n1)
        w2 = rng.integers(0, 3, n2).astype(float)
    else:
        w1 = np.ones(n1)
        w2 = np.ones(n2)
    return t1, w1, t2, w2


# --------------------------------------------------------------------------
# A known answer: a two-state blinking emitter
# --------------------------------------------------------------------------

def telegraph_photons(rng, k_on, k_off, q, T):
    """Photon times (integer ticks) of an emitter that blinks between an ON
    state (Poisson emission at rate ``q`` per tick) and a dark state, with
    exponential dwell times ``1/k_off`` (ON) and ``1/k_on`` (OFF)."""
    t = 0.0
    on = rng.random() < k_on / (k_on + k_off)
    chunks = []
    while t < T:
        dwell = rng.exponential(1.0 / (k_off if on else k_on))
        if on:
            n = rng.poisson(q * dwell)
            chunks.append(np.sort(rng.random(n)) * dwell + t)
        t += dwell
        on = not on
    tt = np.concatenate(chunks)
    tt = tt[tt < T]
    return np.sort(np.floor(tt).astype(np.uint64))


def telegraph_g(tau, k_on, k_off):
    p = k_on / (k_on + k_off)
    return 1.0 + ((1.0 - p) / p) * np.exp(-(k_on + k_off) * np.asarray(tau, float))


class _Telegraph:
    """One simulated trace shared by the normalization tests."""
    k_on = 1.0 / 2000.0
    k_off = 1.0 / 2000.0
    q = 0.1
    T = 2.0e7
    _t = None

    @classmethod
    def times(cls):
        if cls._t is None:
            cls._t = telegraph_photons(np.random.default_rng(7), cls.k_on, cls.k_off, cls.q, cls.T)
        return cls._t


def _bracket_check(test, x, g_est, g_true, below, above, tol, tau_max, amp_sigma):
    """Known-answer check with the single-trajectory statistics kept honest.

    A finite trajectory has an ON fraction that differs from ``p`` by
    ``~sqrt(2 p (1-p) / (lambda T))``, which moves the whole amplitude
    ``G(0) - 1`` by a few percent while leaving the *shape* alone. So: (1) the
    amplitude, read from the first eight lags, must be within ``amp_sigma``
    of the analytic one; (2) with the amplitude rescaled to the analytic value
    the estimate at every lag must lie between the analytic values at the two
    ends of its labelled bin, ``[G(x + above), G(x - below)]`` (G is monotone
    decreasing), up to ``tol``."""
    sel = (x > 0) & (x < tau_max)
    xs, gs = x[sel], g_est[sel]
    first = slice(0, 8)
    amp_est = np.mean((gs[first] - 1.0) / (g_true(xs[first]) - 1.0))   # ~1 if unbiased
    test.assertLess(abs(amp_est - 1.0), amp_sigma, f"amplitude off: {amp_est}")
    g_scaled = 1.0 + (gs - 1.0) / amp_est
    hi = g_true(np.maximum(xs - below[sel], 0.0)) + tol
    lo = g_true(xs + above[sel]) - tol
    ok = (g_scaled <= hi) & (g_scaled >= lo)
    test.assertTrue(np.all(ok), f"outside bracket at tau={xs[~ok]}: est={g_scaled[~ok]}, "
                                f"bracket=[{lo[~ok]}, {hi[~ok]}]")


class TestUnnormalizedAgainstNumpyPairCounting(unittest.TestCase):
    """The weighted pair counts equal the definition of each lag structure."""

    def test_wahl_equals_coarse_lag_pair_counts(self):
        rng = np.random.default_rng(3)
        for trial in range(3):
            n1, n2 = rng.integers(500, 3000, 2)
            t1, w1, t2, w2 = random_streams(rng, n1, n2)
            for nb, nc in [(8, 6), (17, 10), (3, 4)]:
                with self.subTest(trial=trial, n_bins=nb, n_casc=nc):
                    x, got, _ = run_correlator("wahl", t1, w1, t2, w2, nb, nc)
                    xr, ref = wahl_reference(t1, w1, t2, w2, nb, nc)
                    np.testing.assert_array_equal(x, xr)
                    np.testing.assert_allclose(got, ref, rtol=1e-12, atol=1e-9)

    def test_wahl_unit_weights_are_exact_integers(self):
        rng = np.random.default_rng(4)
        t1, w1, t2, w2 = random_streams(rng, 4000, 3500, weights=False)
        x, got, _ = run_correlator("wahl", t1, w1, t2, w2, 10, 8)
        _, ref = wahl_reference(t1, w1, t2, w2, 10, 8)
        self.assertTrue(np.array_equal(got, ref))

    def test_felekyan_equals_its_block_structure(self):
        rng = np.random.default_rng(5)
        for trial in range(3):
            n1, n2 = rng.integers(500, 3000, 2)
            t1, w1, t2, w2 = random_streams(rng, n1, n2)
            for nb, nc in [(8, 6), (17, 10), (3, 4)]:
                with self.subTest(trial=trial, n_bins=nb, n_casc=nc):
                    x, got, _ = run_correlator("felekyan", t1, w1, t2, w2, nb, nc)
                    _, ref = felekyan_reference(t1, w1, t2, w2, nb, nc, x)
                    np.testing.assert_allclose(got, ref, rtol=1e-12, atol=1e-9)
                    # the axis is contiguous: block 0 at spacing 1, block k at 2^(k-1)
                    step = np.diff(x)
                    for k in range(nc):
                        pw = 1 if k == 0 else 1 << (k - 1)
                        self.assertTrue(np.all(step[k * nb:(k + 1) * nb] == pw), (k, step))

    def test_laurence_autocorrelation_equals_exact_pair_counts(self):
        """Bins k >= 1 of an autocorrelation are exact; bin 0 (self pairs) is
        the one the normalization zeroes and is excluded here (see the module
        docstring for why it is wrong unnormalized)."""
        rng = np.random.default_rng(6)
        for nb, nc in [(8, 6), (17, 8)]:
            t1 = np.sort(rng.integers(0, 200000, 2500)).astype(np.uint64)
            w1 = rng.random(2500)
            with self.subTest(n_bins=nb, n_casc=nc):
                x, got, _ = run_correlator("laurence", t1, w1, t1, w1, nb, nc)
                ref = laurence_reference(t1, w1, t1, w1, x)
                np.testing.assert_allclose(got[1:-1], ref[1:-1], rtol=1e-12, atol=1e-9)

    def test_laurence_cross_correlation_equals_exact_pair_counts(self):
        """Cross-correlation with the second stream starting EARLIER than the
        first: ccf_laurence used to compute ``t2 - t1`` in unsigned arithmetic,
        so an earlier partner photon wrapped to ~2^64 and stopped the skip
        loop (found by this A/B 2026-08-17, fixed the same day). Both
        orderings must equal the exact pair counts."""
        rng = np.random.default_rng(1)
        t1 = np.sort(rng.integers(100, 100000, 2000)).astype(np.uint64)
        t2 = np.sort(rng.integers(50, 100000, 2000)).astype(np.uint64)
        w = np.ones(2000)
        x, got, _ = run_correlator("laurence", t1, w, t2, w, 8, 6)
        ref = laurence_reference(t1, w, t2, w, x)
        np.testing.assert_allclose(got[:-1], ref[:-1], rtol=1e-12, atol=1e-9)


@unittest.skipUnless(os.path.exists(_PYCORRELATE_PY), "pycorrelate venv not built (benchmarks/build_envs.sh)")
class TestAgainstPycorrelate(unittest.TestCase):
    """pycorrelate.pcorrelate is an independent exact pair counter (Laurence
    algorithm). It is run in its own venv on the same event streams."""

    @staticmethod
    def _pcorrelate(t, u, bins):
        script = (
            "import sys, json, numpy as np, pycorrelate\n"
            "d = json.load(sys.stdin)\n"
            "t = np.array(d['t'], dtype=np.int64); u = np.array(d['u'], dtype=np.int64)\n"
            "b = np.array(d['bins'], dtype=np.int64)\n"
            "g = pycorrelate.pcorrelate(t, u, b) * np.diff(b)   # pcorrelate divides by the bin width\n"
            "print(json.dumps([float(v) for v in g]))\n"
        )
        payload = json.dumps({"t": [int(v) for v in t], "u": [int(v) for v in u], "bins": [int(v) for v in bins]})
        out = subprocess.run([_PYCORRELATE_PY, "-c", script], input=payload, capture_output=True, text=True, check=True)
        return np.array(json.loads(out.stdout.strip().splitlines()[-1]))

    @pytest.mark.slow
    def test_laurence_acf_bins_match_pcorrelate(self):
        rng = np.random.default_rng(11)
        t = np.sort(rng.integers(0, 50000, 1500)).astype(np.uint64)
        w = np.ones(1500)
        x, got, _ = run_correlator("laurence", t, w, t, w, 8, 5)
        ref = self._pcorrelate(t, t, x[1:])   # bins from lag 1 on (self pairs excluded)
        np.testing.assert_allclose(got[1:-1], ref, rtol=0, atol=1e-9)
        # and pcorrelate agrees with the NumPy counter used throughout
        np.testing.assert_allclose(laurence_reference(t, w, t, w, x)[1:-1], ref, rtol=0, atol=1e-9)

    @pytest.mark.heavy
    def test_wahl_cascades_match_pcorrelate_on_coarsened_times(self):
        """Every cascade of the wahl curve is pcorrelate on the times
        coarsened by 2**c with unit-width bins at the coarse lags."""
        rng = np.random.default_rng(12)
        t1 = np.sort(rng.integers(0, 60000, 1200)).astype(np.uint64)
        t2 = np.sort(rng.integers(0, 60000, 1400)).astype(np.uint64)
        w1 = np.ones(1200)
        w2 = np.ones(1400)
        nb, nc = 6, 5
        x, got, _ = run_correlator("wahl", t1, w1, t2, w2, nb, nc)
        for c in range(nc):
            off = int(x[c * nb]) >> c
            bins = np.arange(off + 1, off + nb + 2)
            ref = self._pcorrelate(t1 >> np.uint64(c), t2 >> np.uint64(c), bins)
            with self.subTest(cascade=c):
                np.testing.assert_allclose(got[c * nb + 1: c * nb + nb + 1], ref, rtol=0, atol=1e-9)


class TestNormalizedAgainstTheBlinkingEmitter(unittest.TestCase):
    """G(tau) of a simulated blinking emitter equals 1 + ((1-p)/p) e^{-lambda tau}."""

    def _g_true(self):
        return lambda tau: telegraph_g(tau, _Telegraph.k_on, _Telegraph.k_off)

    def test_wahl_autocorrelation(self):
        t = _Telegraph.times()
        w = np.ones(len(t))
        x, _, g = run_correlator("wahl", t, w, t, w, 8, 12)
        step = np.diff(x, prepend=0).astype(float)      # coarse bin width per label
        zero = np.zeros_like(step)
        _bracket_check(self, x, g, self._g_true(), step, zero, tol=0.02, tau_max=3e4, amp_sigma=0.08)
        # far tail is 1
        tail = (x > 2e4) & (x < 1e6)
        self.assertLess(np.max(np.abs(g[tail] - 1.0)), 0.02)

    def test_laurence_autocorrelation(self):
        t = _Telegraph.times()
        w = np.ones(len(t))
        x, _, g = run_correlator("laurence", t, w, t, w, 8, 12)
        # laurence labels the *lower* edge: estimate lies in [G(x_{k+1}), G(x_k)]
        width = np.diff(x, append=x[-1] + (x[-1] - x[-2])).astype(float)
        zero = np.zeros_like(width)
        _bracket_check(self, x, g, self._g_true(), zero, width, tol=0.02, tau_max=3e4, amp_sigma=0.08)

    def test_wahl_cross_correlation_of_a_random_split(self):
        """The same trace split by a fair coin into two detectors: the
        cross-correlation is the emitter's G(tau) (no self pairs, no afterpulsing)."""
        t = _Telegraph.times()
        rng = np.random.default_rng(8)
        pick = rng.random(len(t)) < 0.5
        t1, t2 = t[pick], t[~pick]
        x, _, g = run_correlator("wahl", t1, np.ones(len(t1)), t2, np.ones(len(t2)), 8, 12)
        step = np.diff(x, prepend=0).astype(float)
        zero = np.zeros_like(step)
        _bracket_check(self, x, g, self._g_true(), step, zero, tol=0.03, tau_max=3e4, amp_sigma=0.08)
        # order does not matter beyond noise
        _, _, g2 = run_correlator("wahl", t2, np.ones(len(t2)), t1, np.ones(len(t1)), 8, 12)
        sel = (x > 0) & (x < 3e4)
        self.assertLess(np.max(np.abs(g[sel] - g2[sel])), 0.05)

    def test_felekyan_autocorrelation(self):
        """felekyan on its own contiguous axis (block k at spacing 2^(k-1))
        brackets the closed-form G(tau). Until 2026-08-17 the method was
        labelled with the wahl axis (spacing 2^k), so every block-k value sat
        at up to twice its true lag; found by this test, fixed the same day."""
        t = _Telegraph.times()
        w = np.ones(len(t))
        x, _, g = run_correlator("felekyan", t, w, t, w, 8, 12)
        step = np.diff(x, prepend=0).astype(float)
        zero = np.zeros_like(step)
        _bracket_check(self, x, g, self._g_true(), step, zero, tol=0.02, tau_max=3e4, amp_sigma=0.08)


@unittest.skipUnless(os.path.exists(_PYCORRELATE_PY), "reference venv not built (benchmarks/build_envs.sh)")
class TestAgainstMultipletau(unittest.TestCase):
    """multipletau (Schaetzel/Wahl multi-tau on a binned trace, in the
    benchmarks/.venvs/pycorrelate venv) and the wahl photon correlator estimate
    the same G(tau) of the same data -- bounded, the lag bins are placed
    differently."""

    def test_wahl_matches_multipletau_at_short_lags(self):
        t = _Telegraph.times()
        w = np.ones(len(t))
        x, _, g = run_correlator("wahl", t, w, t, w, 16, 10)
        script = (
            "import sys, json, numpy as np, multipletau\n"
            "d = json.load(sys.stdin)\n"
            "t = np.array(d['t'], dtype=np.int64)\n"
            "trace = np.bincount(t, minlength=int(d['T'])).astype(float)\n"
            "mt = multipletau.autocorrelate(trace, m=16, deltat=1.0, normalize=True)\n"
            "print(json.dumps({'tau': mt[:, 0].tolist(), 'g': mt[:, 1].tolist()}))\n"
        )
        payload = json.dumps({"t": [int(v) for v in t], "T": int(_Telegraph.T)})
        out = subprocess.run([_PYCORRELATE_PY, "-c", script], input=payload,
                             capture_output=True, text=True, check=True)
        res = json.loads(out.stdout.strip().splitlines()[-1])
        tau_mt, g_mt = np.array(res["tau"]), np.array(res["g"]) + 1.0
        sel = (x >= 1) & (x <= 300)          # cascades whose coarse step is << tau_c
        g_interp = np.interp(x[sel], tau_mt, g_mt)
        self.assertLess(np.max(np.abs(g[sel] - g_interp) / (g_interp - 1.0)), 0.03)


class TestStreamingCorrelatorIsTheBatchCorrelator(unittest.TestCase):
    """The incremental correlator (its own approximate multi-tau) reproduces the
    batch curve to a few percent; the detailed pins live in
    test/python/streaming/test_streaming_correlator.py. Here: the blinking
    trace, whose G is also known analytically."""

    def test_same_curve_as_batch_on_the_blinking_trace(self):
        t = _Telegraph.times()[:300000]
        w = np.ones(len(t))
        x, _, g = run_correlator("wahl", t, w, t, w, 8, 10)
        s = tttrlib.StreamingCorrelator(8, 10, 1.0)
        for v in t:
            s.push_photon(int(v))
        s.flush()
        gs = np.asarray(s.get_correlation_normalized(), dtype=float)
        np.testing.assert_allclose(np.asarray(s.get_x_axis(), dtype=float), x)
        sel = (x > 0) & (x < 2e4)
        np.testing.assert_allclose(gs[sel], g[sel], rtol=0.05)


if __name__ == "__main__":
    unittest.main()
