#!/usr/bin/env python3
"""ALEX (alternating laser excitation) analysis with Shimon Weiss lab ``.sm`` files.

Shimon Weiss lab single-molecule ``.sm`` files store a macro-time and a routing channel per
photon but no micro-time. Micro-second ALEX encodes the excitation alternation
in the macro-time clock: within each alternation period the green laser is on
for one interior window and the red laser for another, separated by rise/fall
gaps. ``alex_to_microtime`` folds the macro-time into a synthetic micro-time
(``(macro - shift) % period``) so the excitation window can be recovered by
micro-time gating, exactly like a pulsed-interleaved (PIE) measurement.

The two laser-on periods show up as two occupied plateaus in the folded-phase
histogram; ``auto_alex_windows`` finds them and guard-bands their edges to drop
the laser rise/fall photons (some photon loss is expected). The tests build a
ground-truth ALEX stream, round-trip it through the ``.sm`` container, and check
that the auto-detected gating recovers the injected ``E`` and ``S``. A final
test exercises the real ``sm/data.sm`` reference file.
"""
import unittest

import numpy as np

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# .sm container/record ids and macro-time clock (12.5 ns).
SM_CONTAINER = 7
SM_RECORD_TYPE = 11
MACRO_RESOLUTION = 1.25e-8
TY_FLOAT8 = 536870920  # tttrlib tag type for an 8-byte float

# ALEX alternation: PERIOD macro units per cycle. The lasers are on for interior
# windows with rise/fall gaps between them. Kept < 65535 because
# alex_to_microtime casts the phase to unsigned short.
ALEX_PERIOD = 8000
GREEN_WINDOW = (300, 3700)
RED_WINDOW = (4300, 7700)

# Detector routing: donor ("green") = 0, acceptor ("red") = 1.
CH_DONOR = 0
CH_ACCEPTOR = 1


def simulate_alex_sm(path, populations, *, seed=1, smear=0.08):
    """Write a synthetic micro-second ALEX photon stream to an ``.sm`` file.

    Parameters
    ----------
    path : str
        Output ``.sm`` file path.
    populations : list of dict
        Each dict has ``E`` (FRET efficiency), ``S`` (stoichiometry) and ``n``
        (number of bursts).
    seed : int
        Seed for the random generator.
    smear : float
        Fraction of each stream's photons smeared across the laser window edges,
        mimicking the laser rise/fall.

    Returns
    -------
    int
        The number of simulated bursts (molecules).
    """
    rng = np.random.RandomState(seed)
    macro, chan = [], []
    t = np.uint64(0)
    n_bursts = 0

    def place(cnt, det, window):
        lo, hi = window
        base = t + rng.randint(0, 12000, cnt).astype(np.uint64)
        ph = lo + rng.randint(0, hi - lo, cnt)
        n_smear = int(smear * cnt)
        if n_smear:
            idx = rng.choice(cnt, n_smear, replace=False)
            edge = rng.choice([lo, hi], n_smear)
            ph[idx] = (edge + rng.randint(-150, 150, n_smear)) % ALEX_PERIOD
        ph = ph.astype(np.uint64)
        cycle = (base // np.uint64(ALEX_PERIOD)) * np.uint64(ALEX_PERIOD)
        return cycle + ph, np.full(cnt, det, np.int8)

    for pop in populations:
        E, S = pop["E"], pop["S"]
        for _ in range(pop["n"]):
            n_bursts += 1
            t = t + np.uint64(rng.randint(120_000, 260_000))
            size = 40 + rng.poisson(120)
            # Split into excitation windows (green vs red) then, within the
            # green window, into donor vs acceptor (FRET) emission.
            n_green = rng.binomial(size, S)
            n_red = size - n_green
            n_da = rng.binomial(n_green, E)
            n_dd = n_green - n_da
            for cnt, det, window in [
                (n_dd, CH_DONOR, GREEN_WINDOW),      # donor det, green exc
                (n_da, CH_ACCEPTOR, GREEN_WINDOW),   # acceptor det, green exc
                (n_red, CH_ACCEPTOR, RED_WINDOW),    # acceptor det, red exc
            ]:
                if cnt == 0:
                    continue
                m, c = place(cnt, det, window)
                macro.append(m)
                chan.append(c)
            t = t + np.uint64(12000)

    macro = np.concatenate(macro)
    chan = np.concatenate(chan)
    order = np.argsort(macro, kind="stable")
    macro = macro[order].astype(np.uint64)
    chan = chan[order].astype(np.int8)

    d = tttrlib.TTTR()
    d.append_events(
        macro,
        np.zeros(len(macro), np.uint16),
        chan,
        np.zeros(len(macro), np.int8),
    )
    d.header.tttr_container_type = SM_CONTAINER
    d.header.tttr_record_type = SM_RECORD_TYPE
    # SM stores the macro-time clock as the global resolution; set it so the
    # written file reads back with a physical time base.
    d.header.set_tag("MeasDesc_GlobalResolution", MACRO_RESOLUTION, TY_FLOAT8)
    assert d.write(path)
    return n_bursts


def _contiguous_runs(mask):
    """(start, stop_inclusive) True-runs on a circular boolean array."""
    mask = np.asarray(mask, dtype=bool)
    n = len(mask)
    if n == 0 or not mask.any():
        return []
    if mask.all():
        return [(0, n - 1)]
    offset = int(np.argmin(mask))
    rolled = np.roll(mask, -offset)
    runs, i = [], 0
    while i < n:
        if rolled[i]:
            j = i
            while j < n and rolled[j]:
                j += 1
            runs.append(((i + offset) % n, (j - 1 + offset) % n))
            i = j
        else:
            i += 1
    return runs


def auto_alex_windows(phase, rc, donor_ch, acceptor_ch, period,
                      n_bins=200, guard=0.06, occupancy=0.35):
    """Detect the green/red ALEX windows from the folded-phase distribution.

    Returns ``{"green": (lo, hi), "red": (lo, hi)}`` with edges guard-band
    trimmed. Raises ``ValueError`` if two laser plateaus cannot be found.
    """
    phase = np.asarray(phase)
    rc = np.asarray(rc)
    edges = np.linspace(0, period, n_bins + 1)
    counts, _ = np.histogram(phase, bins=edges)
    plateau = np.percentile(counts[counts > 0], 75)
    runs = _contiguous_runs(counts > occupancy * plateau)

    def run_counts(run):
        s, e = run
        return (counts[s:e + 1].sum() if s <= e
                else counts[s:].sum() + counts[:e + 1].sum())

    runs = sorted(runs, key=run_counts, reverse=True)
    if len(runs) < 2:
        raise ValueError("could not find two ALEX laser windows")
    windows = []
    for s, e in runs[:2]:
        lo = float(edges[s])
        hi = float(edges[e + 1]) if e + 1 < len(edges) else float(period)
        margin = guard * (hi - lo)
        windows.append((lo + margin, hi - margin))

    def donor_density(win):
        lo, hi = win
        sel = (phase >= lo) & (phase < hi) & np.isin(rc, donor_ch)
        return sel.sum() / max(hi - lo, 1.0)

    windows.sort(key=donor_density, reverse=True)
    return {"green": windows[0], "red": windows[1]}


def alex_es_per_burst(data, bursts, windows):
    """Count the three ALEX photon streams per burst and return ``E``/``S``.

    ``data`` must already have been folded with ``alex_to_microtime`` so the
    micro-time carries the ALEX phase; ``windows`` is from ``auto_alex_windows``.
    """
    mt = np.asarray(data.micro_times)
    rc = np.asarray(data.routing_channels)
    g_lo, g_hi = windows["green"]
    r_lo, r_hi = windows["red"]
    green = (mt >= g_lo) & (mt < g_hi)
    red = (mt >= r_lo) & (mt < r_hi)
    i_dd = np.zeros(len(bursts))
    i_da = np.zeros(len(bursts))
    i_aa = np.zeros(len(bursts))
    for k, (s, e) in enumerate(bursts):
        sl = slice(int(s), int(e) + 1)
        g = green[sl]
        r = red[sl]
        c = rc[sl]
        i_dd[k] = np.count_nonzero(g & (c == CH_DONOR))
        i_da[k] = np.count_nonzero(g & (c == CH_ACCEPTOR))
        i_aa[k] = np.count_nonzero(r & (c == CH_ACCEPTOR))
    total_green = i_dd + i_da
    E = np.divide(i_da, total_green, out=np.zeros_like(i_da),
                  where=total_green > 0)
    S = np.divide(total_green, total_green + i_aa,
                  out=np.zeros_like(total_green), where=(total_green + i_aa) > 0)
    return E, S, (i_dd, i_da, i_aa)


class TestAlexToMicroTime(unittest.TestCase):
    """Semantics of the macro-time -> ALEX-phase folding."""

    def test_folding_matches_modulo(self):
        macro = np.arange(0, 50_000, 7, dtype=np.uint64)
        d = tttrlib.TTTR()
        d.append_events(
            macro,
            np.zeros(len(macro), np.uint16),
            np.zeros(len(macro), np.int8),
            np.zeros(len(macro), np.int8),
        )
        d.alex_to_microtime(ALEX_PERIOD, 0)
        np.testing.assert_array_equal(
            np.asarray(d.micro_times),
            (macro % ALEX_PERIOD).astype(np.asarray(d.micro_times).dtype),
        )

    def test_period_shift(self):
        macro = np.arange(100, 40_000, 11, dtype=np.uint64)
        d = tttrlib.TTTR()
        d.append_events(
            macro,
            np.zeros(len(macro), np.uint16),
            np.zeros(len(macro), np.int8),
            np.zeros(len(macro), np.int8),
        )
        shift = 37
        d.alex_to_microtime(ALEX_PERIOD, shift)
        expected = ((macro.astype(np.int64) - shift) % ALEX_PERIOD)
        np.testing.assert_array_equal(
            np.asarray(d.micro_times),
            expected.astype(np.asarray(d.micro_times).dtype),
        )


class TestAlexSmRoundTrip(unittest.TestCase):
    """Synthetic ALEX stream survives a round-trip through the .sm container."""

    def test_write_read_preserves_stream(self):
        import os
        import tempfile

        fn = tempfile.mktemp(suffix=".sm")
        try:
            simulate_alex_sm(fn, [dict(E=0.5, S=0.5, n=50)], seed=3)
            d = tttrlib.TTTR(fn, "SM")
            self.assertEqual(d.get_tttr_container_type(), "SM")
            self.assertGreater(len(d), 0)
            self.assertAlmostEqual(
                d.header.macro_time_resolution, MACRO_RESOLUTION)
            # Only two detectors were written.
            self.assertEqual(
                set(np.unique(d.routing_channels)),
                {CH_DONOR, CH_ACCEPTOR},
            )
            # SM carries no micro-time until folded.
            self.assertEqual(int(np.asarray(d.micro_times).max()), 0)
        finally:
            if os.path.isfile(fn):
                os.unlink(fn)


class TestAutoAlexWindows(unittest.TestCase):
    """Automatic window detection from the folded-phase distribution."""

    def test_recovers_laser_windows(self):
        import os
        import tempfile

        fn = tempfile.mktemp(suffix=".sm")
        try:
            simulate_alex_sm(fn, [dict(E=0.3, S=0.55, n=300)], seed=2)
            data = tttrlib.TTTR(fn, "SM")
            data.alex_to_microtime(ALEX_PERIOD, 0)
            win = auto_alex_windows(
                data.micro_times, data.routing_channels,
                [CH_DONOR], [CH_ACCEPTOR], ALEX_PERIOD, guard=0.06)
            g_lo, g_hi = win["green"]
            r_lo, r_hi = win["red"]
            # Detected windows sit inside the true laser windows and never reach
            # into the rise/fall gaps.
            self.assertTrue(GREEN_WINDOW[0] <= g_lo < g_hi <= GREEN_WINDOW[1] + 1)
            self.assertTrue(RED_WINDOW[0] <= r_lo < r_hi <= RED_WINDOW[1] + 1)
            # Edges trimmed by the guard band.
            self.assertGreater(g_lo - GREEN_WINDOW[0], 50)
            self.assertGreater(RED_WINDOW[1] - r_hi, 50)
        finally:
            if os.path.isfile(fn):
                os.unlink(fn)

    def test_raises_on_continuous_wave(self):
        # A single fully-occupied period (no alternation) is not ALEX.
        rng = np.random.RandomState(0)
        n = 20000
        phase = rng.randint(0, ALEX_PERIOD, n)
        rc = rng.randint(0, 2, n).astype(np.int8)
        with self.assertRaises(ValueError):
            auto_alex_windows(phase, rc, [CH_DONOR], [CH_ACCEPTOR], ALEX_PERIOD)


class TestAlexEsRecovery(unittest.TestCase):
    """Full ALEX pipeline recovers the injected E and S from an .sm file."""

    def test_recover_two_populations(self):
        import os
        import tempfile

        populations = [
            dict(E=0.20, S=0.55, n=300),
            dict(E=0.80, S=0.55, n=300),
        ]
        fn = tempfile.mktemp(suffix=".sm")
        try:
            n_sim = simulate_alex_sm(fn, populations, seed=1)

            data = tttrlib.TTTR(fn, "SM")
            # Fold the alternation into the micro-time, then auto-detect windows.
            data.alex_to_microtime(ALEX_PERIOD, 0)
            # Folded phase is bounded by the period.
            self.assertLess(int(np.asarray(data.micro_times).max()), ALEX_PERIOD)
            win = auto_alex_windows(
                data.micro_times, data.routing_channels,
                [CH_DONOR], [CH_ACCEPTOR], ALEX_PERIOD)

            # All-photon burst search.
            bursts = np.asarray(
                data.burst_search(L=40, m=10, T=1.0e-3, mode="sliding_window")
            ).reshape(-1, 2)
            # One burst per simulated molecule (well separated in time).
            self.assertEqual(len(bursts), n_sim)

            E, S, _ = alex_es_per_burst(data, bursts, win)

            self.assertAlmostEqual(float(S.mean()), 0.55, delta=0.04)
            low = E[E < 0.5]
            high = E[E >= 0.5]
            self.assertEqual(len(low), 300)
            self.assertEqual(len(high), 300)
            self.assertAlmostEqual(float(low.mean()), 0.20, delta=0.03)
            self.assertAlmostEqual(float(high.mean()), 0.80, delta=0.03)
        finally:
            if os.path.isfile(fn):
                os.unlink(fn)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestRealSmFile(unittest.TestCase):
    """The reference sm/data.sm file loads, round-trips, and burst-searches."""

    def test_load_and_burst_search(self):
        import os

        fn = settings["sm_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        data = tttrlib.TTTR(fn, "SM")
        self.assertEqual(data.get_tttr_container_type(), "SM")
        self.assertGreater(len(data), 0)
        # .sm carries macro time + routing channel, no micro time.
        self.assertEqual(int(np.asarray(data.micro_times).max()), 0)
        self.assertAlmostEqual(data.header.macro_time_resolution,
                               MACRO_RESOLUTION, places=12)
        # Two-detector confocal stream; folding + burst search must run.
        data.alex_to_microtime(ALEX_PERIOD, 0)
        bursts = np.asarray(
            data.burst_search(L=40, m=10, T=1.0e-3, mode="sliding_window")
        ).reshape(-1, 2)
        self.assertGreaterEqual(len(bursts), 0)

    def test_sm_roundtrip_lossless(self):
        """Read -> write -> read the real .sm file: macro time + routing preserved exactly."""
        import os
        import tempfile

        fn = settings["sm_filename"]
        if not os.path.isfile(fn):
            self.skipTest("missing data file: %s" % fn)
        d = tttrlib.TTTR(fn, "SM")
        out = tempfile.mktemp(suffix=".sm")
        try:
            self.assertTrue(d.write(out))
            d2 = tttrlib.TTTR(out, "SM")
            np.testing.assert_array_equal(d.macro_times, d2.macro_times)
            np.testing.assert_array_equal(d.routing_channels, d2.routing_channels)
            self.assertAlmostEqual(d2.header.macro_time_resolution,
                                   MACRO_RESOLUTION, places=12)
        finally:
            if os.path.isfile(out):
                os.unlink(out)


if __name__ == "__main__":
    unittest.main()
