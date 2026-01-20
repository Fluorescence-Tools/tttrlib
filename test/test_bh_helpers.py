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


if __name__ == "__main__":
    unittest.main()
