import unittest
import numpy as np
import tttrlib
import tempfile
import os


class TestBHHelpers(unittest.TestCase):
    def test_parse_bh_set_file(self):
        # Create a dummy .set file
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".set") as f:
            f.write("* Some comment\n")
            f.write("SP_IMG_X = 512\n")
            f.write("SP_IMG_Y = 256\n")
            f.write("SP_PIX_CLK = 1\n")
            f.write("OTHER = something\n")
            temp_name = f.name

        try:
            # We use the function from tttrlib if it's already integrated,
            # otherwise we can't test it easily without rebuilding.
            # But we can import it directly from the file for now if needed.
            try:
                from tttrlib import parse_bh_set_file
            except ImportError:
                # Fallback to direct import if not yet in tttrlib
                import sys

                sys.path.append(os.path.abspath("ext/python"))
                from bh_helpers import parse_bh_set_file

            settings = parse_bh_set_file(temp_name)
            self.assertEqual(settings["n_pixel_per_line"], 512)
            self.assertEqual(settings["n_lines"], 256)
            self.assertEqual(settings["use_pixel_markers"], True)
        finally:
            if os.path.exists(temp_name):
                os.remove(temp_name)

    def test_detect_frame1_extra_line(self):
        try:
            from tttrlib import detect_frame1_extra_line
        except ImportError:
            import sys

            sys.path.append(os.path.abspath("ext/python"))
            from bh_helpers import detect_frame1_extra_line

        # Case 1: Anomaly detected (Frame 1 has 4 lines, Frame 2 has 3 lines)
        data_anomaly = tttrlib.TTTR()
        # Frame 1: 1 frame marker (ch 4), 4 line markers (ch 2)
        data_anomaly.append_event(0, 0, 4, 1)  # F
        data_anomaly.append_event(0, 0, 2, 1)  # L
        data_anomaly.append_event(0, 0, 2, 1)  # L
        data_anomaly.append_event(0, 0, 2, 1)  # L
        data_anomaly.append_event(0, 0, 2, 1)  # L
        # Frame 2: 1 frame marker, 3 line markers
        data_anomaly.append_event(0, 0, 4, 1)  # F
        data_anomaly.append_event(0, 0, 2, 1)  # L
        data_anomaly.append_event(0, 0, 2, 1)  # L
        data_anomaly.append_event(0, 0, 2, 1)  # L
        # Frame 3: 1 frame marker
        data_anomaly.append_event(0, 0, 4, 1)  # F

        self.assertTrue(detect_frame1_extra_line(data_anomaly))

        # Case 2: No anomaly (Frame 1 has 3 lines, Frame 2 has 3 lines)
        data_normal = tttrlib.TTTR()
        # Frame 1: 1 frame marker, 3 line markers
        data_normal.append_event(0, 0, 4, 1)
        data_normal.append_event(0, 0, 2, 1)
        data_normal.append_event(0, 0, 2, 1)
        data_normal.append_event(0, 0, 2, 1)
        # Frame 2: 1 frame marker, 3 line markers
        data_normal.append_event(0, 0, 4, 1)
        data_normal.append_event(0, 0, 2, 1)
        data_normal.append_event(0, 0, 2, 1)
        data_normal.append_event(0, 0, 2, 1)
        # Frame 3: 1 frame marker
        data_normal.append_event(0, 0, 4, 1)

        self.assertFalse(detect_frame1_extra_line(data_normal))

        # Case 3: Not enough frames
        data_short = tttrlib.TTTR()
        data_short.append_event(0, 0, 4, 1)
        data_short.append_event(0, 0, 2, 1)
        data_short.append_event(0, 0, 4, 1)
        self.assertFalse(detect_frame1_extra_line(data_short))

    def test_get_adjusted_frame_markers(self):
        try:
            from tttrlib import get_adjusted_frame_markers
        except ImportError:
            import sys

            sys.path.append(os.path.abspath("ext/python"))
            from bh_helpers import get_adjusted_frame_markers

        # Case 1: Anomaly detected
        data_anomaly = tttrlib.TTTR()
        # F at 0, L at 1, 2, 3, 4
        data_anomaly.append_event(0, 0, 4, 1)  # 0: F
        data_anomaly.append_event(0, 0, 2, 1)  # 1: L (extra)
        data_anomaly.append_event(0, 0, 2, 1)  # 2: L
        data_anomaly.append_event(0, 0, 2, 1)  # 3: L
        data_anomaly.append_event(0, 0, 2, 1)  # 4: L
        # F at 5, L at 6, 7, 8
        data_anomaly.append_event(0, 0, 4, 1)  # 5: F
        data_anomaly.append_event(0, 0, 2, 1)  # 6: L
        data_anomaly.append_event(0, 0, 2, 1)  # 7: L
        data_anomaly.append_event(0, 0, 2, 1)  # 8: L
        # F at 9
        data_anomaly.append_event(0, 0, 4, 1)  # 9: F

        # Original frame markers are [0, 5, 9]
        # Adjusted frame markers should be [2, 5, 9] because of extra line in F1
        # (index 1 is the extra line marker, so we start at 2)
        adjusted = get_adjusted_frame_markers(data_anomaly)
        self.assertEqual(adjusted, [2, 5, 9])

        # Case 2: No anomaly
        data_normal = tttrlib.TTTR()
        data_normal.append_event(0, 0, 4, 1)  # 0: F
        data_normal.append_event(0, 0, 2, 1)  # 1: L
        data_normal.append_event(0, 0, 4, 1)  # 2: F
        data_normal.append_event(0, 0, 2, 1)  # 3: L
        data_normal.append_event(0, 0, 4, 1)  # 4: F

        # Original and adjusted should be [0, 2, 4]
        adjusted_normal = get_adjusted_frame_markers(data_normal)
        self.assertEqual(adjusted_normal, [0, 2, 4])

    def test_prepare_bh_clsm_settings(self):
        try:
            from tttrlib import prepare_bh_clsm_settings
        except ImportError:
            import sys

            sys.path.append(os.path.abspath("ext/python"))
            from bh_helpers import prepare_bh_clsm_settings

        data_anomaly = tttrlib.TTTR()
        data_anomaly.append_event(0, 0, 4, 1)  # 0: F
        data_anomaly.append_event(0, 0, 2, 1)  # 1: L
        data_anomaly.append_event(0, 0, 2, 1)  # 2: L
        data_anomaly.append_event(0, 0, 4, 1)  # 3: F
        data_anomaly.append_event(0, 0, 2, 1)  # 4: L
        data_anomaly.append_event(0, 0, 4, 1)  # 5: F

        # Create a dummy .set file
        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".set") as f:
            f.write("SP_IMG_X = 512\n")
            f.write("SP_IMG_Y = 256\n")
            f.write("SP_PIX_CLK = 1\n")
            temp_name = f.name

        try:
            settings = prepare_bh_clsm_settings(data_anomaly, set_filename=temp_name)
            self.assertEqual(settings["n_pixel_per_line"], 512)
            self.assertEqual(settings["n_lines"], 256)
            self.assertTrue(settings["skip_first_line_frame1"])
            self.assertEqual(settings["marker_frame_start"], [2, 3, 5])
        finally:
            if os.path.exists(temp_name):
                os.remove(temp_name)


if __name__ == "__main__":
    unittest.main()
