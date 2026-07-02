"""Tests for CLSMImage.get_intensity_masked (virtual fill via a per-event
acceptance bitmask on the TTTR stream)."""
from __future__ import division

import unittest

import tttrlib
import numpy as np


LINE_LENGTH = 800  # macro time between line start and stop marker
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
    return tttrlib.TTTR(macro, micro, chan, etyp)


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


class TestCLSMIntensityMasked(unittest.TestCase):
    def assert_matches_fill(self, img, tttr, channels, mt_ranges=None):
        kw = {} if mt_ranges is None else {"micro_time_ranges": mt_ranges}
        img.fill(tttr_data=tttr, channels=channels, **kw)
        expected = np.asarray(img.intensity)
        got = img.get_intensity_masked(tttr, channels, mt_ranges or [])
        np.testing.assert_array_equal(got, expected)

    def test_matches_fill_all_channels(self):
        tttr = make_clsm_tttr(3, 16, 8, 50)
        self.assert_matches_fill(make_img(tttr, 16, 8), tttr, [1, 2])

    def test_matches_fill_channel_subset(self):
        tttr = make_clsm_tttr(3, 16, 8, 50)
        self.assert_matches_fill(make_img(tttr, 16, 8), tttr, [2])

    def test_matches_fill_micro_time_ranges(self):
        tttr = make_clsm_tttr(3, 16, 8, 50)
        self.assert_matches_fill(make_img(tttr, 16, 8), tttr, [1, 2], [(0, 2048)])

    def test_matches_fill_bidirectional(self):
        tttr = make_clsm_tttr(2, 16, 8, 50)
        img = make_img(tttr, 16, 8, settings={"bidirectional_scan": True})
        self.assert_matches_fill(img, tttr, [1, 2])

    def test_matches_fill_non_uniform_durations(self):
        tttr = make_clsm_tttr(2, 16, 8, 50)
        img = make_img(tttr, 16, 8)
        rng = np.random.default_rng(3)
        img.set_pixel_duration_matrix(rng.uniform(50, 150, size=(16, 8)).tolist())
        self.assert_matches_fill(img, tttr, [1, 2])

    def test_does_not_touch_pixels(self):
        tttr = make_clsm_tttr(2, 16, 8, 50)
        img = make_img(tttr, 16, 8)
        img.clear()  # drop the photons added by the constructor's auto-fill
        got = img.get_intensity_masked(tttr, [1, 2], [])
        self.assertGreater(got.sum(), 0)
        self.assertEqual(img[0][0][0].size(), 0, "virtual fill must not fill pixels")

    def test_empty_channels_defaults_to_all(self):
        tttr = make_clsm_tttr(2, 16, 8, 50)
        img = make_img(tttr, 16, 8)
        img.fill(tttr_data=tttr, channels=[1, 2])
        expected = np.asarray(img.intensity)
        got = img.get_intensity_masked(tttr, [], [])
        # channels default to all used channels; markers are not photons
        np.testing.assert_array_equal(got, expected)


if __name__ == "__main__":
    unittest.main()
