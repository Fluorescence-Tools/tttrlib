from __future__ import division

import unittest
import os
from pathlib import Path
import json
import tttrlib
import numpy as np

# Determine repository root (two levels up from this file)
repo_root = Path(__file__).resolve().parents[1]
# Load settings JSON
settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
settings = json.load(open(settings_path))
# Resolve data root
env_root = os.getenv("TTTRLIB_DATA")
if env_root:
    env_root = env_root.strip().strip('\'"')
    data_root = Path(env_root)
else:
    data_root = (repo_root / settings.get("data_root", "./tttr-data")).resolve()
data_root = data_root.resolve()
# Determine if data directory exists
DATA_AVAILABLE = data_root.is_dir()
if not DATA_AVAILABLE:
    print(f"WARNING: Data directory not found: {data_root}")
# Helper to get full path
def get_data_path(rel_path):
    p = (data_root / rel_path).resolve()
    if not p.exists():
        print(f"WARNING: File {p} does not exist")
    return str(p)
# Update settings file paths
for key in ["spc132_filename", "spc630_filename", "photon_hdf_filename",
           "ptu_hh_t2_filename", "ptu_hh_t3_filename", "ht3_clsm_filename", "sm_filename"]:
    if key in settings:
        settings[key] = get_data_path(settings[key])
# Update hardcoded filenames used in this test
sp5_filename = get_data_path('imaging/leica/sp5/LSM_1.ptu')
sp8_filename = get_data_path('imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu')
ht3_filename = get_data_path('imaging/pq/ht3/pq_ht3_clsm.ht3')
pq_test_files = [
    get_data_path('imaging/pq/Microtime200_HH400/beads.ptu'),
    get_data_path('imaging/pq/Microtime200_TH260/beads.ptu')
]

sp8_reading_parameter = {
    "reading_routine": 'SP8',
}

ht3_reading_parameter = {
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'default',
    "skip_before_first_frame_marker": True
}
ht3_data = tttrlib.TTTR(ht3_filename)

sp5_data = tttrlib.TTTR(sp5_filename, 'PTU')
sp5_reading_parameter = {
    "reading_routine": 'SP5'
}


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping CLSM tests")
class TestCLSM(unittest.TestCase):

    # If this is set to True as set of files are written as a
    # reference for future tests
    make_reference = settings['make_references']

    def test_leica_sp8_image_1(self):
        if not os.path.exists(sp8_filename):
            self.skipTest(f"Data file not found: {sp8_filename}")
        data = tttrlib.TTTR(sp8_filename, 'PTU')
        clsm_image = tttrlib.CLSMImage(
            tttr_data=data,
            **sp8_reading_parameter
        )
        clsm_image.fill(
            tttr_data=data,
            channels=[1]
        )
        # Test mean TAC image
        mean_tac_image = clsm_image.get_mean_micro_time(
            tttr_data=data,
            minimum_number_of_photons=1
        )
        mean_tac_image = np.clip(mean_tac_image, 0, 1280000)
        mean_tac_image = mean_tac_image.sum(axis=0)
        fn = './test/data/reference/img_ref_mean_tac_sp8.npy'
        if self.make_reference:
            np.save(fn, mean_tac_image)
        # Pixel with less than minimum_number_of_photons have negative numbers
        mean_tac_image = np.clip(mean_tac_image, 0, 1280000)
        np.testing.assert_array_almost_equal(np.load(fn), mean_tac_image)

        # Test decay image
        decay_image = clsm_image.get_fluorescence_decay(
            tttr_data=data,
            micro_time_coarsening=256,
            stack_frames=True
        )
        fn = './test/data/reference/img_ref_decay_image_sp8.npy'
        if self.make_reference:
            np.save(fn, decay_image)
        np.testing.assert_array_almost_equal(np.load(fn), decay_image)

        # Access the pixel
        frame_nbr = 1
        line_nbr = 200
        pixel_nbr = 200
        frame = clsm_image[frame_nbr]
        line = frame[line_nbr]
        pixel = line[pixel_nbr]

        self.assertEqual(pixel.tttr_indices[0], 40940)
        self.assertEqual(clsm_image[frame_nbr][line_nbr][pixel_nbr].tttr_indices[0], 40940)
        self.assertEqual(
            clsm_image[frame_nbr][line_nbr][pixel_nbr].tttr_indices[0],
            pixel.tttr_indices[0]
        )

    def test_get_frame_edges(self):
        if not os.path.exists(sp8_filename):
            self.skipTest(f"Data file not found: {sp8_filename}")
        # SP8
        # Use the previously resolved sp8_filename which handles data path resolution
        filename = sp8_filename
        tttr = tttrlib.TTTR(filename)
        kw = {
            "start_event": 0,
            "stop_event": -1,
            "marker_event_type": 15,
            "marker_frame_start": [4, 6],
            "reading_routine": tttrlib.CLSM_SP8
        }
        frame_marker = tttrlib.CLSMImage.get_frame_edges(tttr, **kw)
        ref = np.array([0, 33529, 66704, 100007, 133448, 166819, 200246,
                  233764, 266964, 300495, 334002, 367244, 400663, 433842,
                  467465, 500897, 534214, 567481, 600787, 633652, 666928,
                  699979, 733392, 766777, 800102, 833586, 866850, 900367,
                  934006, 967652, 1001222, 1034729, 1068121, 1101315, 1134802,
                  1168394, 1201654, 1235243, 1268487, 1301777, 1334909, 1368325,
                  1401602, 1435125, 1468726, 1501850, 1535224, 1568731, 1602119,
                  1635687, 1668901, 1702301, 1735680, 1769383, 1802764, 1836438,
                  1869872, 1903322, 1936681, 1969938, 2003582, 2036774, 2070282,
                  2103539, 2137239, 2170864, 2204452, 2237712, 2270818, 2304016,
                  2337694, 2370946, 2404394, 2437620, 2471361, 2504707, 2538323,
                  2571614, 2604919, 2638158, 2671470, 2704766, 2737823, 2770808,
                  2804447, 2837775, 2871352, 2904614, 2937702, 2971088, 3004543,
                  3037889, 3071241, 3104687, 3104829], dtype=np.uint32)
        np.testing.assert_allclose(frame_marker, ref)
        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            channels=[0],
            fill=True,
            reading_routine='SP8'
        )
        clsm.fill(
            tttr_data=tttr,
            channels=[0],
        )

        # Use the resolved sp5_filename variable for correct path handling
        tttr = tttrlib.TTTR(sp5_filename, 'PTU')
        kw = {
            "start_event": 0,
            "stop_event": -1,
            "marker_event_type": 1,
            "marker_frame_start": [4, 6],
            "reading_routine": tttrlib.CLSM_SP5
        }
        frame_marker = tttrlib.CLSMImage.get_frame_edges(tttr, **kw)
        clsm = tttrlib.CLSMImage(
            tttr_data=tttr,
            channels=[0],
            fill=True,
            reading_routine='SP5'
        )

        np.testing.assert_allclose(clsm.intensity.shape, (230, 256, 256))


    def test_copy_constructor(self):
        reading_parameter = ht3_reading_parameter
        clsm_image_1 = tttrlib.CLSMImage(
            tttr_data=ht3_data,
            **reading_parameter
        )
        clsm_image_2 = tttrlib.CLSMImage(
            source=clsm_image_1, fill=True
        )
        self.assertAlmostEqual(
            float(np.sum(clsm_image_1.intensity - clsm_image_2.intensity)),
            0.0
        )

    def test_open_clsm_ptu_read_header(self):
        missing = [f for f in pq_test_files if not os.path.exists(f)]
        if missing:
            self.skipTest(f"Data files not found: {missing}")
        for ptu_file in pq_test_files:
            print(ptu_file)
            data = tttrlib.TTTR(ptu_file)
            clsm = tttrlib.CLSMImage(data)
