"""Tests for the lazy bitmask-primary fill of CLSMImage.

fill() stores a packed per-event acceptance bitmask; per-pixel photon index
vectors are materialized from it only when accessed. The public API behaves
exactly as before.
"""
from __future__ import division

import unittest

import tttrlib
import numpy as np


LINE_LENGTH = 800
GAP = 100


def make_clsm_tttr(n_frames, n_lines, n_pixel, photons_per_line, seed=1):
    rng = np.random.default_rng(seed)
    times, chans, types, micros = [], [], [], []
    t = 0
    for _f in range(n_frames):
        times.append([t]); chans.append([1]); types.append([1]); micros.append([0])
        t += GAP
        for _l in range(n_lines):
            ph = np.sort(rng.integers(0, LINE_LENGTH, photons_per_line)) + t
            pc = rng.choice([1, 2], photons_per_line)
            pm = rng.integers(0, 4096, photons_per_line)
            times.append([t]); chans.append([3]); types.append([1]); micros.append([0])
            times.append(ph); chans.append(pc)
            types.append(np.zeros(photons_per_line, dtype=np.int64)); micros.append(pm)
            times.append([t + LINE_LENGTH]); chans.append([2]); types.append([1]); micros.append([0])
            t += LINE_LENGTH + GAP
    times.append([t]); chans.append([1]); types.append([1]); micros.append([0])
    macro = np.concatenate([np.asarray(x, dtype=np.uint64) for x in times])
    chan = np.concatenate([np.asarray(x, dtype=np.int8) for x in chans])
    etyp = np.concatenate([np.asarray(x, dtype=np.int8) for x in types])
    micro = np.concatenate([np.asarray(x, dtype=np.uint16) for x in micros])
    tttr = tttrlib.TTTR(macro, micro, chan, etyp)
    tttr.header.set_number_of_micro_time_channels(4096)
    return tttr


def make_img(tttr, n_lines, n_pixel, **kw):
    settings = {"n_lines": n_lines}
    settings.update(kw.pop("settings", {}))
    return tttrlib.CLSMImage(
        tttr_data=tttr,
        marker_frame_start=[1], marker_line_start=3, marker_line_stop=2,
        marker_event_type=1, n_pixel_per_line=n_pixel,
        reading_routine="default", skip_before_first_frame_marker=True,
        settings=settings, **kw
    )


class TestCLSMLazyFill(unittest.TestCase):
    def test_intensity_does_not_materialize(self):
        """After fill + intensity, the photon-index memory stays at mask size."""
        tttr = make_clsm_tttr(4, 32, 32, 200)
        img = make_img(tttr, 32, 32)
        img.fill(tttr_data=tttr, channels=[1, 2])
        _ = img.intensity

        overhead, indices, ranges = img.get_memory_usage_detailed()
        n_events = len(np.asarray(tttr.macro_times))
        mask_bytes = ((n_events + 63) // 64) * 8
        self.assertLessEqual(
            indices, 2 * mask_bytes,
            "intensity access must not materialize per-pixel index vectors",
        )

        # First pixel access materializes: index memory grows well beyond mask
        _ = img[0][0][0].tttr_indices
        _, indices_after, _ = img.get_memory_usage_detailed()
        self.assertGreater(indices_after, indices)

    def test_lazy_and_materialized_agree(self):
        """Intensity from the mask equals intensity from materialized pixels."""
        tttr = make_clsm_tttr(3, 16, 16, 120)
        img = make_img(tttr, 16, 16)
        img.fill(tttr_data=tttr, channels=[1, 2])
        i_lazy = np.asarray(img.intensity).copy()

        img2 = make_img(tttr, 16, 16)
        img2.fill(tttr_data=tttr, channels=[1, 2])
        _ = img2[0]  # force materialization
        i_mat = np.asarray(img2.intensity)
        np.testing.assert_array_equal(i_lazy, i_mat)

    def test_uncoarsened_decay_fast_path_matches_materialized(self):
        """The direct coarsening=1 histogram matches the legacy pixel path."""
        tttr = make_clsm_tttr(3, 16, 16, 120)
        img = make_img(tttr, 16, 16)
        img.fill(tttr_data=tttr, channels=[1, 2])
        decay_lazy = np.asarray(
            img.get_fluorescence_decay(
                tttr, micro_time_coarsening=1, stack_frames=False
            )
        )

        img2 = make_img(tttr, 16, 16)
        img2.fill(tttr_data=tttr, channels=[1, 2])
        _ = img2[0]  # force materialization and the legacy traversal
        decay_materialized = np.asarray(
            img2.get_fluorescence_decay(
                tttr, micro_time_coarsening=1, stack_frames=False
            )
        )
        np.testing.assert_array_equal(decay_lazy, decay_materialized)

    def test_decay_micro_time_axis_can_be_capped(self):
        """An explicit TAC cap is the prefix of the full decay cube."""
        tttr = make_clsm_tttr(2, 8, 8, 80)
        img = make_img(tttr, 8, 8)
        img.fill(tttr_data=tttr, channels=[1, 2])
        full = np.asarray(
            img.get_fluorescence_decay(
                tttr, micro_time_coarsening=1, stack_frames=True
            )
        )
        capped = np.asarray(
            img.get_fluorescence_decay(
                tttr,
                micro_time_coarsening=1,
                stack_frames=True,
                max_micro_time_channels=1024,
            )
        )
        self.assertEqual(capped.shape[-1], 1024)
        np.testing.assert_array_equal(capped, full[..., :1024])

    def test_stale_handle_survives_refill(self):
        """A line handle obtained before a refill sees the new fill's photons."""
        tttr = make_clsm_tttr(2, 8, 8, 60)
        img = make_img(tttr, 8, 8)
        img.fill(tttr_data=tttr, channels=[1, 2])
        line = img[0][0]                       # handle escapes
        counts_both = [line[p].size() for p in range(8)]

        img.fill(tttr_data=tttr, channels=[1])  # refill with fewer channels
        counts_ch1 = [line[p].size() for p in range(8)]
        self.assertLess(sum(counts_ch1), sum(counts_both))

        # matches a fresh fill viewed through the normal path
        img2 = make_img(tttr, 8, 8)
        img2.fill(tttr_data=tttr, channels=[1])
        expected = [img2[0][0][p].size() for p in range(8)]
        self.assertEqual(counts_ch1, expected)

    def test_shape_accessors_do_not_materialize(self):
        tttr = make_clsm_tttr(2, 8, 8, 60)
        img = make_img(tttr, 8, 8)
        img.fill(tttr_data=tttr, channels=[1, 2])
        _ = (img.n_frames, img.n_lines, img.n_pixel, len(img))
        _, indices, _ = img.get_memory_usage_detailed()
        n_events = len(np.asarray(tttr.macro_times))
        mask_bytes = ((n_events + 63) // 64) * 8
        self.assertLessEqual(indices, 2 * mask_bytes)

    def test_image_tttr_indices_property(self):
        """The previously broken CLSMImage.tttr_indices now works and equals
        the union of all pixel indices."""
        tttr = make_clsm_tttr(2, 8, 8, 60)
        img = make_img(tttr, 8, 8)
        img.fill(tttr_data=tttr, channels=[1, 2])
        lazy_indices = np.asarray(img.tttr_indices)

        # aggregate from pixels (this also materializes)
        agg = []
        for f in range(img.n_frames):
            for l in range(img.n_lines):
                for p in range(img.n_pixel):
                    agg.extend(img[f][l][p].tttr_indices)
        np.testing.assert_array_equal(lazy_indices, np.sort(np.asarray(agg)))

        # materialized path returns the same
        np.testing.assert_array_equal(np.asarray(img.tttr_indices), lazy_indices)

    def test_split_mode_lazy_intensity(self):
        tttr = make_clsm_tttr(4, 16, 16, 120)
        img = make_img(tttr, 16, 16, split_by_channel=True)
        img.fill(tttr_data=tttr, channels=[1, 2])
        i_lazy = np.asarray(img.intensity).copy()

        img2 = make_img(tttr, 16, 16, split_by_channel=True)
        img2.fill(tttr_data=tttr, channels=[1, 2])
        _ = img2.frame_at(0)  # materialize
        np.testing.assert_array_equal(i_lazy, np.asarray(img2.intensity))


if __name__ == "__main__":
    unittest.main()
