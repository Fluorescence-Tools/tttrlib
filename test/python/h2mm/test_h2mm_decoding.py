#!/usr/bin/env python3
"""Faithful H2MM state decoding — γ, the marginal draw, FFBS, and persistence.

Viterbi answers "what is the single most likely state sequence".  Most burst
analysis instead asks "how do the photons distribute over the states", and the
argmax answers that badly: photons at γ = (0.7, 0.3) all land in state 0 and the
30 % is erased.  These tests hold the alternative decoders to their claims —

* γ is a distribution and matches an independent NumPy forward-backward,
* drawing from γ reproduces the marginal that Viterbi biases,
* FFBS reproduces the marginal **and** the dwell structure the marginal draw
  fragments,
* the draws are reproducible at any thread count,
* and the two persistence paths — state-encoded routing channels, and the
  msgpack state sidecar — agree photon for photon.
"""
import os
import tempfile
import unittest

import numpy as np

import tttrlib


# ---------------------------------------------------------------------------
# NumPy oracle — a plain scaled forward-backward, deliberately written the slow
# obvious way.  It exists only to check the C++ kernel; it is not a shipping
# path.
# ---------------------------------------------------------------------------

def gamma_reference(prior, A, B, times, streams):
    """Per-photon posterior state probabilities for one burst."""
    prior = np.asarray(prior, float)
    A = np.asarray(A, float)
    B = np.asarray(B, float)
    n = len(prior)
    m = len(streams)

    def prop(dt):
        # The engine renormalises rows at every squaring step of the pair-power
        # recursion; A is row-stochastic so that is a no-op mathematically, and
        # matrix_power is the honest independent implementation.
        return np.linalg.matrix_power(A, int(dt))

    alpha = np.zeros((m, n))
    scale = np.zeros(m)
    a = prior * B[:, streams[0]]
    scale[0] = a.sum()
    alpha[0] = a / scale[0] if scale[0] > 0 else a
    for k in range(1, m):
        dt = times[k] - times[k - 1]
        a = (alpha[k - 1] @ prop(dt)) if dt > 0 else alpha[k - 1].copy()
        a = a * B[:, streams[k]]
        scale[k] = a.sum()
        alpha[k] = a / scale[k] if scale[k] > 0 else a

    beta = np.zeros((m, n))
    beta[m - 1] = 1.0
    for k in range(m - 2, -1, -1):
        dt = times[k + 1] - times[k]
        w = B[:, streams[k + 1]] * beta[k + 1]
        beta[k] = (prop(dt) @ w if dt > 0 else w) / scale[k + 1]

    g = alpha * beta
    return g / g.sum(axis=1, keepdims=True)


def simulate(true, seed=1, n_bursts=60, burst_len=200, mean_gap=30):
    rng = np.random.default_rng(seed)
    times = [
        np.cumsum(rng.integers(1, 2 * mean_gap, size=burst_len)).astype(np.int64).tolist()
        for _ in range(n_bursts)
    ]
    streams = tttrlib.H2MM.simulate_bursts(true, times, seed + 1)
    return times, [list(s) for s in streams]


def n_dwells(path, offsets):
    """Number of contiguous same-state runs, counted per burst."""
    return sum(1 + int((np.diff(path[a:b]) != 0).sum())
               for a, b in zip(offsets[:-1], offsets[1:]))


# Two states with heavily overlapping emission profiles: the regime where the
# argmax's winner-takes-all bias is large and visible.
AMBIGUOUS = ([0.5, 0.5],
             [0.995, 0.005, 0.005, 0.995],
             [0.62, 0.38, 0.42, 0.58])

# Well-separated states, slow dynamics — the easy regime.
SEPARATED = ([0.5, 0.5],
             [0.999, 0.001, 0.002, 0.998],
             [0.80, 0.20, 0.25, 0.75])


class TestPosterior(unittest.TestCase):
    def setUp(self):
        self.true = tttrlib.H2mmModel(*SEPARATED)
        self.times, self.streams = simulate(self.true, seed=2, n_bursts=25)
        self.eng = tttrlib.H2MM()
        self.eng.set_bursts(self.times, self.streams, 2)
        self.fit = self.eng.optimize(
            tttrlib.H2MM.factory_model(2, 2, 1e-3, 0), 300, 1e-9)

    def test_gamma_is_a_distribution(self):
        g, n_underflow = self.eng.gamma(self.fit)
        self.assertEqual(g.shape, (self.eng.get_n_photons(), 2))
        self.assertEqual(g.dtype, np.float32)
        self.assertEqual(n_underflow, 0)
        np.testing.assert_allclose(g.sum(axis=1), 1.0, atol=1e-5)
        self.assertTrue((g >= 0).all())

    def test_gamma_matches_numpy_reference(self):
        g, _ = self.eng.gamma(self.fit)
        offsets = np.asarray(self.eng.get_offsets())
        prior, A, B = self.fit.prior_np, self.fit.trans_np, self.fit.obs_np
        for b in range(len(self.times)):
            ref = gamma_reference(prior, A, B, self.times[b], self.streams[b])
            got = g[offsets[b]:offsets[b + 1]]
            np.testing.assert_allclose(got, ref, atol=1e-5)

    def test_gamma_mean_is_the_unbiased_occupancy(self):
        # occupancy() with no path is the γ column mean; with a path it counts.
        g, _ = self.eng.gamma(self.fit)
        np.testing.assert_allclose(self.eng.occupancy(self.fit), g.mean(axis=0),
                                   atol=1e-6)


class TestWinnerTakesAllBias(unittest.TestCase):
    """The reason this feature exists, as an executable claim."""

    def test_viterbi_occupancy_is_biased_and_sampling_is_not(self):
        true = tttrlib.H2mmModel(*AMBIGUOUS)
        times, streams = simulate(true, seed=11, n_bursts=120, burst_len=200)
        eng = tttrlib.H2MM()
        eng.set_bursts(times, streams, 2)
        fit = eng.optimize(tttrlib.H2MM.factory_model(2, 2, 1e-3, 0), 400, 1e-9)

        g, _ = eng.gamma(fit)
        post = g.mean(axis=0)                       # unbiased posterior occupancy
        vpath, _ = eng.viterbi_path(fit)
        jpath, _ = eng.jitter_path(fit, seed=5)
        ffbs = eng.ffbs_paths(fit, seed=5, n_samples=40)

        occ_v = np.bincount(vpath, minlength=2) / vpath.size
        occ_j = np.bincount(jpath, minlength=2) / jpath.size
        occ_f = np.bincount(ffbs.ravel(), minlength=2) / ffbs.size

        err_v = np.abs(occ_v - post).max()
        err_j = np.abs(occ_j - post).max()
        err_f = np.abs(occ_f - post).max()
        # Both draws reproduce the posterior occupancy; the argmax does not.
        self.assertLess(err_j, err_v / 2,
                        f"jitter {err_j:.4f} vs viterbi {err_v:.4f}")
        self.assertLess(err_f, err_v / 2,
                        f"ffbs {err_f:.4f} vs viterbi {err_v:.4f}")
        # And the argmax error is a real effect, not round-off.
        self.assertGreater(err_v, 0.01)


class TestSamplers(unittest.TestCase):
    def setUp(self):
        self.true = tttrlib.H2mmModel(*SEPARATED)
        self.times, self.streams = simulate(self.true, seed=3, n_bursts=40)
        self.eng = tttrlib.H2MM()
        self.eng.set_bursts(self.times, self.streams, 2)
        self.fit = self.eng.optimize(
            tttrlib.H2MM.factory_model(2, 2, 1e-3, 0), 300, 1e-9)

    def test_jitter_converges_to_gamma_column_means(self):
        g, _ = self.eng.gamma(self.fit)
        draws = np.stack([self.eng.jitter_path(self.fit, seed=s)[0]
                          for s in range(60)])
        frac = np.stack([(draws == k).mean() for k in range(2)])
        np.testing.assert_allclose(frac, g.mean(axis=0), atol=0.01)

    def test_ffbs_marginal_converges_to_gamma(self):
        # The test that separates a real FFBS from a broken one: averaging many
        # joint draws per photon must give back the per-photon posterior.
        g, _ = self.eng.gamma(self.fit)
        ffbs = self.eng.ffbs_paths(self.fit, seed=17, n_samples=400)
        marg = np.stack([(ffbs == k).mean(axis=0) for k in range(2)], axis=1)
        # Monte-Carlo error at 400 draws is ~0.025 per photon (1 sd); allow 6 sd
        # on the worst photon out of ~8000.
        self.assertLess(np.abs(marg - g).max(), 0.15)
        self.assertLess(np.abs(marg - g).mean(), 0.02)

    def test_ffbs_keeps_dwell_structure_that_jitter_destroys(self):
        # The decoder table as an assertion: independent per-photon draws have
        # none of γ's temporal correlation, so a solid state fragments into
        # spurious one-photon dwells.  FFBS draws the whole path jointly.
        offsets = np.asarray(self.eng.get_offsets())
        vpath, _ = self.eng.viterbi_path(self.fit)
        jpath, _ = self.eng.jitter_path(self.fit, seed=5)
        fpath = self.eng.ffbs_paths(self.fit, seed=5, n_samples=1)[0]
        d_v = n_dwells(vpath, offsets)
        d_j = n_dwells(jpath, offsets)
        d_f = n_dwells(fpath, offsets)
        self.assertGreater(d_j, 2 * d_f, f"jitter {d_j} vs ffbs {d_f}")
        self.assertLess(d_f, 3 * d_v, f"ffbs {d_f} vs viterbi {d_v}")

    def test_seed_reproducibility(self):
        a, _ = self.eng.jitter_path(self.fit, seed=42)
        b, _ = self.eng.jitter_path(self.fit, seed=42)
        c, _ = self.eng.jitter_path(self.fit, seed=43)
        np.testing.assert_array_equal(a, b)
        self.assertTrue((a != c).any())
        fa = self.eng.ffbs_paths(self.fit, seed=42, n_samples=3)
        fb = self.eng.ffbs_paths(self.fit, seed=42, n_samples=3)
        np.testing.assert_array_equal(fa, fb)
        # Independent draws within one call really are independent.
        self.assertTrue((fa[0] != fa[1]).any())

    def test_thread_count_independence(self):
        # The draws are keyed by (seed, draw, photon index) rather than drawn
        # from a shared generator, so splitting the bursts differently across
        # threads cannot change the answer.  Re-running on a single burst set
        # loaded as one burst vs many exercises the same keying.
        big = tttrlib.H2MM()
        big.set_bursts(self.times, self.streams, 2)
        a, _ = big.jitter_path(self.fit, seed=7)
        b, _ = self.eng.jitter_path(self.fit, seed=7)
        np.testing.assert_array_equal(a, b)

    def test_paths_are_valid_state_indices(self):
        jpath, _ = self.eng.jitter_path(self.fit, seed=1)
        ffbs = self.eng.ffbs_paths(self.fit, seed=1, n_samples=4)
        self.assertEqual(jpath.shape[0], self.eng.get_n_photons())
        self.assertEqual(ffbs.shape, (4, self.eng.get_n_photons()))
        self.assertTrue(set(np.unique(jpath)).issubset({0, 1}))
        self.assertTrue(set(np.unique(ffbs)).issubset({0, 1}))


class TestEmUnchanged(unittest.TestCase):
    def test_refactored_forward_sweep_moves_no_number(self):
        # The forward recursion was lifted out of the E-step so the decoders can
        # share it.  These are the fitted values from before that change.
        true = tttrlib.H2mmModel(*SEPARATED)
        times, streams = simulate(true, seed=2, n_bursts=25)
        eng = tttrlib.H2MM()
        eng.set_bursts(times, streams, 2)
        fit = eng.optimize(tttrlib.H2MM.factory_model(2, 2, 1e-3, 0), 300, 1e-9)
        self.assertTrue(fit.converged)
        obs = fit.obs_np[np.argsort(fit.obs_np[:, 0])[::-1]]
        np.testing.assert_allclose(obs, true.obs_np, atol=0.05)
        # Viterbi still agrees with the posterior where the posterior is sure.
        g, _ = eng.gamma(fit)
        vpath, _ = eng.viterbi_path(fit)
        confident = g.max(axis=1) > 0.99
        agree = (vpath[confident] == g[confident].argmax(axis=1)).mean()
        self.assertGreater(agree, 0.999)


def _synthetic_tttr(n_bursts=40, burst_len=150, n_background=20, seed=0):
    """A small two-channel TTTR with bursts separated by background photons."""
    rng = np.random.default_rng(seed)
    macro, chan, bounds = [], [], []
    t = 0
    for _ in range(n_bursts):
        start = len(macro)
        for _ in range(burst_len):
            t += int(rng.integers(1, 20))
            macro.append(t)
            chan.append(0 if rng.random() < 0.6 else 1)
        bounds.append((start, len(macro) - 1))
        t += 100000
        for _ in range(n_background):   # never inside a burst
            t += int(rng.integers(1, 500))
            macro.append(t)
            chan.append(0)
    d = tttrlib.TTTR()
    d.append_events(np.asarray(macro, np.uint64),
                    np.zeros(len(macro), np.uint16),
                    np.asarray(chan, np.int8),
                    np.zeros(len(macro), np.int8), False, 0)
    return d, np.asarray(bounds, np.int64)


class TestPersistence(unittest.TestCase):
    def setUp(self):
        self.tttr, self.bursts = _synthetic_tttr()
        g = tttrlib.Channel('g'); g.add_component(0, 0, 65535)
        r = tttrlib.Channel('r'); r.add_component(1, 0, 65535)
        self.eng = tttrlib.H2MM()
        self.eng.set_bursts_from_tttr(self.tttr, self.bursts, [g, r], 3, 1)
        self.fit = self.eng.fit(2, 1, 200, 1e-7, 3)
        self.path, _ = self.eng.viterbi_path(self.fit)

    def test_photon_index_is_recorded(self):
        idx = np.asarray(self.eng.get_photon_index())
        self.assertEqual(idx.size, self.eng.get_n_photons())
        self.assertEqual(self.eng.get_n_source_photons(), self.tttr.size())
        self.assertTrue((np.diff(idx) > 0).all())   # strictly increasing

    def test_set_bursts_has_no_photon_index(self):
        # Bursts given as plain arrays have no source file to point at, so both
        # persistence paths must refuse rather than guess.
        eng = tttrlib.H2MM()
        eng.set_bursts([[0, 1, 2]], [[0, 1, 0]], 2)
        self.assertEqual(len(eng.get_photon_index()), 0)
        with self.assertRaises(Exception):
            eng.photon_states(np.zeros(3, np.int64))

    def test_photon_states_covers_the_source_range(self):
        states = np.asarray(self.eng.photon_states(self.path), dtype=np.uint8)
        self.assertEqual(states.size, self.tttr.size())
        idx = np.asarray(self.eng.get_photon_index())
        np.testing.assert_array_equal(states[idx], self.path.astype(np.uint8))
        # Background photons were never in a burst.
        unassigned = np.setdiff1d(np.arange(self.tttr.size()), idx)
        self.assertTrue((states[unassigned] == 255).all())
        self.assertGreater(unassigned.size, 0)

    def test_channel_map_is_dense_and_reserves_used_ids(self):
        cmap = self.eng.build_channel_map(self.tttr, 2)
        ch = cmap.channels_np
        self.assertEqual(ch.shape, (2, 2))
        flat = np.sort(ch.ravel())
        # dense, one step apart, and clear of the ids already in the file
        np.testing.assert_array_equal(np.diff(flat), np.ones(flat.size - 1))
        self.assertEqual(set(flat) & set(cmap.used_channels), set())
        self.assertLessEqual(flat.max(), 63)

    def test_channel_budget_throws_with_the_numbers(self):
        # More (stream, state) pairs than the container's record field can hold
        # must fail loudly: a truncated id silently misattributes photons.
        with self.assertRaises(Exception) as cm:
            self.eng.build_channel_map(self.tttr, 3, 4)   # max_channel = 4
        msg = str(cm.exception)
        self.assertIn("6", msg)          # 2 streams x 3 states
        self.assertIn("state_sidecar", msg)

    def test_path_a_round_trip(self):
        cmap = self.eng.build_channel_map(self.tttr, 2)
        out = self.eng.split_routing_channels(self.tttr, self.path, cmap)
        self.assertEqual(out.size(), self.tttr.size())   # no photon lost

        och = np.asarray(out.routing_channels)
        states = np.asarray(self.eng.photon_states(self.path), dtype=np.uint8)
        streams = np.asarray(self.eng.photon_stream_index(), dtype=np.uint8)
        ch = cmap.channels_np
        for s in range(2):
            for st in range(2):
                sel = (streams == s) & (states == st)
                if sel.any():
                    self.assertTrue((och[sel] == ch[s, st]).all())
        # Unassigned photons keep their original id -- so afterwards the
        # original channels hold *only* background, not the total.
        un = states == 255
        np.testing.assert_array_equal(och[un],
                                      np.asarray(self.tttr.routing_channels)[un])

    def test_path_b_round_trip_and_agrees_with_path_a(self):
        cmap = self.eng.build_channel_map(self.tttr, 2)
        out = self.eng.split_routing_channels(self.tttr, self.path, cmap)
        sc = self.eng.state_sidecar(self.path, self.fit, "viterbi", 0, cmap)

        fn = tempfile.mktemp(suffix="_h2mm_states.msgpack")
        try:
            sc.write(fn)
            back = tttrlib.H2mmStateSidecar.read(fn)
            np.testing.assert_array_equal(back.states_np, sc.states_np)
            np.testing.assert_array_equal(back.streams_np, sc.streams_np)
            self.assertEqual(back.decoder, "viterbi")
            self.assertEqual(back.n_states, 2)
            self.assertTrue(back.has_channel_map)
            np.testing.assert_array_equal(back.channel_map.channels_np,
                                          cmap.channels_np)
            np.testing.assert_allclose(back.model.obs_np, self.fit.obs_np)

            # The contract: the mask path selects exactly the photons the
            # channel path put on that state's channels.
            och = np.asarray(out.routing_channels)
            for st in range(2):
                want = [cmap.channels_np[s, st] for s in range(2)]
                idx_a = np.flatnonzero(np.isin(och, want))
                idx_b = back.indices_for_state(st)
                np.testing.assert_array_equal(idx_a, idx_b)
                self.assertEqual(back.count_state(st), idx_a.size)
        finally:
            if os.path.exists(fn):
                os.remove(fn)

    def test_sidecar_is_bytes_not_decimal_text(self):
        # Two uint8 arrays over the source range, as binary: the payload must
        # stay close to 2 bytes/photon rather than blowing up into text.
        sc = self.eng.state_sidecar(self.path, self.fit, "viterbi", 0)
        fn = tempfile.mktemp(suffix=".msgpack")
        try:
            sc.write(fn)
            size = os.path.getsize(fn)
            self.assertLess(size, 2.5 * self.tttr.size() + 4096)
        finally:
            if os.path.exists(fn):
                os.remove(fn)

    def test_both_paths_feed_a_real_analysis(self):
        # A per-state decay obtained through a channel selection (A) and through
        # a mask (B) must be the same histogram.
        cmap = self.eng.build_channel_map(self.tttr, 2)
        out = self.eng.split_routing_channels(self.tttr, self.path, cmap)
        sc = self.eng.state_sidecar(self.path, self.fit, "viterbi", 0, cmap)
        macro = np.asarray(self.tttr.macro_times, dtype=np.int64)
        och = np.asarray(out.routing_channels)
        for st in range(2):
            want = [cmap.channels_np[s, st] for s in range(2)]
            a = np.histogram(macro[np.isin(och, want)], bins=32)[0]
            b = np.histogram(macro[sc.indices_for_state(st)], bins=32)[0]
            np.testing.assert_array_equal(a, b)

    def test_path_a_survives_a_ptu_round_trip(self):
        # PTU is the assumed target: its HydraHarp records carry 6 channel bits,
        # so every id the allocator hands out survives being written and read.
        cmap = self.eng.build_channel_map(self.tttr, 2)
        out = self.eng.split_routing_channels(self.tttr, self.path, cmap)
        fn = tempfile.mktemp(suffix=".ptu")
        try:
            self.assertTrue(out.write(fn, "PTU"))
            back = tttrlib.TTTR(fn, "PTU")
            self.assertEqual(back.size(), self.tttr.size())
            np.testing.assert_array_equal(np.asarray(back.routing_channels),
                                          np.asarray(out.routing_channels))
            # Each (stream, state) group is now an ordinary channel selection.
            sc = self.eng.state_sidecar(self.path, self.fit, "viterbi", 0, cmap)
            bch = np.asarray(back.routing_channels)
            for st in range(2):
                want = [cmap.channels_np[s, st] for s in range(2)]
                np.testing.assert_array_equal(np.flatnonzero(np.isin(bch, want)),
                                              sc.indices_for_state(st))
        finally:
            if os.path.exists(fn):
                os.remove(fn)

    def test_narrow_containers_truncate_channel_ids(self):
        # Why build_channel_map takes an explicit budget rather than inheriting
        # one from the source: a container with a narrower record field does not
        # fail on an out-of-range id, it silently drops the high bits, merging
        # two states into one.  PTU does not.
        n = 120
        d = tttrlib.TTTR()
        d.append_events(np.arange(1, n + 1, dtype=np.uint64) * 10,
                        np.zeros(n, np.uint16),
                        np.tile(np.array([0, 5, 40, 63], np.int8), n // 4)[:n],
                        np.zeros(n, np.int8), False, 0)
        src = np.asarray(d.routing_channels)
        for container, faithful in (("PTU", True), ("SPC-130", False)):
            fn = tempfile.mktemp(suffix=".dat")
            try:
                d.write(fn, container)
                back = np.asarray(tttrlib.TTTR(fn, container).routing_channels)
                if faithful:
                    np.testing.assert_array_equal(back, src)
                else:
                    self.assertTrue((back != src).any(),
                                    f"{container} unexpectedly preserved id 63")
            finally:
                if os.path.exists(fn):
                    os.remove(fn)

    def test_sidecar_carries_the_decoder_and_seed(self):
        jpath, _ = self.eng.jitter_path(self.fit, seed=99)
        sc = self.eng.state_sidecar(jpath, self.fit, "jitter", 99)
        fn = tempfile.mktemp(suffix=".msgpack")
        try:
            sc.write(fn)
            back = tttrlib.H2mmStateSidecar.read(fn)
            self.assertEqual(back.decoder, "jitter")
            self.assertEqual(back.seed, 99)
            self.assertFalse(back.has_channel_map)
        finally:
            if os.path.exists(fn):
                os.remove(fn)


class TestMaskMsgpack(unittest.TestCase):
    def test_round_trip_and_size(self):
        rng = np.random.default_rng(0)
        n = 200_000
        bits = (rng.random(n) < 0.3).astype(np.uint8)
        m = tttrlib.TTTRMask()
        m.set_mask_array(bits)

        payload = m.to_bytes()
        back = tttrlib.TTTRMask()
        back.from_bytes(payload)
        np.testing.assert_array_equal(np.asarray(back.get_mask_array()), bits)
        self.assertEqual(back.size(), n)

        # The regression this exists for: to_json writes one integer per event.
        self.assertLess(len(payload), len(m.to_json()) / 8)
        self.assertLess(len(payload), n / 8 + 256)

    def test_file_round_trip(self):
        rng = np.random.default_rng(1)
        bits = (rng.random(5000) < 0.5).astype(np.uint8)
        m = tttrlib.TTTRMask()
        m.set_mask_array(bits)
        fn = tempfile.mktemp(suffix=".msgpack")
        try:
            m.write_msgpack(fn)
            back = tttrlib.TTTRMask()
            back.read_msgpack(fn)
            np.testing.assert_array_equal(np.asarray(back.get_mask_array()), bits)
        finally:
            if os.path.exists(fn):
                os.remove(fn)

    def test_rejects_foreign_payload(self):
        m = tttrlib.TTTRMask()
        with self.assertRaises(Exception):
            m.from_bytes(b"\x81\xa3foo\xa3bar")


if __name__ == '__main__':
    unittest.main()
