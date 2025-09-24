from __future__ import division

import os
import unittest
import json
import numpy as np
import tempfile
from pathlib import Path

import tttrlib

repo_root = Path(__file__).resolve().parents[1]
settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
settings = json.load(open(settings_path))
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
# Helper function to get full path
def get_data_path(rel_path):
    p = (data_root / rel_path).resolve()
    if not p.exists():
        print(f"WARNING: File {p} does not exist")
    return str(p)

for key in ["spc132_filename", "spc630_filename", "photon_hdf_filename",
            "ptu_hh_t2_filename", "ptu_hh_t3_filename", "ht3_clsm_filename", "sm_filename"]:
    if key in settings:
        settings[key] = get_data_path(settings[key])

data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping TTTRMask tests")
class Tests(unittest.TestCase):

    def test_set_tttr(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(data)
        self.assertEqual(mask.size(), data.size())

    def test_set_get_mask(self):
        m = [True, False, False]
        mask = tttrlib.TTTRMask()
        mask.set_mask(m)
        self.assertEqual(mask.size(), len(m))
        mg = mask.get_mask()
        self.assertListEqual(list(mg), m)

    def test_get_indices(self):
        mask = tttrlib.TTTRMask()
        mask.set_tttr(data)
        mask.select_channels(data, [0, 1])

    def test_get_selected_ranges(self):
        pass

    def test_select_microtime_ranges(self):
        pass

    def test_select_count_rate(self):
        pass

    def test_select_channels(self):
        pass
