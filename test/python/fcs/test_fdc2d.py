"""The 2D-FDC photon pass (`Fdc2D.h`), against a definition rather than a port.

A 2D-FDC matrix counts photon *pairs*: for every reference photon, the photons
whose macro-time lands in `dT ± ddT/2`, binned by the two micro-times. The
kernel finds those windows with a binary search and bins in log-time, and both
of those are places where a wrong answer looks entirely reasonable.

So the reference here is a **double loop over every pair** — no search, no
shared code with the kernel — and the bin edges are recomputed from the
published axis rather than read back out of the thing under test. A port
checked only against the implementation it replaces cannot tell a faithful port
from a shared mistake; PRD-035 hit exactly that when a recorded fixture turned
out to encode a live bug in the original.

The method-level tests — that two lifetimes come back, that cross-peaks appear
only when states interconvert, that the lag dependence recovers the rate, and
that a single state produces no cross-peak — run against a simulated stream
whose answer is known before the analysis, and live in
`test_fdc2d_simulation.py`.
"""

import unittest

import numpy as np

import tttrlib


def log_ticks(t_min, t_max, logt_imax):
    """The published axis, fetched once so the reference can bin independently."""
    ticks = np.empty(logt_imax + 1, dtype=np.int64)
    tttrlib.fdc_log_ticks(t_max - t_min + 1, ticks)
    return ticks


def brute_force(macro, micro, dT, ddT, t_min, t_max, logt_imax):
    """Every (i, k) pair, tested directly against the window. O(n^2) on purpose.

    This is the definition of the matrix, written the slow way: no binary
    search, no chunking, and the bin lookup done with `np.searchsorted` on the
    published edges instead of calling the kernel's own.
    """
    ticks = log_ticks(t_min, t_max, logt_imax)
    t_imax = t_max - t_min + 1
    half = ddT // 2
    last = macro[-1]
    out = np.zeros((logt_imax, logt_imax), dtype=np.int64)

    def bin_of(mt):
        tau = mt - t_min
        if tau <= 0 or tau >= t_imax:
            return -1
        idx = int(np.searchsorted(ticks, tau, side="left"))
        if idx <= 0 or idx >= ticks.shape[0]:
            return -1
        return idx - 1

    for i in range(macro.shape[0]):
        bi = bin_of(micro[i])
        if bi <= 0:
            continue
        start, stop = macro[i] + dT - half, macro[i] + dT + half
        if stop > last:
            continue
        for k in range(macro.shape[0]):
            if not (start <= macro[k] <= stop):
                continue
            bk = bin_of(micro[k])
            if bk > 0 and bk < logt_imax:
                out[bi, bk] += 1
    return out


def a_small_stream(seed=3, n=400, t_max=4096):
    """Sorted macro-times with duplicates, and micro-times over the full range."""
    rng = np.random.default_rng(seed)
    macro = np.sort(rng.integers(0, 20_000, size=n)).astype(np.int64)
    micro = rng.integers(1, t_max, size=n).astype(np.int64)
    return macro, micro


def scan(macro, micro, lags, ddT, t_min=0, t_max=4096, logt_imax=16, n_chunks=1):
    lags = np.asarray(lags, dtype=np.int64)
    out = np.zeros(lags.size * logt_imax * logt_imax, dtype=np.int64)
    tttrlib.fdc_scan_log(macro, micro, lags, ddT, t_min, t_max,
                         logt_imax, n_chunks, out)
    return out.reshape(lags.size, logt_imax, logt_imax)


class TestAgainstTheDefinition(unittest.TestCase):

    def test_it_matches_a_double_loop_over_every_pair(self):
        macro, micro = a_small_stream()
        got = scan(macro, micro, [500], ddT=200)[0]
        expected = brute_force(macro, micro, 500, 200, 0, 4096, 16)
        np.testing.assert_array_equal(got, expected)
        self.assertGreater(got.sum(), 0, "a test on an all-zero matrix proves nothing")

    def test_it_matches_the_double_loop_at_several_lags_at_once(self):
        """The lag loop is where a single-lag kernel and a scan diverge."""
        macro, micro = a_small_stream(seed=5)
        lags = [100, 500, 2000]
        got = scan(macro, micro, lags, ddT=150)
        for j, dT in enumerate(lags):
            with self.subTest(lag=dT):
                np.testing.assert_array_equal(
                    got[j], brute_force(macro, micro, dT, 150, 0, 4096, 16))

    def test_the_single_lag_entry_point_agrees_with_the_scan(self):
        macro, micro = a_small_stream(seed=7)
        out = np.zeros(16 * 16, dtype=np.int64)
        tttrlib.fdc_log(macro, micro, 500, 200, 0, 4096, 16, 1, out)
        np.testing.assert_array_equal(out.reshape(16, 16),
                                      scan(macro, micro, [500], ddT=200)[0])


class TestTheContracts(unittest.TestCase):
    """The two properties the header calls contracts, and the input checks."""

    def test_the_result_does_not_depend_on_the_chunk_count(self):
        """Chunking is a parallelism decision. Integer sums make it exact."""
        macro, micro = a_small_stream(seed=11, n=1500)
        reference = scan(macro, micro, [300, 900], ddT=250, n_chunks=1)
        for nc in (2, 3, 7, 64):
            with self.subTest(n_chunks=nc):
                np.testing.assert_array_equal(
                    scan(macro, micro, [300, 900], ddT=250, n_chunks=nc), reference)

    def test_a_micro_time_outside_the_window_is_dropped_not_clamped(self):
        """Two claims: it drops them, and it is not merely clamping them.

        Checking only that the count falls would pass for a kernel that clamps
        into the top bin, since that also changes the numbers. So the result is
        compared against the dropping reference *and* against the same stream
        with the offending micro-times clamped by hand -- which must differ.
        The top bin is not reserved for out-of-range values; real photons live
        there, so "the edge bin is empty" is not the test.
        """
        macro, micro = a_small_stream(seed=13, n=300)
        inside = scan(macro, micro, [400], ddT=200, t_max=4096)[0]

        spoiled = micro.copy()
        spoiled[::10] = 9999                      # far past t_max
        after = scan(macro, spoiled, [400], ddT=200, t_max=4096)[0]

        self.assertLess(after.sum(), inside.sum(),
                        "out-of-range photons still contributed pairs")
        np.testing.assert_array_equal(
            after, brute_force(macro, spoiled, 400, 200, 0, 4096, 16))

        clamped = np.minimum(spoiled, 4095)
        self.assertFalse(
            np.array_equal(after, scan(macro, clamped, [400], ddT=200)[0]),
            "dropping and clamping gave the same matrix, so this proves nothing")

    def test_an_unsorted_stream_is_rejected(self):
        """The window bounds come from a binary search; unsorted is silent garbage."""
        macro, micro = a_small_stream(seed=17, n=100)
        macro[50], macro[10] = macro[10], macro[50]
        with self.assertRaises(ValueError):
            scan(macro, micro, [400], ddT=200)

    def test_the_shapes_must_match(self):
        macro, micro = a_small_stream(seed=19, n=50)
        with self.assertRaises(ValueError):
            tttrlib.fdc_scan_log(macro, micro[:-1], np.asarray([400], dtype=np.int64),
                                 200, 0, 4096, 16, 1, np.zeros(16 * 16, dtype=np.int64))
        with self.assertRaises(ValueError):
            # out sized for one lag, two lags asked for
            tttrlib.fdc_scan_log(macro, micro, np.asarray([400, 800], dtype=np.int64),
                                 200, 0, 4096, 16, 1, np.zeros(16 * 16, dtype=np.int64))


class TestTheLogAxis(unittest.TestCase):
    """The bin edges are a published convention, so they are pinned here."""

    def test_the_edges_are_the_reference_construction(self):
        t_imax = 4097
        n = 17
        ticks = np.empty(n, dtype=np.int64)
        tttrlib.fdc_log_ticks(t_imax, ticks)
        self.assertEqual(ticks[0], -1)
        expected = [-1] + [int(np.floor(t_imax ** (j / (n - 1)) - 1.0 + 0.5))
                           for j in range(1, n)]
        np.testing.assert_array_equal(ticks, np.asarray(expected, dtype=np.int64))
        self.assertTrue(np.all(np.diff(ticks) >= 0), "the axis must be sorted")

    def test_a_value_below_the_first_edge_or_above_the_last_is_out_of_range(self):
        ticks = np.asarray([-1, 0, 3, 10], dtype=np.int64)
        self.assertEqual(tttrlib.fdc_log_bin(-5, ticks), -1)
        self.assertEqual(tttrlib.fdc_log_bin(11, ticks), -1)
        self.assertEqual(tttrlib.fdc_log_bin(0, ticks), 0)
        self.assertEqual(tttrlib.fdc_log_bin(3, ticks), 1)
        self.assertEqual(tttrlib.fdc_log_bin(9, ticks), 2)

    def test_the_bins_are_right_closed_and_that_is_the_reference_convention(self):
        """`(ticks[j], ticks[j+1]]` -- a value on an edge belongs to the bin
        that *ends* there, not the one that starts.

        Inherited deliberately from the implementation this replaces, which is
        `np.searchsorted(..., side="left") - 1`. Pinned because it is invisible
        anywhere else and flipping it would shift every count by one bin while
        leaving the matrix looking entirely plausible.
        """
        ticks = np.asarray([-1, 0, 3, 10], dtype=np.int64)
        for value in (-5, -1, 0, 1, 2, 3, 4, 9, 10):
            with self.subTest(value=value):
                expected = int(np.searchsorted(ticks, value, side="left")) - 1
                self.assertEqual(tttrlib.fdc_log_bin(value, ticks), expected)
        # Above the last edge the kernel says out-of-range where a bare
        # searchsorted would hand back a bin that does not exist.
        self.assertEqual(tttrlib.fdc_log_bin(11, ticks), -1)


class TestACallerSuppliedAxis(unittest.TestCase):
    """`fdc_scan_axis` — the general form the log scan is a special case of.

    It exists because the reference implementation also builds a *linearly*
    binned matrix, which a downstream caller reads as the linear decay, and log
    binning cannot be undone to recover it. Rather than a second kernel, the
    axis became an argument: the linear rule `ceil(tau / f)` is the same
    binary-search lookup as the log one, given edges `[-1, 0, f, 2f, ...]`.
    """

    @staticmethod
    def scan_axis(macro, micro, lags, ddT, ticks, t_min=0, t_max=4096, n_chunks=1):
        lags = np.asarray(lags, dtype=np.int64)
        ticks = np.asarray(ticks, dtype=np.int64)
        bins = ticks.size - 1
        out = np.zeros(lags.size * bins * bins, dtype=np.int64)
        tttrlib.fdc_scan_axis(macro, micro, lags, ddT, t_min, t_max, ticks,
                              n_chunks, out)
        return out.reshape(lags.size, bins, bins)

    def test_the_log_axis_reproduces_the_log_scan_exactly(self):
        """If these ever disagree, one of the two has its own bin rule."""
        macro, micro = a_small_stream(seed=23)
        ticks = log_ticks(0, 4096, 16)
        np.testing.assert_array_equal(
            self.scan_axis(macro, micro, [500], 200, ticks)[0],
            scan(macro, micro, [500], ddT=200)[0])

    def test_a_linear_axis_bins_by_ceiling_division(self):
        """The reference's linear rule, checked against arithmetic rather than
        against the edge array that is supposed to express it."""
        factor = 8
        span = 4096
        ticks = np.concatenate([[-1], np.arange(0, span + factor, factor)]).astype(np.int64)
        for tau in (1, 7, 8, 9, 16, 17, 4095):
            with self.subTest(tau=tau):
                self.assertEqual(tttrlib.fdc_log_bin(tau, ticks),
                                 -(-tau // factor))

    def test_a_linear_matrix_matches_a_double_loop_using_that_rule(self):
        macro, micro = a_small_stream(seed=29, n=250)
        factor, span = 16, 4096
        ticks = np.concatenate([[-1], np.arange(0, span + factor, factor)]).astype(np.int64)
        got = self.scan_axis(macro, micro, [400], 200, ticks)[0]

        bins = ticks.size - 1
        expected = np.zeros((bins, bins), dtype=np.int64)
        half, last = 100, macro[-1]
        for i in range(macro.shape[0]):
            bi = -(-int(micro[i]) // factor)
            if not 0 < bi < bins:
                continue
            start, stop = macro[i] + 400 - half, macro[i] + 400 + half
            if stop > last:
                continue
            for k in range(macro.shape[0]):
                if start <= macro[k] <= stop:
                    bk = -(-int(micro[k]) // factor)
                    if 0 < bk < bins:
                        expected[bi, bk] += 1
        np.testing.assert_array_equal(got, expected)
        self.assertGreater(got.sum(), 0)

    def test_a_descending_axis_is_rejected(self):
        """The bin comes from a binary search, so unsorted edges answer wrongly."""
        macro, micro = a_small_stream(seed=31, n=60)
        with self.assertRaises(ValueError):
            self.scan_axis(macro, micro, [400], 200, [-1, 10, 5, 20])


class TestTwoAxesInOnePass(unittest.TestCase):
    """`fdc_scan_two_axes` — both matrices from one walk over the photons.

    It exists for a measured reason: at 1M photons and comparable bin counts,
    one axis costs 144.8 ms and two axes as two separate calls cost 329.1 ms.
    The second pass is a real 2.27x, and a caller wanting the log matrix for a
    lifetime inversion usually wants the linearly-binned decay beside it.

    The thing to pin is therefore not "it produces a matrix" but "it produces
    the *same* matrices the separate calls do" -- a fused loop that quietly
    disagrees with the single-axis path in an edge case is exactly what this
    optimisation risks.
    """

    @staticmethod
    def axes():
        log = np.empty(17, dtype=np.int64)
        tttrlib.fdc_log_ticks(4096, log)
        factor = 256
        lin = np.concatenate([[-1], np.arange(0, 4096 + factor, factor)]).astype(np.int64)
        return log, lin

    def two(self, macro, micro, lags, ddT, ticks_a, ticks_b, n_chunks=4):
        lags = np.asarray(lags, dtype=np.int64)
        ba, bb = ticks_a.size - 1, ticks_b.size - 1
        oa = np.zeros(lags.size * ba * ba, dtype=np.int64)
        ob = np.zeros(lags.size * bb * bb, dtype=np.int64)
        tttrlib.fdc_scan_two_axes(macro, micro, lags, ddT, 0, 4095,
                                  ticks_a, ticks_b, n_chunks, oa, ob)
        return oa.reshape(lags.size, ba, ba), ob.reshape(lags.size, bb, bb)

    def one(self, macro, micro, lags, ddT, ticks, n_chunks=4):
        lags = np.asarray(lags, dtype=np.int64)
        bins = ticks.size - 1
        out = np.zeros(lags.size * bins * bins, dtype=np.int64)
        tttrlib.fdc_scan_axis(macro, micro, lags, ddT, 0, 4095, ticks, n_chunks, out)
        return out.reshape(lags.size, bins, bins)

    def test_each_axis_matches_its_own_single_axis_call(self):
        macro, micro = a_small_stream(seed=37, n=800)
        log, lin = self.axes()
        got_log, got_lin = self.two(macro, micro, [400, 900], 200, log, lin)
        np.testing.assert_array_equal(got_log, self.one(macro, micro, [400, 900], 200, log))
        np.testing.assert_array_equal(got_lin, self.one(macro, micro, [400, 900], 200, lin))
        self.assertGreater(got_log.sum(), 0)

    def test_the_totals_differ_only_by_the_photons_an_axis_cannot_place(self):
        """Two binnings of one set of pairs do NOT have to agree, and here they
        do not.

        Bin 0 is excluded on every axis, and "bin 0" covers a different span of
        micro-times per axis: on this log axis the first edges are
        `[-1, 1, 2, 4, ...]`, so `tau = 1` lands in bin 0 and is dropped, while
        the linear axis (`[-1, 0, 256, ...]`) puts it in bin 1 and keeps it.
        The totals therefore differ by exactly the pairs involving a `tau = 1`
        photon.

        This test previously asserted the totals were equal, which sounded
        obviously true and was not -- the kernel was right and the assertion was
        wrong. What is pinned now is the actual invariant: remove the photons
        only one axis can place, and the two agree exactly.
        """
        macro, micro = a_small_stream(seed=41, n=600)
        log, lin = self.axes()

        got_log, got_lin = self.two(macro, micro, [500], 200, log, lin)
        self.assertNotEqual(int(got_log.sum()), int(got_lin.sum()),
                            "the sample no longer exercises the asymmetry")
        self.assertEqual(tttrlib.fdc_log_bin(1, log), 0)     # dropped
        self.assertEqual(tttrlib.fdc_log_bin(1, lin), 1)     # kept

        keep = micro != 1
        same_log, same_lin = self.two(np.ascontiguousarray(macro[keep]),
                                      np.ascontiguousarray(micro[keep]),
                                      [500], 200, log, lin)
        self.assertEqual(int(same_log.sum()), int(same_lin.sum()))

    def test_it_is_still_independent_of_the_chunk_count(self):
        macro, micro = a_small_stream(seed=43, n=900)
        log, lin = self.axes()
        ref = self.two(macro, micro, [400], 250, log, lin, n_chunks=1)
        for nc in (2, 5, 16):
            with self.subTest(n_chunks=nc):
                got = self.two(macro, micro, [400], 250, log, lin, n_chunks=nc)
                np.testing.assert_array_equal(got[0], ref[0])
                np.testing.assert_array_equal(got[1], ref[1])

    def test_passing_the_same_axis_twice_gives_the_same_matrix_twice(self):
        """The degenerate case, which a per-axis indexing slip would break."""
        macro, micro = a_small_stream(seed=47, n=400)
        log, _ = self.axes()
        a, b = self.two(macro, micro, [400], 200, log, log)
        np.testing.assert_array_equal(a, b)

    def test_a_descending_axis_is_rejected_on_either_side(self):
        macro, micro = a_small_stream(seed=53, n=60)
        log, lin = self.axes()
        bad = np.asarray([-1, 10, 5, 20], dtype=np.int64)
        with self.assertRaises(ValueError):
            self.two(macro, micro, [400], 200, bad, lin)
        with self.assertRaises(ValueError):
            self.two(macro, micro, [400], 200, log, bad)


class TestTheReferenceSpanRule(unittest.TestCase):
    """`t_Imax` comes from `TK_Create2DFDC_04.m`, and the log axis is built on it.

    The MATLAB is the authoritative implementation: where it and a port
    disagree, it wins (user ruling, 2026-08-11). Lines 38-40 read

        t_Imax    = ceil((tMax-tMin)/tStep) + lint_BinFactor
        lint_Imax = ceil(t_Imax / lint_BinFactor)
        t_Imax    = lint_Imax * lint_BinFactor

    and `Mat_2DFDC_logt` is built from that same `t_Imax`. So the *log* axis
    moves when the *linear* binning factor changes — which reads as a bug and is
    the reference's rule. The port originally hard-coded `span + 1`, which is
    only the factor-1 case.
    """

    @staticmethod
    def reference(span, factor):
        """The .m formula, transcribed independently of the C++."""
        padded = span + factor
        lint_imax = -(-padded // factor)          # ceil
        return lint_imax * factor

    def test_it_matches_the_matlab_formula(self):
        for span in (40, 255, 3000, 4095):
            for factor in (1, 2, 3, 5, 30, 256):
                with self.subTest(span=span, factor=factor):
                    self.assertEqual(tttrlib.fdc_t_imax(span, factor),
                                     self.reference(span, factor))

    def test_factor_one_is_the_old_span_plus_one(self):
        """The default, so a caller who never binned linearly sees no change."""
        for span in (40, 255, 3000, 4095):
            self.assertEqual(tttrlib.fdc_t_imax(span, 1), span + 1)

    def test_a_zero_or_negative_factor_is_rejected(self):
        with self.assertRaises(ValueError):
            tttrlib.fdc_t_imax(4095, 0)

    def test_the_factor_moves_the_log_axis_and_therefore_the_matrix(self):
        """The consequence worth pinning: this is not a cosmetic parameter.

        Same photons, same lag, same log bin count — a different linear binning
        factor gives a different log matrix, because the edges are built from a
        span that the factor rounds up. Equal totals, redistributed.
        """
        macro, micro = a_small_stream(seed=59, n=700)
        lags = np.asarray([500], dtype=np.int64)

        def at(factor):
            out = np.zeros(16 * 16, dtype=np.int64)
            tttrlib.fdc_scan_log(macro, micro, lags, 200, 0, 4095, 16, 4, out, factor)
            return out.reshape(16, 16)

        one, thirty = at(1), at(30)
        self.assertEqual(int(one.sum()), int(thirty.sum()),
                         "pairs should move between bins, not disappear")
        self.assertFalse(np.array_equal(one, thirty),
                         "the linear binning factor did not reach the log axis")

    def test_the_default_still_reproduces_the_plain_log_scan(self):
        macro, micro = a_small_stream(seed=61, n=500)
        lags = np.asarray([400], dtype=np.int64)
        explicit = np.zeros(16 * 16, dtype=np.int64)
        tttrlib.fdc_scan_log(macro, micro, lags, 200, 0, 4095, 16, 4, explicit, 1)
        np.testing.assert_array_equal(explicit.reshape(16, 16),
                                      scan(macro, micro, [400], ddT=200,
                                           t_max=4095, logt_imax=16)[0])


class TestTheGateIsTheReferenceGate(unittest.TestCase):
    """The reference admits photons *above* `t_max`, and nothing else here saw it.

    `TK_Create2DFDC_04.m:66` gates on `tauI >= t_Imax`, not on `tauI > tMax`.
    Since `t_Imax` rounds the span up to whole linear bins, the method keeps
    photons past `t_max` whenever the span is not a whole number of bins.

    This gap survived three independent checks — a fixture recorded from another
    implementation, a brute-force double loop, and a simulation suite — because
    **it changes nothing unless the data reaches past `t_max`**, and all three
    used gates their data never approached. It was found by a fourth party on a
    narrow gate at `lint_bin_factor = 4`. The lesson is in the test name: the
    gate is the reference's, and a test that never crowds it proves nothing
    about it.
    """

    #: gate [15, 25] with factor 4: t_Imax = 16 against a span of 10, so
    #: tau < 16 is admitted -- micro-times up to 30, five ticks past t_max.
    T_MIN, T_MAX, FACTOR = 15, 25, 4

    def stream(self, n=600, seed=67):
        rng = np.random.default_rng(seed)
        macro = np.sort(rng.integers(0, 40_000, size=n)).astype(np.int64)
        # deliberately crowds and exceeds t_max
        micro = rng.integers(self.T_MIN + 1, self.T_MIN + 20, size=n).astype(np.int64)
        return macro, micro

    def log_scan(self, macro, micro, factor, logt_imax=8):
        lags = np.asarray([300], dtype=np.int64)
        out = np.zeros(logt_imax * logt_imax, dtype=np.int64)
        tttrlib.fdc_scan_log(macro, micro, lags, 100, self.T_MIN, self.T_MAX,
                             logt_imax, 4, out, factor)
        return out.reshape(logt_imax, logt_imax)

    def test_the_factor_widens_the_gate_not_just_the_axis(self):
        """Factor 4 admits tau < 16; the physical gate would stop at 11."""
        macro, micro = self.stream()
        self.assertEqual(tttrlib.fdc_t_imax(self.T_MAX - self.T_MIN, self.FACTOR), 16)
        self.assertEqual(tttrlib.fdc_t_imax(self.T_MAX - self.T_MIN, 1), 11)

        wide = self.log_scan(macro, micro, self.FACTOR)
        narrow = self.log_scan(macro, micro, 1)
        self.assertGreater(int(wide.sum()), int(narrow.sum()),
                           "the reference gate should admit photons above t_max")

    def test_the_general_kernel_defaults_to_the_physical_gate(self):
        """A binning artefact is the wrong default for a caller not
        reproducing this paper, so `fdc_scan_axis` gates at t_max unless told."""
        macro, micro = self.stream()
        lags = np.asarray([300], dtype=np.int64)
        ticks = np.empty(9, dtype=np.int64)
        tttrlib.fdc_log_ticks(tttrlib.fdc_t_imax(self.T_MAX - self.T_MIN, self.FACTOR),
                              ticks)

        default = np.zeros(8 * 8, dtype=np.int64)
        tttrlib.fdc_scan_axis(macro, micro, lags, 100, self.T_MIN, self.T_MAX,
                              ticks, 4, default)

        widened = np.zeros(8 * 8, dtype=np.int64)
        tttrlib.fdc_scan_axis(macro, micro, lags, 100, self.T_MIN, self.T_MAX,
                              ticks, 4, widened,
                              tttrlib.fdc_t_imax(self.T_MAX - self.T_MIN, self.FACTOR))

        self.assertLess(int(default.sum()), int(widened.sum()),
                        "the explicit bound did not widen the gate")
        np.testing.assert_array_equal(widened.reshape(8, 8),
                                      self.log_scan(macro, micro, self.FACTOR))

    def test_a_negative_bound_is_rejected(self):
        macro, micro = self.stream(n=50)
        lags = np.asarray([300], dtype=np.int64)
        ticks = np.empty(9, dtype=np.int64)
        tttrlib.fdc_log_ticks(16, ticks)
        with self.assertRaises(ValueError):
            tttrlib.fdc_scan_axis(macro, micro, lags, 100, self.T_MIN, self.T_MAX,
                                  ticks, 4, np.zeros(8 * 8, dtype=np.int64), -1)


if __name__ == "__main__":
    unittest.main()
