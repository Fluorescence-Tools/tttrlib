# SPDX-License-Identifier: BSD-3-Clause
"""A/B of the burst-search family against independent references.

Every kernel in ``modules/spectroscopy/burst`` (bar BVA / 2CDE / recurrence,
which have their own A/B file) plus the core photon-selection primitives, each
compared with something the code was *not* ported from:

* sliding window        -- FRETBursts ``bsearch`` (compiled and pure Python),
                           driven in its own virtualenv; and a NumPy
                           transcription of the same rule that always runs
* dual-channel (DCBS)   -- FRETBursts ``Bursts.and_gate`` on two streams vs
                           ``burst_search_coincident``
* CUSUM / SPRT          -- Zhang & Yang 2005 in NumPy (continuous exponential
                           form) and PAM's discretised implementation run in
                           Octave, behaviourally
* Kalman                -- ChiSurf's ``KalmanBurstDetector`` (an independent
                           Python detector: bin, filter, extract) live
* BOCPD                 -- Adams & MacKay 2007 with the plug-in Poisson
                           predictive, transcribed from the pre-delegation
                           ChiSurf numba code (chisurf ``fffe299c3``)
* Bayesian Blocks       -- astropy ``bayesian_blocks(fitness='events')``,
                           recorded fixture, through a small C++ harness since
                           the DP has no Python binding
* significance          -- scipy (Poisson tail, normal quantile), Li & Ma 1983
                           eq. 17 in NumPy, the trials/FAR formulas in NumPy
* confidence            -- NumPy transcription of the documented statistic
* selection primitives  -- NumPy (``searchsorted`` / ``bincount``)
* BurstFilter/Feature   -- NumPy on the burst indices; FRETBursts burst stats

* BurstML               -- the original FRET_burstML MEX likelihood
                           (``mlhDiffNTRbkg_MT.cpp`` from the archive in
                           junk/), compiled natively with the shims in
                           test/cpp/burstml_mex_shim and GSL

The max-tree search has no independent implementation available and stays
known-answer tested in test/python/test_burst_search_maxtree.py.

Reference libraries are optional; a missing one skips its class visibly.
"""
from __future__ import annotations

import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np

import tttrlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FRETBURSTS_PY = os.path.join(ROOT, "benchmarks", ".venvs", "fretbursts", "bin", "python")
HAVE_FRETBURSTS = os.path.exists(FRETBURSTS_PY)
OCTAVE = shutil.which("octave")
PAM_M = os.path.abspath(os.path.join(ROOT, "..", "chisurf", "junk", "PAM", "PAM.m"))
HAVE_PAM = OCTAVE is not None and os.path.exists(PAM_M)
BB_FIXTURE = os.path.join(ROOT, "test", "data", "reference",
                          "bayesian_blocks_astropy_reference.npz")

try:
    from scipy import stats as sp_stats
    from scipy.special import gammaln
    HAVE_SCIPY = True
except Exception:  # pragma: no cover
    HAVE_SCIPY = False


def _import_chisurf_kalman():
    sibling = os.path.abspath(os.path.join(ROOT, "..", "chisurf"))
    for attempt in (None, sibling):
        try:
            if attempt is not None and attempt not in sys.path:
                sys.path.insert(0, attempt)
            import importlib
            return importlib.import_module("chisurf.core.fluorescence.burst.kalman")
        except Exception:
            continue
    return None


_CHISURF_KALMAN = _import_chisurf_kalman()
HAVE_CHISURF = _CHISURF_KALMAN is not None

RES = 1e-7  # seconds per macro-time tick in every synthetic stream here


# ---------------------------------------------------------------------------
# streams
# ---------------------------------------------------------------------------

def make_tttr(ticks, channels=None, resolution=RES):
    ticks = np.asarray(ticks, dtype=np.uint64)
    n = ticks.size
    ch = np.zeros(n, dtype=np.int8) if channels is None else np.asarray(channels, dtype=np.int8)
    tttr = tttrlib.TTTR(ticks, np.zeros(n, dtype=np.uint16), ch, np.zeros(n, dtype=np.int8))
    tttr.header.set_macro_time_resolution(resolution)
    return tttr


def bursty_stream(seed, background_cps=3000.0, duration=1.0,
                  bursts=((0.2, 0.003, 30000.0), (0.5, 0.002, 60000.0), (0.8, 0.004, 25000.0)),
                  two_channels=True):
    """Poisson background plus injected bursts; returns (ticks, channels)."""
    rng = np.random.default_rng(seed)
    t = [rng.uniform(0.0, duration, rng.poisson(background_cps * duration))]
    for t0, length, rate in bursts:
        t.append(rng.uniform(t0, t0 + length, rng.poisson(rate * length)))
    t = np.sort(np.concatenate(t))
    ticks = np.round(t / RES).astype(np.int64)
    ticks = np.maximum.accumulate(ticks)
    ch = rng.integers(0, 2, ticks.size).astype(np.int8) if two_channels else np.zeros(ticks.size, np.int8)
    return ticks, ch


def pairs(x):
    return np.asarray(x, dtype=np.int64).reshape(-1, 2)


# ---------------------------------------------------------------------------
# sliding window -- FRETBursts
# ---------------------------------------------------------------------------

def sliding_window_reference(times, L, m, T):
    """FRETBursts' rule (Ingargiola 2016; ``bsearch_py``) in NumPy: a burst is a
    run of positions where m consecutive photons span <= T ticks; its last
    photon is m-1 after the last such position; keep runs with >= L photons."""
    times = np.asarray(times, dtype=np.int64)
    n = times.size
    if n < m:
        return np.zeros((0, 2), dtype=np.int64)
    above = (times[m - 1:] - times[:n - m + 1]) <= T
    padded = np.concatenate([[False], above, [False]]).astype(np.int8)
    d = np.diff(padded)
    starts = np.flatnonzero(d == 1)
    ends = np.flatnonzero(d == -1) - 1          # last position of the run
    istop = ends + m - 1                          # last photon of the burst
    keep = (istop - starts + 1) >= L
    return np.stack([starts[keep], istop[keep]], axis=1)


class TestSlidingWindowAgainstFretbursts(unittest.TestCase):
    """``TTTR.burst_search_sliding_window`` vs FRETBursts' ``bsearch``."""

    CASES = [(20, 10, 5000), (10, 5, 2000), (30, 15, 8000), (5, 3, 400), (50, 20, 30000)]

    def _tttrlib(self, ticks, L, m, T_ticks):
        tttr = make_tttr(ticks)
        # T is given in seconds and truncated to ticks inside; put it mid-tick
        T = (T_ticks + 0.5) * RES
        return pairs(tttr.burst_search_sliding_window(L, m, T))

    def test_numpy_transcription_of_the_fretbursts_rule(self):
        for seed in range(6):
            ticks, _ = bursty_stream(seed, two_channels=False)
            for L, m, T in self.CASES:
                with self.subTest(seed=seed, L=L, m=m, T=T):
                    got = self._tttrlib(ticks, L, m, T)
                    ref = sliding_window_reference(ticks, L, m, T)
                    np.testing.assert_array_equal(got, ref)
                    self.assertGreater(len(ref), 0)

    def test_burst_that_runs_to_the_end_of_the_stream(self):
        # FRETBursts has a dedicated off-by-one branch for this case
        rng = np.random.default_rng(3)
        ticks = np.sort(np.concatenate([rng.uniform(0, 1e6, 500), rng.uniform(1e6 - 2000, 1e6, 200)])).astype(np.int64)
        got = self._tttrlib(ticks, 20, 10, 500)
        ref = sliding_window_reference(ticks, 20, 10, 500)
        np.testing.assert_array_equal(got, ref)
        self.assertEqual(got[-1, 1], ticks.size - 1)

    @unittest.skipUnless(HAVE_FRETBURSTS, "FRETBursts virtualenv not built (benchmarks/build_envs.sh)")
    def test_live_against_fretbursts_bsearch(self):
        cases = [(seed, L, m, T) for seed in range(3) for L, m, T in self.CASES[:3]]
        streams = {seed: bursty_stream(seed)[0] for seed in range(3)}
        ref = _run_fretbursts([(streams[seed], L, m, T, None, None) for seed, L, m, T in cases])
        for i, (seed, L, m, T) in enumerate(cases):
            with self.subTest(seed=seed, L=L, m=m, T=T):
                got = self._tttrlib(streams[seed], L, m, T)
                np.testing.assert_array_equal(got, ref[f"bursts_c_{i}"][:, :2])
                np.testing.assert_array_equal(got, ref[f"bursts_py_{i}"][:, :2])


def _run_fretbursts(cases):
    """cases: list of (ticks, L, m, T_ticks, mask_a | None, mask_b | None)."""
    with tempfile.TemporaryDirectory() as d:
        inp, outp = os.path.join(d, "in.npz"), os.path.join(d, "out.npz")
        payload = {"n_cases": len(cases)}
        for i, (ticks, L, m, T, mask_a, mask_b) in enumerate(cases):
            payload[f"times_{i}"] = np.asarray(ticks, dtype=np.int64)
            payload[f"L_{i}"], payload[f"m_{i}"], payload[f"T_{i}"] = L, m, T
            if mask_a is not None:
                payload[f"mask_a_{i}"] = mask_a
                payload[f"mask_b_{i}"] = mask_b
        np.savez(inp, **payload)
        subprocess.run([FRETBURSTS_PY, os.path.join(HERE, "_fretbursts_ab_driver.py"), inp, outp],
                       check=True, capture_output=True)
        return dict(np.load(outp))


def _merge_overlapping(intervals):
    """Union of inclusive index intervals that overlap or touch."""
    out = []
    for s, e in sorted(map(tuple, intervals)):
        if out and s <= out[-1][1] + 1:
            out[-1][1] = max(out[-1][1], e)
        else:
            out.append([s, e])
    return np.asarray(out, dtype=np.int64).reshape(-1, 2)


@unittest.skipUnless(HAVE_FRETBURSTS, "FRETBursts virtualenv not built (benchmarks/build_envs.sh)")
class TestCoincidentAgainstFretburstsAndGate(unittest.TestCase):
    """``burst_search_coincident`` with two channel groups and the sliding
    window is the dual-channel burst search; FRETBursts decides the AND in
    time with ``Bursts.and_gate`` and re-indexes the whole stream.

    One documented difference: a per-stream search can return bursts whose
    *global* photon ranges overlap (the streams interleave), and FRETBursts'
    ``and_gate`` then emits overlapping coincident bursts, whereas the vote
    count here fuses them into one -- so the reference is compared after
    taking the union of overlapping intervals."""

    def test_two_group_coincidence_is_fretbursts_and_gate(self):
        L, m, T = 20, 10, 5000
        streams = [bursty_stream(seed) for seed in range(6)]
        ref = _run_fretbursts([(t, L, m, T, ch == 0, ch == 1) for t, ch in streams])
        n_bursts = 0
        for seed, (ticks, ch) in enumerate(streams):
            tttr = make_tttr(ticks, ch)
            got = np.asarray(tttr.burst_search_coincident(
                [[0], [1]], algorithm="sliding_window", min_groups=0, L=L,
                parameters={"L": L, "m": m, "T": (T + 0.5) * RES})).reshape(-1, 2)
            expect = _merge_overlapping(ref[f"dcbs_{seed}"])
            expect = expect[(expect[:, 1] - expect[:, 0] + 1) >= L]
            with self.subTest(seed=seed):
                np.testing.assert_array_equal(got, expect)
            n_bursts += len(got)
        self.assertGreater(n_bursts, 0)


# ---------------------------------------------------------------------------
# CUSUM / SPRT -- Zhang & Yang 2005
# ---------------------------------------------------------------------------

def cusum_sprt_reference(dt_ms, IB, sb_ratio, alpha, beta, min_photons):
    """Zhang & Yang, J. Phys. Chem. B 109, 21930 (2005), continuous form.

    Photon-by-photon CUSUM on the log-likelihood ratio of the inter-photon time
    under the burst rate I1 = (S/B) * IB against the background rate IB
    (exponential waiting times), threshold h from their eq. 4; the burst end by
    Wald's SPRT with error rates (alpha, beta); the start refined by a
    backwards CUSUM. Rates in counts/ms, dt in ms.
    """
    N = dt_ms.size
    I1 = sb_ratio * IB
    KL = (IB - I1) / I1 + math.log(I1 / IB)          # expected LLR per photon
    A = (1.0 - beta) / alpha
    B = beta / (1.0 - alpha)
    hA = math.log(A) / (I1 - IB)
    hB = math.log(B) / (I1 - IB)
    hC = math.log(I1 / IB) / (I1 - IB)
    h = -math.log(alpha / 3.0 / (KL + 1.0) ** 2 * math.log(1.0 / alpha))
    Sa, Sb = math.log(I1 / IB), (I1 - IB)
    nd = int(round(math.log(1.0 / alpha) / KL))
    llr = Sa - dt_ms * Sb                            # per-photon log LR

    def cusum(i, f):
        step = 1 if i < f else -1
        j, S = i, 0.0
        while j != f:
            S = max(0.0, S + llr[j])
            if S >= h:
                return j
            j += step
        return f

    def sprt(i, nmax):
        j, n, S = i, 0, 0.0
        while j < nmax:
            S += dt_ms[j]
            n += 1
            if S <= n * hC - hA:
                S, n = 0.0, 0
            elif S > n * hC - hB:
                return j
            j += 1
        return nmax

    out = []
    kl = cusum(1, N)
    while kl < N:
        krp = sprt(kl, N)
        if krp >= N:
            break
        kl1 = cusum(krp, N)
        if kl1 >= N:
            break
        kr = cusum(kl1 - nd, kl) if kl1 > nd else cusum(0, kl)
        if kr >= kl and kr - kl + 1 >= min_photons:
            out.append((kl, kr))
        kl = kl1
    return np.asarray(out, dtype=np.int64).reshape(-1, 2)


class TestCusumSprtAgainstZhangYang(unittest.TestCase):

    def test_numpy_transcription_of_the_paper(self):
        for seed in range(5):
            ticks, _ = bursty_stream(seed, two_channels=False)
            tttr = make_tttr(ticks)
            dt_ms = np.diff(ticks, prepend=ticks[0]).astype(float) * RES * 1e3
            for IB_cps, sb, alpha, beta, L in [(3000.0, 5.0, 0.05, 0.05, 20), (3000.0, 8.0, 0.01, 0.1, 10),
                                               (2500.0, 4.0, 0.05, 0.05, 30)]:
                with self.subTest(seed=seed, sb=sb, alpha=alpha):
                    got = pairs(tttr.burst_search_cusum_sprt(L, IB_cps, sb, alpha, beta))
                    ref = cusum_sprt_reference(dt_ms, IB_cps / 1e3, sb, alpha, beta, L)
                    np.testing.assert_array_equal(got, ref)
                    self.assertGreater(len(got), 0)

    @unittest.skipUnless(HAVE_PAM, "Octave and ../chisurf/junk/PAM/PAM.m needed")
    def test_behaviourally_against_pam_in_octave(self):
        """PAM (Schrimpf et al. 2018) discretises the same Zhang & Yang search
        (geometric inter-photon pmf, alpha = 1/N, offset heuristics), so edges
        can differ by a few photons; every PAM burst must be matched by one of
        ours with Jaccard >= 0.85 in photons, and vice versa."""
        src = open(PAM_M, encoding="utf-8", errors="replace").read().splitlines()
        i0 = next(i for i, l in enumerate(src) if l.startswith("function [START,STOP] = CUSUM_burstsearch"))
        i1 = next(i for i in range(i0 + 1, len(src)) if src[i].startswith("function BurstSearch_Preview"))
        body = [l for l in src[i0 + 1:i1] if not l.startswith("global FileInfo")]
        body = [l.replace("FileInfo.ClockPeriod", "ClockPeriod") for l in body]
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "pam_cusum_ref.m"), "w") as f:
                f.write("function [START,STOP] = pam_cusum_ref(Photons,IB,IT,ClockPeriod)\n")
                f.write("\n".join(body) + "\n")
            for seed in range(3):
                ticks, _ = bursty_stream(seed, two_channels=False)
                np.savetxt(os.path.join(d, "photons.txt"), ticks, fmt="%d")
                IB_khz, IT_khz = 3.0, 15.0
                cmd = (f"Photons=load('photons.txt'); [S,E]=pam_cusum_ref(Photons,{IB_khz},{IT_khz},{RES}); "
                       f"dlmwrite('out.txt',[S E],' ');")
                subprocess.run([OCTAVE, "--no-gui", "-q", "--eval", cmd], cwd=d, check=True,
                               capture_output=True, timeout=600)
                pam = np.loadtxt(os.path.join(d, "out.txt"), dtype=np.int64, ndmin=2) - 1  # 1-based
                tttr = make_tttr(ticks)
                got = pairs(tttr.burst_search_cusum_sprt(20, IB_khz * 1e3, IT_khz / IB_khz, 0.05, 0.05))
                pam = pam[(pam[:, 1] - pam[:, 0] + 1) >= 20]

                def best_overlap(b, others):
                    s, e = b
                    ov = [(min(e, e2) - max(s, s2) + 1) / (max(e, e2) - min(s, s2) + 1)
                          for s2, e2 in others if min(e, e2) >= max(s, s2)]
                    return max(ov) if ov else 0.0

                with self.subTest(seed=seed):
                    self.assertGreater(len(pam), 0)
                    for b in pam:
                        self.assertGreaterEqual(best_overlap(b, got), 0.85, (b.tolist(), got.tolist()))
                    for b in got:
                        self.assertGreaterEqual(best_overlap(b, pam), 0.85, (b.tolist(), pam.tolist()))


# ---------------------------------------------------------------------------
# Kalman -- ChiSurf's detector
# ---------------------------------------------------------------------------

def _bin_like_the_kernel(ticks, channels, dt, per_channel):
    """The kernel's binning: bins of dt from the first photon, per used channel."""
    t0 = ticks[0]
    ticks_per_bin = dt / RES
    span = (ticks[-1] - t0) * RES
    n_bins = int(math.ceil(span / dt)) + 1
    b = ((ticks - t0).astype(float) / ticks_per_bin).astype(np.int64)
    b = np.clip(b, 0, n_bins - 1)
    used = np.unique(channels) if per_channel else np.array([0])
    dim = used.size
    counts = np.zeros((n_bins, dim))
    for d, c in enumerate(used):
        sel = (channels == c) if per_channel else np.ones_like(channels, bool)
        counts[:, d] = np.bincount(b[sel], minlength=n_bins)
    return b, counts


def _bins_to_bursts(b, runs, n_photons, L):
    """Inclusive bin runs -> inclusive photon index pairs, min L photons."""
    out = []
    for s, e in runs:
        first = int(np.searchsorted(b, s, side="left"))
        last = int(np.searchsorted(b, e + 1, side="left")) - 1
        if last < first or last - first + 1 < L:
            continue
        out.append((first, last))
    return np.asarray(out, dtype=np.int64).reshape(-1, 2)


@unittest.skipUnless(HAVE_CHISURF, "ChiSurf (installed or ../chisurf) needed for the Kalman detector")
class TestKalmanBurstSearchWarmUp(unittest.TestCase):
    """`warmup_bins > 0` seeds the filter from the first bins and reports no burst
    inside them: the legacy zero start (x0 = 0, so R ~ 0 on the first update)
    flags one or two spurious bursts at t ~ 0 on every trace; the warm-up keeps
    exactly the injected bursts. Default 0 stays ChiSurf-identical (the class
    below pins that)."""

    def test_warm_up_drops_the_spurious_start_and_keeps_the_injected_bursts(self):
        ticks, ch = bursty_stream(0)
        tttr = make_tttr(ticks, ch)
        legacy = np.asarray(tttr.burst_search_kalman(20, 1e-4, 100.0, 0.1, 3.0, 2, 5, True)).reshape(-1, 2)
        warm = np.asarray(tttr.burst_search_kalman(20, 1e-4, 100.0, 0.1, 3.0, 2, 5, True, 100)).reshape(-1, 2)
        t = np.asarray(tttr.macro_times) * RES
        self.assertLess(t[legacy[0, 0]], 0.01)                # the legacy artefact at t ~ 0
        starts = t[warm[:, 0]]
        self.assertTrue(np.all(starts > 0.01))
        for centre in (0.2, 0.5, 0.8):                        # the injected bursts survive
            self.assertTrue(np.any(np.abs(starts - centre) < 0.01), starts)
        # and the legacy result is the warm-up result plus the artefact
        np.testing.assert_array_equal(legacy[1:], warm)


class TestKalmanBurstSearchAgainstChisurfDetector(unittest.TestCase):
    """``burst_search_kalman`` vs ChiSurf's ``KalmanBurstDetector.detect`` on
    the same bins -- an independent implementation (numpy filter, run
    extraction, gap merging); only the binning is shared by construction."""

    def _case(self, seed, per_channel, dt=1e-4, q=100.0, r_scale=0.1, z=3.0, min_len=2, gap=5, L=20):
        ticks, ch = bursty_stream(seed, two_channels=per_channel)
        tttr = make_tttr(ticks, ch)
        got = pairs(tttr.burst_search_kalman(L=L, dt=dt, q=q, r_scale=r_scale, z_thresh=z,
                                             min_len=min_len, merge_gap=gap, per_channel=per_channel))
        b, counts = _bin_like_the_kernel(ticks, ch, dt, per_channel)
        det = _CHISURF_KALMAN.KalmanBurstDetector(dim=counts.shape[1], dt=dt, q=q, r_scale=r_scale,
                                                  z_thresh=z, min_len=min_len, merge_gap=gap)
        res = det.detect(counts)
        ref = _bins_to_bursts(b, [(bb.start, bb.end) for bb in res.bursts], ticks.size, L)
        return got, ref

    def test_single_channel(self):
        for seed in range(4):
            with self.subTest(seed=seed):
                got, ref = self._case(seed, per_channel=False)
                np.testing.assert_array_equal(got, ref)
                self.assertGreater(len(got), 0)

    def test_two_channels(self):
        for seed in range(4):
            with self.subTest(seed=seed):
                got, ref = self._case(seed, per_channel=True)
                np.testing.assert_array_equal(got, ref)
                self.assertGreater(len(got), 0)

    def test_other_thresholds(self):
        for z, gap, min_len in [(2.5, 0, 1), (4.0, 10, 3)]:
            got, ref = self._case(1, per_channel=True, z=z, gap=gap, min_len=min_len)
            np.testing.assert_array_equal(got, ref)


# ---------------------------------------------------------------------------
# BOCPD -- Adams & MacKay
# ---------------------------------------------------------------------------

def bocpd_reference(counts, prior_count, prior_duration, hazard, max_run):
    """Adams & MacKay 2007 run-length recursion with a per-channel Gamma-Poisson
    model and the plug-in Poisson predictive at the posterior mean, as in the
    original ChiSurf implementation (numba, chisurf fffe299c3); returns the bins
    where the MAP run length is zero."""
    T, dim = counts.shape
    R = min(max_run, T)
    log_h, log_1h = math.log(hazard), math.log1p(-hazard)
    log_R_prev = np.full(R + 1, -np.inf)
    log_R_prev[0] = 0.0
    alpha = np.full((R + 1, dim), prior_count)
    beta = np.full((R + 1, dim), prior_duration)
    cps = []
    for t in range(T):
        Rmax = min(t + 1, R)
        k = counts[t]
        lam = alpha[:Rmax] / beta[:Rmax]
        pred = (k * np.log(lam) - lam).sum(axis=1) - gammaln(k + 1).sum()
        tmp = log_R_prev[:Rmax] + pred
        m = tmp.max()
        log_cp = m + log_h + math.log(np.exp(tmp - m).sum())
        log_R = np.full(R + 1, -np.inf)
        log_R[0] = log_cp
        log_R[1:Rmax + 1] = tmp + log_1h
        mm = log_R[:Rmax + 1].max()
        log_R[:Rmax + 1] -= mm + math.log(np.exp(log_R[:Rmax + 1] - mm).sum())
        new_alpha, new_beta = alpha.copy(), beta.copy()
        new_alpha[0] = prior_count + k
        new_beta[0] = prior_duration + 1.0
        new_alpha[1:Rmax + 1] = alpha[:Rmax] + k
        new_beta[1:Rmax + 1] = beta[:Rmax] + 1.0
        alpha, beta = new_alpha, new_beta
        if int(np.argmax(log_R[:Rmax + 1])) == 0:
            cps.append(t)
        log_R_prev = log_R
    return cps


@unittest.skipUnless(HAVE_SCIPY, "scipy needed for gammaln")
class TestBocpdAgainstAdamsMacKay(unittest.TestCase):

    def _case(self, seed, per_channel, dt=1e-3, prior_count=1.0, prior_duration=1.0, hazard=0.1, max_run=256, L=20):
        ticks, ch = bursty_stream(seed, two_channels=per_channel)
        tttr = make_tttr(ticks, ch)
        got = pairs(tttr.burst_search_bocpd(L=L, dt=dt, prior_count=prior_count, prior_duration=prior_duration,
                                            changepoint_prob=hazard, max_run=max_run, per_channel=per_channel))
        b, counts = _bin_like_the_kernel(ticks, ch, dt, per_channel)
        cps = bocpd_reference(counts, prior_count, prior_duration, hazard, max_run)
        edges = [0] + cps + [counts.shape[0]]
        runs = [(s, e - 1) for s, e in zip(edges[:-1], edges[1:]) if e > s]
        ref = _bins_to_bursts(b, runs, ticks.size, L)
        return got, ref

    def test_single_channel(self):
        for seed in range(3):
            with self.subTest(seed=seed):
                got, ref = self._case(seed, per_channel=False)
                np.testing.assert_array_equal(got, ref)
                self.assertGreater(len(got), 0)

    def test_two_channels(self):
        for seed in range(3):
            with self.subTest(seed=seed):
                got, ref = self._case(seed, per_channel=True)
                np.testing.assert_array_equal(got, ref)

    def test_other_priors_and_a_short_run_cap(self):
        for kw in [dict(prior_count=0.5, prior_duration=2.0, hazard=0.02), dict(max_run=16, hazard=0.3), dict(dt=2e-4)]:
            with self.subTest(**kw):
                got, ref = self._case(2, per_channel=True, **kw)
                np.testing.assert_array_equal(got, ref)


# ---------------------------------------------------------------------------
# Bayesian Blocks -- astropy (recorded), through the C++ harness
# ---------------------------------------------------------------------------

_HARNESS = {}


def _burst_harness():
    """Compile test/cpp/ab_burst_harness.cpp against the installed
    libtttrlib_burst once per session; None when that is not possible."""
    if "path" in _HARNESS:
        return _HARNESS["path"]
    _HARNESS["path"] = None
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    pkg = os.path.dirname(tttrlib.__file__)
    libs = [f for f in os.listdir(pkg) if f.startswith("libtttrlib_burst")]
    if cxx is None or not libs:
        return None
    src = os.path.join(ROOT, "test", "cpp", "ab_burst_harness.cpp")
    out = os.path.join(tempfile.mkdtemp(prefix="ab_burst_"), "ab_burst")
    inc = [os.path.join(ROOT, "modules", *p, "include") for p in
           (("spectroscopy", "burst"), ("core",), ("util",), ("math",), ("io", "base"))]
    cmd = [cxx, "-std=c++17", "-O2", *sum([["-I", i] for i in inc], []), src,
           "-L", pkg, "-ltttrlib_burst", f"-Wl,-rpath,{pkg}",
           f"-Wl,-rpath,{os.path.join(sys.prefix, 'lib')}", "-o", out]
    if subprocess.run(cmd, capture_output=True).returncode != 0:
        return None
    probe = subprocess.run([out, "ncp"], input="0.05 100\n", capture_output=True, text=True)
    if probe.returncode != 0:
        return None
    _HARNESS["path"] = out
    return out


def _harness_run(cmd, text):
    r = subprocess.run([_burst_harness(), cmd], input=text, capture_output=True, text=True, check=True)
    return r.stdout.split()


class TestBayesianBlocksAgainstAstropy(unittest.TestCase):
    """``bayesian_blocks_events`` (the DP every burst region runs) vs astropy's
    ``bayesian_blocks(fitness='events')`` recorded on seven photon sets, and
    the p0 -> ncp_prior calibration (Scargle 2013 eq. 21)."""

    def setUp(self):
        if _burst_harness() is None:
            self.skipTest("no C++ compiler or libtttrlib_burst to build the harness")
        if not os.path.exists(BB_FIXTURE):
            self.skipTest("fixture missing: run gen_ab_bayesian_blocks_astropy_reference.py")
        self.fx = np.load(BB_FIXTURE)

    def test_change_points_are_astropys(self):
        tick = float(self.fx["tick"])
        for i in range(int(self.fx["n_cases"])):
            ticks = self.fx[f"ticks_{i}"]
            ncp = float(self.fx[f"ncp_prior_{i}"])
            ref = self.fx[f"change_points_{i}"]
            with self.subTest(case=i, n=ticks.size, p0=float(self.fx[f"p0_{i}"])):
                text = f"{tick!r} {ncp!r} {ticks.size} " + " ".join(map(str, ticks.tolist()))
                cp = np.array(_harness_run("bb", text), dtype=np.int64)
                self.assertEqual(cp[0], 0)
                self.assertEqual(cp[-1], ticks.size)
                np.testing.assert_array_equal(cp[1:-1], ref)

    def test_ncp_prior_is_scargle_eq_21(self):
        for p0, n in [(0.05, 100), (0.005, 4096), (0.001, 37), (0.5, 10)]:
            got = float(_harness_run("ncp", f"{p0!r} {n}\n")[0])
            self.assertAlmostEqual(got, 4.0 - np.log(73.53 * p0 * n ** -0.478), places=12)

    def test_full_search_finds_the_injected_bursts_and_only_those(self):
        # end-to-end (trigger + DP + promotion) has no external twin: known answer
        for seed in range(3):
            ticks, _ = bursty_stream(seed, two_channels=False)
            tttr = make_tttr(ticks)
            got = pairs(tttr.burst_search_bayesian_blocks(L=20))
            centres = (ticks[got[:, 0]] + ticks[got[:, 1]]) / 2 * RES
            for t0, length, _ in ((0.2, 0.003, 0), (0.5, 0.002, 0), (0.8, 0.004, 0)):
                self.assertTrue(np.any((centres > t0 - 0.001) & (centres < t0 + length + 0.001)), (seed, t0))
            self.assertLessEqual(len(got), 6, got)


# ---------------------------------------------------------------------------
# significance statistics
# ---------------------------------------------------------------------------

def li_ma_eq17(n_on, n_off, alpha):
    """Li & Ma, ApJ 272, 317 (1983), eq. 17, transcribed."""
    n_on, n_off = float(n_on), float(n_off)
    tot = n_on + n_off
    a = n_on * math.log((1 + alpha) / alpha * n_on / tot) if n_on > 0 else 0.0
    b = n_off * math.log((1 + alpha) * n_off / tot) if n_off > 0 else 0.0
    s = math.sqrt(2.0 * max(a + b, 0.0))
    return s if n_on - alpha * n_off >= 0 else -s


@unittest.skipUnless(HAVE_SCIPY, "scipy needed")
class TestSignificanceAgainstScipyAndLiMa(unittest.TestCase):

    def test_li_ma_matches_equation_17(self):
        rng = np.random.default_rng(0)
        for _ in range(300):
            n_on = int(rng.integers(0, 400))
            n_off = int(rng.integers(0, 4000))
            alpha = float(rng.uniform(0.01, 2.0))
            if n_on + n_off == 0:
                continue
            got = tttrlib.li_ma_significance(n_on, n_off, alpha)
            self.assertAlmostEqual(got, li_ma_eq17(n_on, n_off, alpha), places=10, msg=(n_on, n_off, alpha))

    def test_poisson_tail_and_sigma_match_scipy(self):
        # test_burst_significance.py pins these already; kept here so the
        # register row is self-contained
        for k, mu in [(5, 1.0), (30, 12.5), (200, 150.0), (3, 20.0), (1000, 800.0)]:
            self.assertAlmostEqual(tttrlib.log_poisson_upper_tail(k, mu),
                                   sp_stats.poisson.logsf(k - 1, mu), places=9)
        for p in [0.5, 1e-3, 1e-8, 1e-30, 1e-200]:
            self.assertAlmostEqual(tttrlib.log_p_to_sigma(math.log(p)), sp_stats.norm.isf(p), places=8)
        for k, mu in [(30, 12.5), (200, 150.0)]:
            self.assertAlmostEqual(tttrlib.poisson_significance(k, mu),
                                   sp_stats.norm.isf(sp_stats.poisson.sf(k - 1, mu)), places=8)

    def test_trials_and_false_alarm_rate_formulas(self):
        self.assertEqual(tttrlib.estimate_n_trials(tttrlib.TrialsModel_kIndependentWindows, 10000, 10, 777), 1000.0)
        self.assertEqual(tttrlib.estimate_n_trials(tttrlib.TrialsModel_kTestedComponents, 10000, 10, 777), 777.0)
        self.assertEqual(tttrlib.estimate_n_trials(tttrlib.TrialsModel_kIndependentWindows, 5, 10, 0), 1.0)
        for far, T, n in [(0.01, 60.0, 1e5), (1.0, 3600.0, 1e7), (1e-4, 10.0, 100.0)]:
            p = far * T / n
            self.assertAlmostEqual(tttrlib.sigma_for_false_alarm_rate(far, T, n), sp_stats.norm.isf(min(p, 1.0)), places=8)


class TestBurstConfidenceAgainstNumpy(unittest.TestCase):
    """``TTTR.burst_confidence`` transcribed: background from the photons within
    +-window/2 of the burst edges (the burst itself excluded), Li & Ma with
    alpha = t_burst / t_off; Poisson and Gaussian modes likewise."""

    def test_all_three_modes(self):
        ticks, _ = bursty_stream(0, two_channels=False)
        tttr = make_tttr(ticks)
        bursts = pairs(tttr.burst_search_sliding_window(20, 10, 5e-4))
        window = 0.05
        half = int(0.5 * window / RES)
        for mode, name in [(tttrlib.SignificanceMode_kLiMa, "lima"), (tttrlib.SignificanceMode_kPoisson, "poisson"),
                           (tttrlib.SignificanceMode_kGaussian, "gauss")]:
            got = np.asarray(tttr.burst_confidence(bursts.ravel().tolist(), window, int(mode)))
            for j, (s, e) in enumerate(bursts):
                k = e - s + 1
                dur = (ticks[e] - ticks[s]) * RES
                lo = int(np.searchsorted(ticks, ticks[s] - half, side="left"))
                hi = int(np.searchsorted(ticks, ticks[e] + half, side="right")) - 1
                n_off = (s - lo) + (hi - e)
                t_off = ((ticks[s] - ticks[lo]) + (ticks[hi] - ticks[e])) * RES
                mu = n_off / t_off * dur
                if name == "lima":
                    ref = li_ma_eq17(k, n_off, dur / t_off)
                elif name == "poisson":
                    ref = tttrlib.poisson_significance(k, mu)
                else:
                    ref = (k - mu) / math.sqrt(mu)
                with self.subTest(mode=name, burst=j):
                    self.assertAlmostEqual(got[j], ref, places=9)


# ---------------------------------------------------------------------------
# core selection primitives
# ---------------------------------------------------------------------------

class TestSelectionPrimitivesAgainstNumpy(unittest.TestCase):

    def setUp(self):
        self.ticks, self.ch = bursty_stream(5)
        self.tttr = make_tttr(self.ticks, self.ch)

    def test_intensity_trace_is_a_bincount(self):
        for tw in [1e-3, 2.5e-4, 0.05]:
            got = np.asarray(self.tttr.get_intensity_trace(tw))
            clocks = int(math.floor(tw / RES))
            ref = np.bincount(self.ticks // clocks, minlength=int(self.ticks[-1] // clocks) + 1)
            np.testing.assert_array_equal(got, ref)
            self.assertEqual(got.sum(), self.ticks.size)

    def test_ranges_by_time_window(self):
        for tw_min, tw_max, nmin, nmax, inv in [(1e-3, -1, -1, -1, False), (2e-3, 5e-3, -1, -1, False),
                                                 (1e-3, -1, 10, -1, False), (1e-3, -1, 3, 8, True)]:
            got = np.asarray(self.tttr.get_ranges_by_time_window(
                tw_min, tw_max, nmin, nmax, invert=inv)).reshape(-1, 2)
            # greedy windows: from `begin`, the first photon >= tw_min later
            # closes the window; select by span < tw_max and photon count
            tmin = int(tw_min / RES)
            tmax = int(tw_max / RES) if tw_max > 0 else None
            ref, b, n = [], 0, self.ticks.size
            while b < n:
                e = int(np.searchsorted(self.ticks, self.ticks[b] + tmin, side="left"))
                e = max(e, b + 1)
                dt = self.ticks[min(e, n - 1)] - self.ticks[b] if e < n else self.ticks[n - 1] - self.ticks[b]
                if e >= n:
                    dt = self.ticks[n - 1] - self.ticks[b]  # loop ran off the end
                nph = e - b
                ok = (tmax is None or dt < tmax) and (nmin < 0 or nph >= nmin) and (nmax < 0 or nph <= nmax)
                if ok != inv:
                    ref.append((b, e))
                b = e
            np.testing.assert_array_equal(got, np.asarray(ref, dtype=np.int64).reshape(-1, 2))

    def test_selection_by_count_rate(self):
        for tw, nmax, inv in [(1e-3, 5, False), (1e-3, 5, True), (5e-4, 3, False)]:
            got = np.asarray(self.tttr.get_selection_by_count_rate(tw, nmax, inv))
            w = int(tw / RES)
            ref, i, n = [], 0, self.ticks.size
            while i < n:
                r = int(np.searchsorted(self.ticks, self.ticks[i] + w, side="left"))
                r = max(r, i + 1)
                nph = r - i
                if (nph >= nmax) if inv else (nph < nmax):
                    ref.extend(range(i, r))
                i = r
            np.testing.assert_array_equal(got, np.asarray(ref, dtype=np.int64))


# ---------------------------------------------------------------------------
# BurstFilter properties and feature extraction
# ---------------------------------------------------------------------------

class TestBurstFilterAndFeaturesAgainstNumpy(unittest.TestCase):

    def test_properties_and_channel_counts(self):
        ticks, ch = bursty_stream(7)
        tttr = make_tttr(ticks, ch)
        bf = tttrlib.BurstFilter(tttr)
        bf.set_burst_parameters(20, 10, 5e-4)
        bursts = pairs(bf.find_bursts())
        ref = pairs(tttr.burst_search_sliding_window(20, 10, 5e-4))
        np.testing.assert_array_equal(bursts, ref)
        props = np.asarray(bf.get_all_burst_properties())
        size = bursts[:, 1] - bursts[:, 0] + 1
        dur = (ticks[bursts[:, 1]] - ticks[bursts[:, 0]]) * RES
        np.testing.assert_array_equal(props[:, 0], bursts[:, 0])
        np.testing.assert_array_equal(props[:, 1], bursts[:, 1])
        np.testing.assert_array_equal(props[:, 2], size)
        np.testing.assert_allclose(props[:, 3], dur, rtol=1e-12)
        np.testing.assert_allclose(props[:, 4], size / dur, rtol=1e-12)
        # named detector channels -> per-burst photon counts and E = A/(D+A)
        donor, acc = tttrlib.Channel("donor"), tttrlib.Channel("acceptor")
        donor.add_component(0, 0, 65535)
        acc.add_component(1, 0, 65535)
        bf.add_channel(donor)
        bf.add_channel(acc)
        bfe = tttrlib.BurstFeatureExtractor(bf)
        counts = bfe.channel_photons()
        nd = np.array([np.sum(ch[s:e + 1] == 0) for s, e in bursts])
        na = np.array([np.sum(ch[s:e + 1] == 1) for s, e in bursts])
        np.testing.assert_array_equal(np.asarray(counts["donor"]), nd)
        np.testing.assert_array_equal(np.asarray(counts["acceptor"]), na)
        np.testing.assert_allclose(np.asarray(bfe.fret_efficiencies), na / (nd + na), rtol=1e-12)
        # filters are the obvious set operations on the index pairs
        bf.filter_by_size(30, 100000)
        np.testing.assert_array_equal(pairs(bf.get_bursts()), bursts[size >= 30])
        bf.reset_to_raw_bursts()
        bf.filter_by_duration(1e-3, 1.0)
        np.testing.assert_array_equal(pairs(bf.get_bursts()), bursts[dur >= 1e-3])

    @unittest.skipUnless(HAVE_FRETBURSTS, "FRETBursts virtualenv not built")
    def test_size_and_width_are_fretbursts_burst_stats(self):
        ticks, ch = bursty_stream(8, two_channels=False)
        L, m, T = 20, 10, 5000
        ref = _run_fretbursts([(ticks, L, m, T, None, None)])["bursts_c_0"]  # istart, istop, start, stop
        tttr = make_tttr(ticks)
        bf = tttrlib.BurstFilter(tttr)
        bf.set_burst_parameters(L, m, (T + 0.5) * RES)
        bf.find_bursts()
        props = np.asarray(bf.get_all_burst_properties())
        np.testing.assert_array_equal(props[:, 2], ref[:, 1] - ref[:, 0] + 1)      # size
        np.testing.assert_allclose(props[:, 3], (ref[:, 3] - ref[:, 2]) * RES, rtol=1e-12)  # width


# ---------------------------------------------------------------------------
# streaming detector == batch
# ---------------------------------------------------------------------------

class TestStreamingBurstDetectorMatchesBatch(unittest.TestCase):
    """test/python/streaming/test_streaming_burst_detector.py pins the
    equivalence in detail; one end-to-end row here on the reference streams."""

    def test_same_bursts_as_the_sliding_window(self):
        for seed in range(3):
            ticks, _ = bursty_stream(seed, two_channels=False)
            L, m, T = 20, 10, 5000
            ref = sliding_window_reference(ticks, L, m, T)
            det = tttrlib.StreamingBurstDetector(m, (T + 0.5) * RES, RES)
            det.set_min_photons(L)
            for v in ticks:
                det.push_photon(int(v))
            det.flush()
            got = pairs(det.get_burst_indices())
            np.testing.assert_array_equal(got, ref)


# ---------------------------------------------------------------------------
# BurstML -- the original FRET_burstML MEX likelihood, built natively
# ---------------------------------------------------------------------------

_MEX = {}


def _find_gsl():
    for prefix in (sys.prefix, "/opt/homebrew", "/usr/local", "/usr"):
        if os.path.exists(os.path.join(prefix, "include", "gsl", "gsl_blas.h")):
            return prefix
    return None


def _burstml_mex_binary():
    """Unzip junk/FRET_burstML/burstMLProject.zip and build mlhDiffNTRbkg_MT.cpp
    with the shims in test/cpp/burstml_mex_shim; None when that is not possible."""
    if "path" in _MEX:
        return _MEX["path"]
    _MEX["path"] = None
    zip_path = os.path.join(ROOT, "junk", "FRET_burstML", "burstMLProject.zip")
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    gsl = _find_gsl()
    if not (os.path.exists(zip_path) and cxx and gsl):
        return None
    import zipfile
    d = tempfile.mkdtemp(prefix="burstml_mex_")
    with zipfile.ZipFile(zip_path) as z:
        z.extract("src/mlhDiffNTRbkg_MT.cpp", d)
        z.extract("src/mlhDiffNTRbkg_MT.h", d)
    hdr = os.path.join(d, "src", "mlhDiffNTRbkg_MT.h")
    with open(hdr, encoding="utf-8", errors="replace") as f:
        txt = f.read().replace("<gsl/gsl_eigen.h.>", "<gsl/gsl_eigen.h>")   # a typo MSVC forgave
    with open(hdr, "w") as f:
        f.write(txt)
    shim = os.path.join(ROOT, "test", "cpp", "burstml_mex_shim")
    out = os.path.join(d, "mex_ref")
    cmd = [cxx, "-std=c++17", "-O2", "-w", "-I", shim, "-I", os.path.join(gsl, "include"),
           os.path.join(d, "src", "mlhDiffNTRbkg_MT.cpp"),
           os.path.join(ROOT, "test", "cpp", "ab_burstml_mex_driver.cpp"),
           "-L", os.path.join(gsl, "lib"), "-lgsl", "-lgslcblas",
           f"-Wl,-rpath,{os.path.join(gsl, 'lib')}", "-o", out]
    if subprocess.run(cmd, capture_output=True).returncode != 0:
        return None
    _MEX["path"] = out
    return out


def _mex_nll(binary, param_sets, lb, ub, n_colours, jmax, qmax, t_th, n_th, t100, colours, offsets):
    n_b, n_ph = len(offsets) - 1, len(t100)
    lines = [f"{len(param_sets[0])} {len(param_sets)} {n_colours} {jmax} {qmax!r} {t_th!r} {n_th!r} {n_b} {n_ph}",
             " ".join(repr(float(v)) for p in param_sets for v in p),      # numP x numEval, column-major
             " ".join(map(repr, map(float, lb))) + " " + " ".join(map(repr, map(float, ub))),
             " ".join(map(str, offsets)), " ".join(str(i + 1) for i in range(n_b)),
             " ".join(map(str, t100)), " ".join(str(int(c) + 1) for c in colours)]
    r = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    return [float(x) for x in r.stdout.split()]


class TestBurstMLAgainstTheOriginalMex(unittest.TestCase):
    """``BurstML::neg_log_likelihood`` vs the FRET_burstML MEX it was ported
    from (`mlhDiffNTRbkg_MT.cpp`, Hoffmann et al.), compiled natively from the
    archive in junk/ with GSL. Same bursts, same parameters, same grid: the
    negative log-likelihood must agree to rounding. Photon times are quantised
    to 100 ns because that is the MEX's input unit."""

    def setUp(self):
        self.binary = _burstml_mex_binary()
        if self.binary is None:
            self.skipTest("needs junk/FRET_burstML/burstMLProject.zip, a C++ compiler and GSL")
        sys.path.insert(0, HERE)
        from test_burstml import simulate_bursts
        self.simulate_bursts = simulate_bursts

    def _data(self, seed, n_bursts=20):
        times, colours, offsets = self.simulate_bursts(n_bursts=n_bursts, min_photons=30, seed=seed)
        t100 = np.round(times * 1e4).astype(np.int64)
        return t100, (t100 * 1e-4), colours, offsets

    def _tttrlib(self, times_ms, colours, offsets, n_states, n_colours, jmax, params):
        ml = tttrlib.BurstML()
        ml.set_burst_data(times_ms.tolist(), [int(c) for c in colours], [int(o) for o in offsets])
        ml.set_n_states(n_states)
        ml.set_n_colours(n_colours)
        ml.set_jmax(jmax)
        ml.set_qmax(4.0)
        ml.set_t_th(0.3)
        ml.set_n_th(30.0)
        return [ml.neg_log_likelihood(list(p)) for p in params]

    def test_two_colours_two_states(self):
        params = [[150.0, 300.0, 0.8, 2.5, 0.8, 0.5, 0.85, 0.45, 2.5, 0.8],
                  [200.0, 350.0, 1.0, 3.0, 0.8, 0.5, 0.8, 0.5, 3.0, 1.0],
                  [120.0, 280.0, 0.5, 2.0, 1.5, 0.3, 0.9, 0.4, 1.0, 0.5],
                  [60.0, 550.0, 0.2, 9.0, 8.0, 0.75, 0.72, 0.58, 4.5, 2.5]]
        lb = [50, 100, 0.1, 0.3, 0.1, 0.2, 0.7, 0.3, 0.5, 0.5]
        ub = [500, 600, 3.0, 10.0, 10.0, 0.8, 0.95, 0.6, 5.0, 3.0]
        for seed, jmax in [(1, 15), (7, 20)]:
            t100, times_ms, colours, offsets = self._data(seed)
            ours = self._tttrlib(times_ms, colours, offsets, 2, 2, jmax, params)
            ref = _mex_nll(self.binary, params, lb, ub, 2, jmax, 4.0, 0.3, 30.0, t100.tolist(), colours, offsets.tolist())
            for p, o, r in zip(params, ours, ref):
                with self.subTest(seed=seed, jmax=jmax, params=p):
                    self.assertAlmostEqual(o / r, 1.0, delta=1e-12, msg=(o, r))

    def test_three_states(self):
        n = 3   # 5n params: n0, tau, k(n-1), f(n-1), E, bkg(2)
        params = [[100.0, 200.0, 300.0, 0.6, 1.5, 3.0, 0.5, 1.0, 0.4, 0.5, 0.9, 0.6, 0.3, 2.0, 1.0],
                  [150.0, 250.0, 350.0, 0.9, 2.0, 2.5, 1.5, 0.7, 0.3, 0.6, 0.8, 0.5, 0.2, 1.5, 0.7]]
        lb = [50, 50, 50, 0.1, 0.1, 0.1, 0.05, 0.05, 0.1, 0.1, 0.1, 0.1, 0.05, 0.1, 0.1]
        ub = [500, 500, 500, 5.0, 5.0, 5.0, 5.0, 5.0, 0.9, 0.9, 0.99, 0.99, 0.9, 5.0, 5.0]
        t100, times_ms, colours, offsets = self._data(3)
        ours = self._tttrlib(times_ms, colours, offsets, n, 2, 12, params)
        ref = _mex_nll(self.binary, params, lb, ub, 2, 12, 4.0, 0.3, 30.0, t100.tolist(), colours, offsets.tolist())
        for p, o, r in zip(params, ours, ref):
            with self.subTest(params=p):
                self.assertAlmostEqual(o / r, 1.0, delta=1e-12, msg=(o, r))

    def test_three_colours(self):
        # (3 + numC) n + numC - 2 = 11 params for n = 2, numC = 3:
        # n0(2) tau(2) k(1) f(1) E[colour 0](2) E[colour 1](2) bkg(3)
        params = [[150.0, 300.0, 0.8, 2.5, 0.8, 0.5, 0.5, 0.3, 0.3, 0.4, 2.0, 0.8, 0.5],
                  [200.0, 250.0, 1.0, 2.0, 1.2, 0.4, 0.4, 0.2, 0.2, 0.5, 1.0, 1.0, 1.0]]
        lb = [50, 100, 0.1, 0.3, 0.1, 0.2, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1]
        ub = [500, 600, 3.0, 10.0, 10.0, 0.8, 0.9, 0.9, 0.9, 0.9, 5.0, 5.0, 5.0]
        t100, times_ms, colours, offsets = self._data(5)
        rng = np.random.default_rng(0)
        colours = colours.copy()
        colours[rng.random(colours.size) < 0.25] = 2      # a third detection colour
        ours = self._tttrlib(times_ms, colours, offsets, 2, 3, 12, params)
        ref = _mex_nll(self.binary, params, lb, ub, 3, 12, 4.0, 0.3, 30.0, t100.tolist(), colours, offsets.tolist())
        for p, o, r in zip(params, ours, ref):
            with self.subTest(params=p):
                self.assertAlmostEqual(o / r, 1.0, delta=1e-12, msg=(o, r))


if __name__ == "__main__":
    unittest.main()
