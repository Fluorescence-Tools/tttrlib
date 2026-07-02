from __future__ import division

import unittest

import tttrlib
import numpy as np


LINE_LENGTH = 8  # macro time between line start and line stop marker
GAP = 10  # macro time between structural markers


def make_clsm_tttr(frame_line_photon_offsets):
    """Build a synthetic TTTR stream with default-routine CLSM markers.

    frame_line_photon_offsets: list (frames) of lists (lines) of photon macro
    time offsets relative to the line start marker. Photons are placed on
    routing channel 1; markers use the default routine conventions
    (marker_event_type=1, frame start channel 1, line start 3, line stop 2).
    """
    macro_times = []
    channels = []
    event_types = []

    def add(time, channel, event_type):
        macro_times.append(time)
        channels.append(channel)
        event_types.append(event_type)

    t = 0
    for lines in frame_line_photon_offsets:
        add(t, 1, 1)  # frame start marker
        t += GAP
        for offsets in lines:
            add(t, 3, 1)  # line start marker
            for off in offsets:
                add(t + off, 1, 0)  # photon on routing channel 1
            add(t + LINE_LENGTH, 2, 1)  # line stop marker
            t += LINE_LENGTH + GAP
    add(t, 1, 1)  # closing frame marker so the last frame is complete

    n = len(macro_times)
    return tttrlib.TTTR(
        np.array(macro_times, dtype=np.uint64),
        np.zeros(n, dtype=np.uint16),
        np.array(channels, dtype=np.int8),
        np.array(event_types, dtype=np.int8),
    )


def make_clsm_image(tttr, n_lines):
    return tttrlib.CLSMImage(
        tttr_data=tttr,
        marker_frame_start=[1],
        marker_line_start=3,
        marker_line_stop=2,
        marker_event_type=1,
        n_pixel_per_line=4,
        reading_routine="default",
        skip_before_first_frame_marker=True,
        settings={"n_lines": n_lines},
    )


class TestCLSMNonUniformPixelDurations(unittest.TestCase):
    def test_non_uniform_pixel_durations_basic(self):
        """Photons are assigned to pixels according to non-uniform durations."""
        # 2 frames x 2 lines x 4 pixels, line duration LINE_LENGTH = 8
        # Line 0 durations [2, 1, 3, 2] -> cumulative [2, 3, 6, 8]
        #   offsets 0, 2, 3, 7 -> pixels 0, 1, 2, 3
        # Line 1 durations [1, 3, 2, 2] -> cumulative [1, 4, 6, 8]
        #   offsets 0, 1, 4, 7 -> pixels 0, 1, 2, 3
        line0_offsets = [0, 2, 3, 7]
        line1_offsets = [0, 1, 4, 7]
        tttr = make_clsm_tttr(
            [
                [line0_offsets, line1_offsets],  # Frame 0
                [line0_offsets, line1_offsets],  # Frame 1
            ]
        )
        clsm_image = make_clsm_image(tttr, n_lines=2)
        self.assertEqual(clsm_image.n_frames, 2)
        self.assertEqual(clsm_image.n_lines, 2)
        self.assertEqual(clsm_image.n_pixel, 4)

        # One lines x pixels matrix, applied to every frame
        clsm_image.set_pixel_duration_matrix(
            [
                [2, 1, 3, 2],  # Line 0
                [1, 3, 2, 2],  # Line 1
            ]
        )

        clsm_image.fill(tttr_data=tttr, channels=[1])

        for f in range(2):
            for l in range(2):
                for p in range(4):
                    self.assertEqual(
                        clsm_image[f][l][p].size(),
                        1,
                        f"Frame {f}, line {l}, pixel {p} should have 1 photon",
                    )

    def test_non_uniform_vs_uniform_duration(self):
        """Non-uniform durations produce different results than uniform."""
        # 1 frame x 1 line x 4 pixels; photons at offsets 0, 2, 3, 7
        offsets = [0, 2, 3, 7]
        tttr = make_clsm_tttr([[offsets]])

        # Non-uniform durations [2, 1, 3, 2] -> cumulative [2, 3, 6, 8]
        # offsets 0, 2, 3, 7 -> pixels 0, 1, 2, 3
        clsm_nonuniform = make_clsm_image(tttr, n_lines=1)
        clsm_nonuniform.set_pixel_duration_matrix([[2, 1, 3, 2]])
        clsm_nonuniform.fill(tttr_data=tttr, channels=[1])

        for p in range(4):
            self.assertEqual(
                clsm_nonuniform[0][0][p].size(),
                1,
                f"Non-uniform pixel {p} should have 1 photon",
            )

        # Uniform pixel duration = LINE_LENGTH / 4 = 2:
        # offsets 0, 2, 3, 7 -> pixels 0, 1, 1, 3 (pixel 1 gets 2 photons)
        clsm_uniform = make_clsm_image(tttr, n_lines=1)
        clsm_uniform.fill(tttr_data=tttr, channels=[1])

        self.assertEqual(
            clsm_uniform[0][0][1].size(),
            2,
            "Uniform pixel 1 should have 2 photons",
        )
        self.assertEqual(
            clsm_uniform[0][0][2].size(),
            0,
            "Uniform pixel 2 should have 0 photons",
        )

    def test_has_non_uniform_durations(self):
        """has_non_uniform_durations() reflects whether a matrix is set."""
        tttr = make_clsm_tttr([[[0, 1, 2, 3]]])
        clsm = make_clsm_image(tttr, n_lines=1)

        self.assertFalse(
            clsm.has_non_uniform_durations(),
            "Initially should not have non-uniform durations",
        )

        clsm.set_pixel_duration_matrix([[1, 2, 3, 4]])
        self.assertTrue(
            clsm.has_non_uniform_durations(),
            "After setting durations, should have non-uniform durations",
        )

        # Round trip and cumulative sums
        matrix = clsm.get_pixel_duration_matrix()
        self.assertEqual(len(matrix), 1)
        np.testing.assert_allclose(matrix[0], [1, 2, 3, 4])
        np.testing.assert_allclose(
            clsm.get_cumulative_durations(0, 0), [1, 3, 6, 10]
        )
        # Durations apply to every frame; any frame index yields the same line
        np.testing.assert_allclose(
            clsm.get_cumulative_durations(5, 0), [1, 3, 6, 10]
        )

        # Out-of-range access returns empty instead of crashing
        self.assertEqual(len(clsm.get_cumulative_durations(0, 5)), 0)
        self.assertEqual(len(clsm.get_cumulative_durations(-1, -1)), 0)

        # Clearing with an empty matrix returns to uniform mode
        clsm.set_pixel_duration_matrix([])
        self.assertFalse(clsm.has_non_uniform_durations())


if __name__ == "__main__":
    unittest.main()
