# SPDX-License-Identifier: BSD-3-Clause
"""A/B of the `modules/core` algorithms against independent references.

Every kernel here is compared with something that was not derived from it:
numpy's histogram family and bincount, plain-numpy transcriptions of the
documented sliding-window / time-window definitions, hashlib for SHA-256,
Python integer arithmetic for the bit and byte-order helpers, `ptufile`
(Christoph Gohlke) for PicoQuant T3 decoding and `phconvert` (Ingargiola et
al., junk/phconvert) for Becker & Hickl SPC-130 and PicoQuant HT3 decoding.

What is already pinned elsewhere and NOT repeated: `HistogramNd` against
boost-histogram for every axis kind, storage mode and flow bin
(test_histogram_nd.py); the numpy-shaped `histogram / histogram2d /
histogramdd` API against numpy (test_histogram_numpy_api.py); the 2-D legacy
functions bin for bin against `np.histogram2d` (test_histogram2d.py); T2 PTU
decoding against ptufile (tttr/test_t2_ptufile_reference.py).

Register: okf/testing/algorithm-validation.md
"""
import hashlib
import importlib
import importlib.util
import os
import shutil
import subprocess
import sys
import types
import unittest

import numpy as np

import tttrlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
sys.path.insert(0, _HERE)
from test_settings import settings, DATA_AVAILABLE  # noqa: E402

try:
    import ptufile
    HAVE_PTUFILE = True
except ImportError:
    HAVE_PTUFILE = False


def _rng(seed=0):
    return np.random.default_rng(seed)


# --------------------------------------------------------------------------
# legacy 1-D histogram functions vs numpy
# --------------------------------------------------------------------------

def _np_hist_lin(data, edges, weights=None):
    """numpy equivalent of tttrlib's `n_bins` edges: bin i starts at edges[i]
    and the last bin is one width wide, so numpy needs one more edge."""
    w = edges[1] - edges[0]
    full = np.concatenate([edges, [edges[-1] + w]])
    h, _ = np.histogram(data, bins=full, weights=weights)
    # numpy closes the last bin on the right; tttrlib does not (floor map)
    top = full[-1]
    if weights is None:
        h[-1] -= np.count_nonzero(data == top)
    else:
        h[-1] -= weights[data == top].sum()
    return h.astype(float)


def _np_hist_log(data, edges, weights=None):
    """The same in log space: geometric edges, one extra edge appended."""
    le = np.log(edges)
    w = le[1] - le[0]
    full = np.exp(np.concatenate([le, [le[-1] + w]]))
    ok = data > 0
    h, _ = np.histogram(data[ok], bins=full,
                        weights=None if weights is None else weights[ok])
    return h.astype(float)


class TestLegacyHistogram1DAgainstNumpy(unittest.TestCase):

    def test_lin_double_matches_numpy(self):
        rng = _rng(1)
        for n_bins in (2, 7, 64, 513):
            edges = np.linspace(-3.0, 5.0, n_bins)
            data = rng.normal(1.0, 3.0, 20000)
            weights = rng.uniform(0.5, 2.0, data.size)
            for use_w in (False, True):
                with self.subTest(n_bins=n_bins, weights=use_w):
                    hist = np.zeros(n_bins)
                    tttrlib.histogram1D_double(data, weights, edges, hist,
                                               "lin", use_w)
                    ref = _np_hist_lin(data, edges, weights if use_w else None)
                    np.testing.assert_allclose(hist, ref, rtol=0, atol=1e-9)
            # Values sitting exactly on an edge: tttrlib maps them through
            # floor((v - lo) / w) in floating point, numpy through an exact
            # searchsorted on the edge array. Both put such a value in the bin
            # starting at that edge except where the affine map rounds a hair
            # below -- then it lands one bin lower. Total conserved, per-bin
            # within the number of edge hits.
            with self.subTest(n_bins=n_bins, on_edges=True):
                w = edges[1] - edges[0]
                d = np.concatenate([edges, [edges[-1] + w]])
                hist = np.zeros(n_bins)
                tttrlib.histogram1D_double(d, np.ones(d.size), edges, hist, "lin", False)
                ref = _np_hist_lin(d, edges)
                self.assertLessEqual(abs(hist.sum() - ref.sum()), 1)
                self.assertTrue(np.all(np.abs(hist - ref) <= 1))

    def test_lin_int_matches_numpy(self):
        rng = _rng(2)
        edges = np.arange(0, 40, 2, dtype=np.int32)
        data = rng.integers(-5, 50, 5000).astype(np.int32)
        hist = np.zeros(edges.size)
        tttrlib.histogram1D_int(data, np.ones(data.size), edges, hist, "lin", False)
        ref = _np_hist_lin(data.astype(float), edges.astype(float))
        np.testing.assert_array_equal(hist, ref)

    def test_log10_matches_numpy_in_log_space(self):
        rng = _rng(3)
        for n_bins in (5, 32, 200):
            edges = np.geomspace(0.1, 1000.0, n_bins)
            data = np.exp(rng.uniform(np.log(0.01), np.log(5000.0), 30000))
            data = np.concatenate([data, [-1.0, 0.0]])  # must be dropped
            hist = np.zeros(n_bins)
            tttrlib.histogram1D_double(data, np.ones(data.size), edges, hist,
                                       "log10", False)
            ref = _np_hist_log(data, edges)
            # a value on an edge can round either way in log space: allow the
            # per-bin count to differ by the number of exact-edge hits (0 here)
            self.assertEqual(hist.sum(), ref.sum())
            np.testing.assert_allclose(hist, ref, atol=2)
            # away from edges the agreement is exact
            far = np.abs(np.log(data[data > 0])[:, None] - np.log(edges)[None, :]).min(1) > 1e-6
            d = data[data > 0][far]
            h2 = np.zeros(n_bins)
            tttrlib.histogram1D_double(d, np.ones(d.size), edges, h2, "log10", False)
            np.testing.assert_array_equal(h2, _np_hist_log(d, edges))

    def test_range_form_matches_numpy(self):
        rng = _rng(4)
        data = rng.uniform(-1, 11, 10000)
        for log in (False, True):
            lo, hi, n = (0.5, 20.0, 40) if log else (0.0, 10.0, 25)
            hist = np.zeros(n)
            tttrlib.histogram1D_range_double(np.abs(data) + 0.01 if log else data,
                                             np.ones(data.size), lo, hi, n, hist,
                                             log, False)
            edges = np.zeros(n)
            tttrlib.make_bin_edges_double(edges, lo, hi, log)
            ref = (_np_hist_log(np.abs(data) + 0.01, edges) if log
                   else _np_hist_lin(data, edges))
            np.testing.assert_allclose(hist, ref, atol=1)
            # and the edges are numpy's linspace / geomspace
            np.testing.assert_allclose(edges, np.geomspace(lo, hi, n) if log
                                       else np.linspace(lo, hi, n), rtol=1e-12)

    def _search_axis(self):
        rng = _rng(5)
        edges = np.array([0.0, 0.5, 2.0, 2.1, 5.0, 9.0])
        data = rng.uniform(-1, 10, 20000)
        hist = np.zeros(edges.size)
        tttrlib.histogram1D_double(data, np.ones(data.size), edges, hist,
                                   "search", False)
        ref, _ = np.histogram(data, bins=edges)
        return hist, ref

    def test_search_axis_inner_bins_match_numpy(self):
        """The 'search' axis (any axis_type other than lin/log10: arbitrary,
        non-uniform edges resolved by binary search) vs np.histogram with the
        same edges: the inner bins agree exactly."""
        hist, ref = self._search_axis()
        np.testing.assert_array_equal(hist[1:ref.size - 1], ref[1:-1])

    def test_search_axis_first_and_last_bins_match_numpy(self):
        """All bins of the search axis, including the first and the last
        (closed on the right, as numpy's is), equal np.histogram. This A/B
        found `bin_of` rejecting idx == 0 and `search_bin_idx` rejecting the
        last bin on 2026-08-17 -- the first and last bin were never filled;
        fixed the same day."""
        hist, ref = self._search_axis()
        np.testing.assert_array_equal(hist[:ref.size], ref)
        self.assertEqual(hist[ref.size:].sum(), 0)


# --------------------------------------------------------------------------
# TTTR time-window primitives vs numpy transcriptions of their definitions
# --------------------------------------------------------------------------

def _make_tttr(macro_times, micro_times=None, channels=None, macro_res=1e-7,
               micro_res=1e-10, n_micro_channels=1024):
    macro_times = np.asarray(macro_times, dtype=np.uint64)
    n = macro_times.size
    if micro_times is None:
        micro_times = np.zeros(n, dtype=np.uint16)
    if channels is None:
        channels = np.zeros(n, dtype=np.int8)
    t = tttrlib.TTTR()
    t.append_events(macro_times, np.asarray(micro_times, dtype=np.uint16),
                    np.asarray(channels, dtype=np.int8),
                    np.zeros(n, dtype=np.int8))
    h = t.header
    h.set_macro_time_resolution(macro_res)
    h.set_micro_time_resolution(micro_res)
    h.set_number_of_micro_time_channels(n_micro_channels)
    return t


def _ref_selection_by_count_rate(time, tw_ticks, n_ph_max, invert=False):
    sel = []
    i, n = 0, len(time)
    while i < n:
        r = i
        while r < n and time[r] - time[i] < tw_ticks:
            r += 1
        n_ph = r - i
        keep = (n_ph >= n_ph_max) if invert else (n_ph < n_ph_max)
        if keep:
            sel.extend(range(i, r))
        i = r
    return np.array(sel, dtype=np.int64)


def _ref_ranges_by_time_window(time, tw_min, tw_max=None, min_ph=-1, max_ph=-1,
                               invert=False):
    out = []
    n = len(time)
    b = 0
    while b < n:
        e = b + 1
        dt = 0
        while e < n:
            dt = int(time[e]) - int(time[b])
            if dt >= tw_min:
                break
            e += 1
        n_ph = e - b
        ok = ((tw_max is None or dt < tw_max)
              and (min_ph < 0 or n_ph >= min_ph)
              and (max_ph < 0 or n_ph <= max_ph))
        if invert:
            ok = not ok
        if ok:
            out.extend([b, e])
        b = e
    return np.array(out, dtype=np.int64)


class TestTimeWindowPrimitivesAgainstNumpy(unittest.TestCase):

    def setUp(self):
        rng = _rng(7)
        # bursty stream: Poisson background plus a few dense bursts, in ticks
        bg = np.cumsum(rng.exponential(200.0, 20000))
        bursts = np.concatenate([np.sort(rng.uniform(c, c + 3000, 300))
                                 for c in rng.uniform(0, bg[-1], 12)])
        self.mt = np.sort(np.concatenate([bg, bursts])).astype(np.uint64)
        self.res = 1e-6  # s per tick
        self.tttr = _make_tttr(self.mt, macro_res=self.res)

    def test_intensity_trace(self):
        # NOTE the Doxygen on TTTR::get_intensity_trace says the window is in
        # milliseconds; the arithmetic divides it by the header's macro-time
        # resolution, which is in SECONDS (ptufile agrees, see below), and every
        # example in examples/ passes seconds (0.001 for "1 ms bins"). Seconds
        # it is; the doc string is the odd one out.
        for tw_s in (1e-3, 2.5e-4, 5e-7):
            trace = np.asarray(self.tttr.get_intensity_trace(tw_s))
            cpb = max(1, int(np.floor(tw_s / self.res)))
            # documented: bin = t // clocks_per_bin, n_bins = t_max // cpb + 1
            ref = np.bincount((self.mt // cpb).astype(np.int64),
                              minlength=int(self.mt[-1] // cpb) + 1)
            np.testing.assert_array_equal(trace, ref)

    def test_selection_by_count_rate(self):
        # same seconds-not-milliseconds remark as for the intensity trace
        for tw_s, n_max, inv in ((1e-3, 5, False), (1e-3, 5, True),
                                 (5e-5, 20, False), (3e-3, 100, True)):
            with self.subTest(tw_s=tw_s, n_max=n_max, invert=inv):
                got = np.asarray(self.tttr.get_selection_by_count_rate(
                    tw_s, n_max, inv))
                tw_ticks = int(tw_s / self.res)
                ref = _ref_selection_by_count_rate(self.mt, tw_ticks, n_max, inv)
                np.testing.assert_array_equal(got, ref)

    def test_ranges_by_time_window(self):
        cal_ms = self.res * 1e3
        cases = [dict(minimum_window_length=1.0),
                 dict(minimum_window_length=1.0, maximum_window_length=5.0),
                 dict(minimum_window_length=0.5,
                      minimum_number_of_photons_in_time_window=20),
                 dict(minimum_window_length=0.5,
                      maximum_number_of_photons_in_time_window=8, invert=True)]
        for kw in cases:
            with self.subTest(**kw):
                got = np.asarray(self.tttr.get_ranges_by_time_window(
                    macro_time_calibration=cal_ms, **kw))
                tw_min = int(kw["minimum_window_length"] / cal_ms)
                tw_max = (int(kw["maximum_window_length"] / cal_ms)
                          if "maximum_window_length" in kw else None)
                ref = _ref_ranges_by_time_window(
                    self.mt, tw_min, tw_max,
                    kw.get("minimum_number_of_photons_in_time_window", -1),
                    kw.get("maximum_number_of_photons_in_time_window", -1),
                    kw.get("invert", False))
                np.testing.assert_array_equal(got, ref)

    def test_count_rate_and_mean_microtime(self):
        rng = _rng(8)
        mic = rng.integers(0, 1024, self.mt.size).astype(np.uint16)
        t = _make_tttr(self.mt, mic, macro_res=self.res, micro_res=1e-10)
        # count rate = N / (span in seconds)
        span = float(self.mt[-1] - self.mt[0]) * self.res
        self.assertAlmostEqual(t.get_count_rate(), self.mt.size / span, delta=1e-6 * self.mt.size / span)
        self.assertAlmostEqual(t.get_mean_microtime(), mic.mean() * 1e-10, places=18)


class TestMicrotimeHistogramAndMomentLifetime(unittest.TestCase):

    def test_microtime_histogram_is_bincount(self):
        rng = _rng(9)
        n_ch = 4096
        mic = rng.integers(0, n_ch, 50000).astype(np.uint16)
        ch = rng.integers(0, 3, mic.size).astype(np.int8)
        t = _make_tttr(np.arange(mic.size) * 10, mic, ch, n_micro_channels=n_ch)
        for coarsen in (1, 4, 16):
            hist, time = t.get_microtime_histogram(coarsen)
            hist = np.asarray(hist)
            ref = np.bincount((mic // coarsen).astype(np.int64),
                              minlength=n_ch // coarsen)
            np.testing.assert_array_equal(hist[:ref.size], ref)
            np.testing.assert_allclose(np.asarray(time),
                                       np.arange(hist.size) * coarsen * 1e-10)
        # channel filter
        hist, _ = t.get_microtime_histogram(1, [1])
        np.testing.assert_array_equal(np.asarray(hist)[:n_ch],
                                      np.bincount(mic[ch == 1].astype(np.int64), minlength=n_ch))

    def test_moment_lifetime_is_isenberg_first_moment(self):
        """Isenberg 1973: tau = <t>_decay - <t>_irf, on a simulated
        exponential decay convolved with a narrow IRF -- a known answer, and
        the exact moment formula transcribed in numpy."""
        rng = _rng(10)
        dt = 0.016  # ns per channel
        tau_ns = 3.7
        irf = np.clip(rng.normal(80, 4, 20000), 0, 4095).astype(np.uint16)
        dec = np.clip((rng.exponential(tau_ns / dt, 200000)
                       + rng.normal(80, 4, 200000)), 0, 4095).astype(np.uint16)
        t_irf = _make_tttr(np.arange(irf.size), irf, micro_res=dt * 1e-9)
        t_dec = _make_tttr(np.arange(dec.size), dec, micro_res=dt * 1e-9)
        got = t_dec.mean_lifetime(t_irf, dt=dt)
        ref = dec.mean() - irf.mean()
        self.assertAlmostEqual(got, ref * dt, places=9)
        self.assertAlmostEqual(got, tau_ns, delta=0.05)


class TestMicrotimeLinearizationAgainstNumpy(unittest.TestCase):

    def test_lut_without_dithering_is_a_rounded_lookup_plus_shift(self):
        rng = _rng(11)
        lut_size = 256
        n_ch = 3
        # a monotone but non-linear TAC characteristic per channel
        luts = np.stack([np.sort(rng.uniform(0, lut_size - 1, lut_size)).astype(np.float32)
                         for _ in range(n_ch)])
        shifts = np.array([0, 5, -7], dtype=np.int32)
        # set_mt_linearizer used to adopt the raw pointer while the Python
        # proxy kept its own -> double free at teardown (observed 2026-08-17);
        # it copies since. Both routes are exercised below.
        channel_luts = tttrlib.MapIntVectorFloat()
        channel_shifts = tttrlib.MapSignedCharInt()
        for c in range(n_ch):
            v = tttrlib.VectorFloat()
            for x in luts[c]:
                v.append(float(x))
            channel_luts[c] = v
            channel_shifts[c] = int(shifts[c])
        mic = rng.integers(0, lut_size, 20000).astype(np.uint16)
        ch = rng.integers(0, n_ch, mic.size).astype(np.int8)
        t = _make_tttr(np.arange(mic.size), mic, ch, n_micro_channels=lut_size)
        t.apply_channel_luts(channel_luts, channel_shifts)
        t.apply_luts_and_shifts(0, False)
        got = np.asarray(t.micro_times)
        # numpy transcription: nearest lookup, floor(+0.5), + shift, mod size;
        # micro times >= lut_size-1 are only shifted.
        ref = np.empty_like(mic)
        for c in range(n_ch):
            m = ch == c
            v = mic[m].astype(np.int64)
            inb = v < lut_size - 1
            r = np.empty(v.size, dtype=np.int64)
            r[inb] = (np.floor(luts[c][v[inb]] + np.float32(0.5)).astype(np.int64) + shifts[c]) % lut_size
            r[~inb] = (v[~inb] + shifts[c]) % lut_size
            ref[m] = r
        np.testing.assert_array_equal(got, ref)
        # the same through set_mt_linearizer (copied; the proxy outlives the TTTR)
        lin = tttrlib.MicrotimeLinearization()
        for c in range(n_ch):
            lin.set_channel_lut(c, channel_luts[c])
            lin.set_channel_shift(c, int(shifts[c]))
        t2 = _make_tttr(np.arange(mic.size), mic, ch, n_micro_channels=lut_size)
        t2.set_mt_linearizer(lin)
        t2.apply_luts_and_shifts(0, False)
        np.testing.assert_array_equal(np.asarray(t2.micro_times), ref)
        del t2
        self.assertTrue(lin.has_luts())   # the caller's object is still alive


# --------------------------------------------------------------------------
# record decoding vs ptufile / phconvert
# --------------------------------------------------------------------------

def _load_phconvert(name):
    """Import one phconvert reader from junk/phconvert without its package
    __init__ (which needs a generated _version module and pytables)."""
    root = os.path.join(_ROOT, "junk", "phconvert")
    if not os.path.isdir(root):
        return None
    if "phconvert" not in sys.modules:
        sys.modules["phconvert._version"] = types.SimpleNamespace(version="0")
        spec = importlib.util.spec_from_file_location(
            "phconvert", os.path.join(root, "phconvert", "__init__.py"),
            submodule_search_locations=[os.path.join(root, "phconvert")])
        pkg = importlib.util.module_from_spec(spec)
        sys.modules["phconvert"] = pkg
    try:
        return importlib.import_module("phconvert." + name)
    except Exception:
        return None


@unittest.skipIf(not DATA_AVAILABLE, "test data not available")
class TestRecordDecodingAgainstIndependentReaders(unittest.TestCase):

    @unittest.skipIf(not HAVE_PTUFILE, "ptufile not installed")
    def test_hydraharp_t3_matches_ptufile(self):
        fn = settings["ptu_hh_t3_filename"]
        if not os.path.isfile(fn):
            self.skipTest(fn)
        p = ptufile.PtuFile(fn)
        r = p.decode_records()
        ph = (r["channel"] >= 0) & (r["marker"] == 0)
        d = tttrlib.TTTR(fn, "PTU")
        et = np.asarray(d.event_types)
        is_ph = et == 0
        np.testing.assert_array_equal(np.asarray(d.macro_times)[is_ph], r["time"][ph])
        np.testing.assert_array_equal(np.asarray(d.micro_times)[is_ph], r["dtime"][ph])
        np.testing.assert_array_equal(np.asarray(d.routing_channels)[is_ph], r["channel"][ph])
        self.assertAlmostEqual(d.header.macro_time_resolution, p.global_resolution, places=18)
        self.assertAlmostEqual(d.header.micro_time_resolution, p.tcspc_resolution, places=18)
        # markers, if any, agree in count and time
        mk = r["marker"] != 0
        self.assertEqual(int((et == 1).sum()), int(mk.sum()))
        if mk.any():
            np.testing.assert_array_equal(np.asarray(d.macro_times)[et == 1], r["time"][mk])

    def test_bh_spc130_matches_phconvert(self):
        fn = settings["spc132_filename"]
        if not os.path.isfile(fn):
            self.skipTest(fn)
        bh = _load_phconvert("bhreader")
        if bh is None:
            self.skipTest("phconvert not importable from junk/")
        with open(fn, "rb") as f:
            ref = bh._read_spc1xx_8xx(f)
        d = tttrlib.TTTR(fn, "SPC-130")
        et = np.asarray(d.event_types)
        # phconvert keeps markers as detectors >= marker_shift; tttrlib as event type 1
        is_ph = et == 0
        det = ref["detectors"]
        ph_ref = det < (ref["marker_ids"].min() if ref["marker_ids"].size else 255)
        np.testing.assert_array_equal(np.asarray(d.macro_times)[is_ph], ref["timestamps"][ph_ref])
        np.testing.assert_array_equal(np.asarray(d.micro_times)[is_ph], ref["nanotimes"][ph_ref])
        np.testing.assert_array_equal(np.asarray(d.routing_channels)[is_ph], det[ph_ref])
        self.assertAlmostEqual(d.header.macro_time_resolution, ref["timestamps_unit"], places=15)

    def test_ht3_matches_phconvert(self):
        fn = settings["ht3_clsm_filename"]
        if not os.path.isfile(fn):
            self.skipTest(fn)
        pq = _load_phconvert("pqreader")
        if pq is None:
            self.skipTest("phconvert not importable from junk/")
        ts, det, nano, meta, _ = pq.load_ht3(fn)
        d = tttrlib.TTTR(fn, "HT3")
        et = np.asarray(d.event_types)
        # phconvert: overflow rows are detector 127, markers are 64 + bits
        ph_ref = det < 64
        mk_ref = (det >= 64) & (det < 127)
        np.testing.assert_array_equal(np.asarray(d.macro_times)[et == 0], ts[ph_ref])
        np.testing.assert_array_equal(np.asarray(d.micro_times)[et == 0], nano[ph_ref])
        np.testing.assert_array_equal(np.asarray(d.routing_channels)[et == 0], det[ph_ref])
        np.testing.assert_array_equal(np.asarray(d.macro_times)[et == 1], ts[mk_ref])
        np.testing.assert_array_equal(np.asarray(d.routing_channels)[et == 1], det[mk_ref] - 64)

    # ---- 2026-08-17: the second reading round -----------------------------

    def _ptu_vs_ptufile(self, fn, channel_offset=0):
        """Photons and markers of a PTU vs ptufile. `channel_offset` is what
        tttrlib adds to ptufile's 0-based channel: 0 for HydraHarp/TimeHarp/
        MultiHarp (a 0-based hardware field), 1 for PicoHarp T3 whose 4-bit
        field is 1-based on the wire and is kept as written."""
        if not os.path.isfile(fn):
            self.skipTest(fn)
        p = ptufile.PtuFile(fn)
        r = p.decode_records()
        ph = (r["channel"] >= 0) & (r["marker"] == 0)
        mk = r["marker"] != 0
        d = tttrlib.TTTR(fn, "PTU")
        et = np.asarray(d.event_types)
        self.assertEqual(int((et == 0).sum()), int(ph.sum()))
        np.testing.assert_array_equal(np.asarray(d.macro_times)[et == 0], r["time"][ph])
        np.testing.assert_array_equal(np.asarray(d.micro_times)[et == 0], r["dtime"][ph])
        np.testing.assert_array_equal(np.asarray(d.routing_channels)[et == 0] - channel_offset, r["channel"][ph])
        self.assertEqual(int((et == 1).sum()), int(mk.sum()))
        np.testing.assert_array_equal(np.asarray(d.macro_times)[et == 1], r["time"][mk])
        return d, r, p

    @unittest.skipIf(not HAVE_PTUFILE, "ptufile not installed")
    def test_picoharp_t3_matches_ptufile(self):
        """PicoHarp T3 (also every Leica SP8 PTU): channel 15 is the special
        record -- dtime 0 an overflow, otherwise a marker with its bits in
        dtime; photons keep dtime 0. Until 2026-08-17 the decoder tested
        dtime == 0 for markers (0.1 % of photons lost, channel-15 markers
        passed as photons). Markers keep channel 15 with the bits in the
        micro time (the SP8 CLSM routine selects on that)."""
        for key in ("ptu_picoharp_t3_filename", "clsm_sp8_filename"):
            fn = settings.get(key, "")
            with self.subTest(file=os.path.basename(fn)):
                d, r, p = self._ptu_vs_ptufile(fn, channel_offset=1)
                et = np.asarray(d.event_types)
                mk = r["marker"] != 0
                np.testing.assert_array_equal(np.asarray(d.micro_times)[et == 1], r["marker"][mk])
                self.assertTrue(np.all(np.asarray(d.routing_channels)[et == 1] == 15))
                self.assertGreater(int(((et == 0) & (np.asarray(d.micro_times) == 0)).sum()), 0)

    @unittest.skipIf(not HAVE_PTUFILE, "ptufile not installed")
    def test_timeharp260_pt3_matches_ptufile(self):
        self._ptu_vs_ptufile(settings.get("microtime_th260_beads_filename", ""))

    @unittest.skipIf(not HAVE_PTUFILE, "ptufile not installed")
    def test_generic_t3_matches_ptufile(self):
        """MultiHarp / HydraHarp v2 'generic' T3 (record type 0x00010304 family)."""
        self._ptu_vs_ptufile(settings.get("ptu_generic_t3_filename", ""))

    def test_picoharp_t3_write_read_roundtrip_with_markers(self):
        """The PHT3 writer mirrors the reader: markers as channel 15 + bits,
        photons with micro time 0 survive (they used to be clipped to 1)."""
        rng = np.random.default_rng(1)
        n = 5000
        macro = np.sort(rng.integers(0, 400000, n)).astype(np.uint64)
        micro = rng.integers(0, 4096, n).astype(np.uint16)
        micro[:200] = 0
        chan = rng.integers(1, 5, n).astype(np.int8)
        types = np.zeros(n, np.int8)
        marker_idx = rng.choice(n, 40, replace=False)
        types[marker_idx] = 1
        chan[marker_idx] = 15
        micro[marker_idx] = rng.integers(1, 5, 40)
        d = tttrlib.TTTR()
        d.append_events(macro, micro, chan, types)
        d.header.set_macro_time_resolution(25e-9)
        d.header.set_micro_time_resolution(16e-12)
        d.header.set_number_of_micro_time_channels(4096)
        d.header.tttr_container_type = 0
        d.header.tttr_record_type = 5          # PQ_RECORD_TYPE_PHT3
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".ptu")
        os.close(fd)
        try:
            self.assertTrue(d.write(path))
            back = tttrlib.TTTR(path, "PTU")
            np.testing.assert_array_equal(np.asarray(back.macro_times), macro)
            np.testing.assert_array_equal(np.asarray(back.micro_times), micro)
            np.testing.assert_array_equal(np.asarray(back.routing_channels), chan)
            np.testing.assert_array_equal(np.asarray(back.event_types), types)
            if HAVE_PTUFILE:
                r = ptufile.PtuFile(path).decode_records()
                ph = (r["channel"] >= 0) & (r["marker"] == 0)
                np.testing.assert_array_equal(r["dtime"][ph], micro[types == 0])
                np.testing.assert_array_equal(r["marker"][r["marker"] != 0], micro[types == 1])
        finally:
            if os.path.exists(path):
                os.remove(path)

    def test_brighteyes_ttr_matches_libttp(self):
        """BrightEyes-TTM ``.ttr`` vs the vendor's own Cython parser
        (``libttp.ttpCython.timeProcessNewProtocol``, recorded by
        ``tttr/gen_ab_brighteyes_libttp_reference.py`` on the first 4 M words of
        the Zenodo sample): photon channels, macro times (libttp's default
        16-bit step unwrapping) and micro times (TDC code differenced against
        the record's laser code when it has one) identical for 326 835
        photons; pixel / line / frame markers are the rising edges of the
        step-byte enable bits (A -> pixel, C -> line, B -> frame; libttp's
        column names call B 'scan' and C 'line')."""
        fix = os.path.join(_ROOT, "test", "data", "reference", "brighteyes_libttp_reference.npz")
        fn = settings.get("brighteyes_ttr_filename", "")
        if not os.path.exists(fix) or not os.path.isfile(fn):
            self.skipTest("libttp fixture or .ttr sample missing")
        d = np.load(fix)
        tt = tttrlib.TTTR(fn, "BRIGHTEYES-TTR")
        et = np.asarray(tt.event_types)
        mt = np.asarray(tt.macro_times).astype(np.int64)
        mi = np.asarray(tt.micro_times).astype(np.int64)
        ch = np.asarray(tt.routing_channels)
        n = int(d["photon_record"].size)
        ph = np.flatnonzero(et == 0)[:n]
        np.testing.assert_array_equal(ch[ph], d["photon_channel"])
        np.testing.assert_array_equal(mt[ph], d["record_step"][d["photon_record"]])
        expected_micro = np.where(d["photon_laser_valid"],
                                  (d["photon_code"].astype(np.int64) - d["photon_laser_code"]) % 256,
                                  d["photon_code"].astype(np.int64))
        np.testing.assert_array_equal(mi[ph], expected_micro)
        last_step = int(d["record_step"][-1])
        mk = (mt <= last_step) & (et == 1)
        for bit, marker in (("pixel_enable", 1), ("frame_enable", 2), ("line_enable", 3)):   # libttp names, our meaning
            b = d[bit].astype(np.int8)
            rise = np.flatnonzero((b[1:] > 0) & (b[:-1] == 0)) + 1
            if b[0] > 0:
                rise = np.concatenate([[0], rise])
            np.testing.assert_array_equal(mt[mk & (ch == marker)], d["record_step"][rise])

    def test_photon_hdf5_matches_h5py(self):
        try:
            import h5py
        except ImportError:
            self.skipTest("h5py not installed")
        fn = settings.get("photon_hdf_filename", "")
        if not os.path.isfile(fn):
            self.skipTest(fn)
        d = tttrlib.TTTR(fn, "PHOTON-HDF5")
        with h5py.File(fn, "r") as f:
            ts = f["/photon_data/timestamps"][:]
            det = f["/photon_data/detectors"][:]
            nano = f["/photon_data/nanotimes"][:]
            unit = float(f["/photon_data/timestamps_specs/timestamps_unit"][()])
            tunit = float(f["/photon_data/nanotimes_specs/tcspc_unit"][()])
        np.testing.assert_array_equal(np.asarray(d.macro_times), ts)
        np.testing.assert_array_equal(np.asarray(d.micro_times), nano)
        np.testing.assert_array_equal(np.asarray(d.routing_channels), det)
        self.assertAlmostEqual(d.header.macro_time_resolution, unit, places=18)
        self.assertAlmostEqual(d.header.micro_time_resolution, tunit, places=18)

    def test_ht3_v1_sample_is_sf_compressed_and_phconvert_cannot_know(self):
        """``pq_ht3v1.0_hh_t3.ht3`` is a HydraHarp v1 HT3 whose overflow
        records carry a count (Suren Felekyan's SF compression: the sync
        counter advances by (1 + count) * 1024). tttrlib detects that (record
        type SF_HT3); phconvert decodes v1 overflows as 1024 each and comes out
        2.2x too short. Channels and micro times agree exactly; the macro times
        differ by design. The stream itself decides: at ~100 kHz a plain v1
        file would carry runs of consecutive overflow records for every gap
        beyond 32 us, and this file has none."""
        fn = settings.get("ht3_v1_filename", "")
        if not os.path.isfile(fn):
            self.skipTest(fn)
        pq = _load_phconvert("pqreader")
        if pq is None:
            self.skipTest("phconvert not importable from junk/")
        ts, det, nano, meta, _ = pq.load_ht3(fn)
        d = tttrlib.TTTR(fn, "HT3")
        self.assertEqual(d.header.tttr_record_type, 14)          # PQ_RECORD_TYPE_SF_HT3
        ph = det < 64
        np.testing.assert_array_equal(np.asarray(d.routing_channels), det[ph])
        np.testing.assert_array_equal(np.asarray(d.micro_times), nano[ph])
        raw = np.fromfile(fn, dtype=np.uint32)
        recs = raw[raw.size - ts.size:]
        ovf = ((recs >> 25) & 0x7F) == 0x7F
        self.assertGreater(int(ovf.sum()), 0)
        self.assertEqual(int((np.diff(np.flatnonzero(ovf)) == 1).sum()), 0)   # never two overflow records in a row
        counts = recs[ovf] & 0xFFFFFF
        self.assertGreater(int((counts > 0).sum()), 0)                    # counted overflows are present
        # tttrlib's macro times are phconvert's plus 1024 per counted overflow
        add = np.zeros(recs.size, dtype=np.int64); add[ovf] = counts.astype(np.int64)
        expected = ts[ph].astype(np.int64) + 1024 * np.cumsum(add)[ph]
        np.testing.assert_array_equal(np.asarray(d.macro_times).astype(np.int64), expected)

    def test_bh_set_sidecar_tac_width_matches_phconvert_load_set(self):
        """The micro-time channel width of a Becker & Hickl file lives only in
        the .set sidecar (SPCM's SP_TAC_TC = SP_TAC_R / (SP_TAC_G * SP_ADC_RE)).
        phconvert's ``load_set`` parses the same file independently. Until
        2026-08-17 only the SPC-QC path used the sidecar, without the TAC gain:
        the SPC-130 FLIM sample (gain 4, 12.5 ns over 4096 channels) read as
        6.1 ps instead of 3.05 ps."""
        bh = _load_phconvert("bhreader")
        if bh is None:
            self.skipTest("phconvert not importable from junk/")
        for spc, ct in (("imaging/bh/spcm/FocalCheck_A1_20x_8xzoom_750nm_m1.spc", "SPC-130"),
                        ("bh/bh_spcqc004.spc", "SPC-QC")):
            fn = os.path.join(_ROOT, "tttr-data", spc)
            if not os.path.isfile(fn):
                continue
            with self.subTest(file=os.path.basename(spc)):
                sp = bh.load_set(fn[:-4] + ".set")["setup"]
                d = tttrlib.TTTR(fn, ct)
                self.assertAlmostEqual(d.header.micro_time_resolution, float(sp["SP_TAC_TC"]), delta=1e-18)
                self.assertAlmostEqual(d.header.micro_time_resolution,
                                       float(sp["SP_TAC_R"]) / (float(sp["SP_TAC_G"]) * int(sp["SP_ADC_RE"])), delta=1e-18)
                self.assertEqual(d.header.number_of_micro_time_channels, int(sp["SP_ADC_RE"]))

    def test_bh_spc630_256_matches_phconvert_up_to_its_overflow_shift(self):
        """SPC-600/630 32-bit records: 8-bit ADC, 17-bit macro time, 3-bit
        routing. phconvert's `_read_spc6xx_32bit` masks the 17-bit field but
        adds 2^12 per overflow (the SPC-130 shift) and inverts the ADC against
        4095 -- its timestamps run backwards 61 times on this file. Channels
        and the ADC (mod 256) agree exactly; the macro times agree once the
        overflow increment is 2^17."""
        fn = settings["spc630_filename"]
        if not os.path.isfile(fn):
            self.skipTest(fn)
        bh = _load_phconvert("bhreader")
        if bh is None:
            self.skipTest("phconvert not importable from junk/")
        with open(fn, "rb") as f:
            ref = bh._read_spc6xx_32bit(f)
        d = tttrlib.TTTR(fn, "SPC-600_256")
        self.assertEqual(len(d.macro_times), ref["timestamps"].size)
        np.testing.assert_array_equal(np.asarray(d.routing_channels), ref["detectors"])
        np.testing.assert_array_equal(np.asarray(d.micro_times), ref["nanotimes"] & 0xFF)
        mt = np.asarray(d.macro_times).astype(np.int64)
        self.assertTrue(np.all(np.diff(mt) >= 0))
        self.assertGreater(int((np.diff(ref["timestamps"].astype(np.int64)) < 0).sum()), 0)   # the reference's defect
        # phconvert's own fields with the 17-bit overflow increment reproduce tttrlib
        raw = np.fromfile(fn, dtype=np.uint32)[1:]
        field = ((raw & 0x01FFFF00) >> 8).astype(np.int64)
        ovfl = ((raw >> 30) & 1).astype(np.int64)
        multi = (raw >> 30) == 3
        keep = ((raw >> 31) & 1) == 0
        ts17 = field + (np.cumsum(ovfl) << 17)
        if not multi.any():
            np.testing.assert_array_equal(ts17[keep], mt)
        self.assertAlmostEqual(d.header.macro_time_resolution, ref["timestamps_unit"], places=15)

    def test_bh_spc_qc_matches_phconvert(self):
        fn = settings.get("spcqc_filename", "")
        if not os.path.isfile(fn):
            self.skipTest(fn)
        bh = _load_phconvert("bhreader")
        if bh is None:
            self.skipTest("phconvert not importable from junk/")
        with open(fn, "rb") as f:                # a path would re-read the header word as a record
            ref = bh._read_QCX04(f)
        d = tttrlib.TTTR(fn, "SPC-QC")
        np.testing.assert_array_equal(np.asarray(d.macro_times), ref["timestamps"])
        np.testing.assert_array_equal(np.asarray(d.micro_times), ref["nanotimes"])
        np.testing.assert_array_equal(np.asarray(d.routing_channels), ref["detectors"])
        self.assertAlmostEqual(d.header.macro_time_resolution, ref["timestamps_unit"], places=15)

    def test_sm_matches_phconvert(self):
        fn = settings.get("sm_filename", "")
        if not os.path.isfile(fn):
            self.skipTest(fn)
        sm = _load_phconvert("smreader")
        if sm is None:
            self.skipTest("phconvert not importable from junk/")
        ts, det, _ = sm.load_sm(fn, return_labels=True)
        d = tttrlib.TTTR(fn, "SM")
        np.testing.assert_array_equal(np.asarray(d.macro_times), ts)
        np.testing.assert_array_equal(np.asarray(d.routing_channels), det)


# --------------------------------------------------------------------------
# util: SHA-256, bit ops, byte order -- header-only, via a tiny harness
# --------------------------------------------------------------------------

_HARNESS = r"""
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Sha256.h"
#include "BitOps.h"
#include "ByteOrder.h"
#include "string_encoding.h"
#include "Histogram.h"
using namespace tttrlib::bitops;
using namespace tttrlib::util;
using namespace tttrlib::string_encoding;
int main(int argc, char** argv) {
    std::string cmd = argv[1];
    if (cmd == "sha") { std::string s(argv[2]); printf("%s\n", sha256_hex(s).c_str()); }
    else if (cmd == "shafile") { printf("%s\n", sha256_file_hex(argv[2]).c_str()); }
    else if (cmd == "bits") {
        uint64_t x = strtoull(argv[2], nullptr, 10);
        printf("%d %d\n", x ? ctz64(x) : -1, popcount64(x));
    }
    else if (cmd == "swap") {
        uint32_t a = (uint32_t) strtoul(argv[2], nullptr, 10); SwapEndian(a);
        uint64_t b = strtoull(argv[3], nullptr, 10); SwapEndian(b);
        uint16_t c = (uint16_t) strtoul(argv[4], nullptr, 10); SwapEndian(c);
        printf("%u %llu %u\n", a, (unsigned long long) b, c);
    }
    else if (cmd == "latin1") {
        std::string u8 = utf8_to_iso_8859_1(std::string(argv[2]));
        std::string back = iso_8859_1_to_utf8(u8);
        printf("%s\n", back.c_str());
    }
    else if (cmd == "bincount") {
        int n_bins = atoi(argv[2]);
        std::vector<int> data; for (int i = 3; i < argc; ++i) data.push_back(atoi(argv[i]));
        std::vector<int> bins(n_bins, 0);
        bincount1D(data.data(), (int) data.size(), bins.data(), n_bins);
        for (int b : bins) printf("%d ", b); printf("\n");
    }
    else if (cmd == "words") {
        size_t n = strtoull(argv[2], nullptr, 10);
        printf("%zu %llu\n", word_count(n), (unsigned long long) tail_mask(n));
    }
    return 0;
}
"""


class TestUtilAgainstPython(unittest.TestCase):
    exe = None

    @classmethod
    def setUpClass(cls):
        cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
        if cxx is None:
            raise unittest.SkipTest("no C++ compiler")
        import tempfile
        d = tempfile.mkdtemp(prefix="tttrlib_util_ab_")
        src = os.path.join(d, "h.cpp")
        with open(src, "w") as f:
            f.write(_HARNESS)
        cls.exe = os.path.join(d, "h")
        inc = os.path.join(_ROOT, "modules", "util", "include")
        inc_core = os.path.join(_ROOT, "modules", "core", "include")
        hist_cpp = os.path.join(_ROOT, "modules", "core", "src", "Histogram.cpp")
        r = subprocess.run([cxx, "-std=c++17", "-O1", "-I", inc, "-I", inc_core,
                            src, hist_cpp, "-o", cls.exe],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise unittest.SkipTest("harness did not compile: " + r.stderr[-800:])

    def _run(self, *args):
        return subprocess.run([self.exe, *map(str, args)], capture_output=True,
                              text=True, check=True).stdout.strip()

    def test_sha256_matches_hashlib(self):
        for s in ["", "abc", "The quick brown fox jumps over the lazy dog",
                  "a" * 1000, "x" * 55, "y" * 56, "z" * 64, "w" * 119]:
            self.assertEqual(self._run("sha", s), hashlib.sha256(s.encode()).hexdigest())

    def test_sha256_file_matches_hashlib(self):
        fn = os.path.join(_ROOT, "LICENSE.txt")
        with open(fn, "rb") as f:
            ref = hashlib.sha256(f.read()).hexdigest()
        self.assertEqual(self._run("shafile", fn), ref)

    def test_ctz_popcount_match_python(self):
        rng = _rng(12)
        vals = [1, 2, 3, 2 ** 63, 2 ** 64 - 1, 0xF0F0F0F0F0F0F0F0]
        vals += [int(v) for v in rng.integers(1, 2 ** 63, 20, dtype=np.int64)]
        for v in vals:
            ctz, pop = map(int, self._run("bits", v).split())
            self.assertEqual(ctz, (v & -v).bit_length() - 1)
            self.assertEqual(pop, bin(v).count("1"))

    def test_swap_endian_matches_int_to_bytes(self):
        a, b, c = 0x12345678, 0x0102030405060708, 0xABCD
        got = list(map(int, self._run("swap", a, b, c).split()))
        ref = [int.from_bytes(a.to_bytes(4, "little"), "big"),
               int.from_bytes(b.to_bytes(8, "little"), "big"),
               int.from_bytes(c.to_bytes(2, "little"), "big")]
        self.assertEqual(got, ref)

    def test_bincount1D_matches_numpy(self):
        """`bincount1D` vs np.bincount, through the harness and (since
        2026-08-17, when the array binding was added) through Python."""
        rng = _rng(13)
        data = rng.integers(-3, 20, 300)
        for n_bins in (10, 20, 25):
            got = np.array(list(map(int, self._run("bincount", n_bins, *data).split())))
            ref = np.bincount(data[(data >= 0) & (data < n_bins)], minlength=n_bins)
            np.testing.assert_array_equal(got, ref)
            out = np.zeros(n_bins, np.int32)
            tttrlib.bincount1D(data.astype(np.int32), out)
            np.testing.assert_array_equal(out, ref)

    def test_word_count_and_tail_mask(self):
        for n in (0, 1, 63, 64, 65, 127, 128, 1000):
            wc, tm = map(int, self._run("words", n).split())
            self.assertEqual(wc, (n + 63) // 64)
            r = n % 64
            self.assertEqual(tm, (2 ** 64 - 1) if r == 0 else (1 << r) - 1)

    def test_latin1_round_trip_matches_codecs(self):
        s = "Grüße Ångström café ¡Hola! ½ µ"
        self.assertEqual(self._run("latin1", s),
                         s.encode("latin-1").decode("latin-1"))


if __name__ == "__main__":
    unittest.main()
