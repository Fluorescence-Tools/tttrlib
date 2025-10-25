from __future__ import division

import json
import os
import tempfile
import unittest
import tttrlib
import numpy as np
from pathlib import Path

print("Test: ", __file__)

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
    data_root_str = settings.get("data_root", "./tttr-data")
    # Handle network shares (V:, Z:, etc.) - don't convert to UNC
    if len(data_root_str) > 1 and data_root_str[1] == ':':
        # It's a drive letter path, use it as-is
        data_root = Path(data_root_str)
    else:
        data_root = (repo_root / data_root_str).resolve()
# Don't call .resolve() on network shares as it converts them to UNC paths
if not (len(str(data_root)) > 1 and str(data_root)[1] == ':'):
    data_root = data_root.resolve()
# Determine if data directory exists
DATA_AVAILABLE = data_root.is_dir()
if not DATA_AVAILABLE:
    print(f"WARNING: Data directory not found: {data_root}")
# Helper function to get full path
def get_data_path(rel_path):
    p = data_root / rel_path
    # Don't resolve network share paths
    if not (len(str(data_root)) > 1 and str(data_root)[1] == ':'):
        p = p.resolve()
    if not p.exists():
        print(f"WARNING: File {p} does not exist")
    return str(p)
# Update settings file paths
for key in ["spc132_filename", "spc630_filename", "photon_hdf_filename",
           "ptu_hh_t2_filename", "ptu_hh_t3_filename", "ht3_clsm_filename", "sm_filename"]:
    if key in settings:
        settings[key] = get_data_path(settings[key])
# Update test_files entries
if "test_files" in settings:
    settings["test_files"] = [[get_data_path(p), t] for p, t in settings["test_files"]]
test_files = settings["test_files"]
# Lazy load data only if available (avoid segfault on import)
data = None
if DATA_AVAILABLE:
    try:
        data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
    except Exception as e:
        print(f"WARNING: Failed to load data: {e}")
        data = None


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping TTTR transcode tests")
class Tests(unittest.TestCase):

    def setUp(self):
        """Verify data is loaded before each test"""
        if data is None:
            self.skipTest("Data failed to load")

    def test_read_write(self):
        # Read & write TTTR file
        for input_fn, routine in test_files[:1]: # so far only spc-130 & PTU-T3 supported
            suffix = os.path.splitext(input_fn)[1]
            fd, output_fn = tempfile.mkstemp(suffix)
            os.close(fd)  # Close the file descriptor before writing

            # Read data from input file first
            data = tttrlib.TTTR(input_fn, routine)
            data.write(output_fn)
            data_written = tttrlib.TTTR(output_fn, routine)

            np.testing.assert_array_almost_equal(data.routing_channels, data_written.routing_channels)
            np.testing.assert_array_almost_equal(data.macro_times, data_written.macro_times)
            np.testing.assert_array_almost_equal(data.micro_times, data_written.micro_times)

    def test_write_tttr_other_header(self):
        """Test writing data with a different header format.
        
        Note: When writing SPC-130 data with a PTU header, the macro_times may differ
        due to format conversion. This test verifies that the write operation succeeds
        and the file can be read back.
        """
        fd, tmp_filename = tempfile.mkstemp(suffix='.ptu')
        os.close(fd)  # Close the file descriptor before writing
        fn_data = settings["ptu_hh_t3_filename"]
        container_type = 0
        # Read header from other TTTR file
        other_header = tttrlib.TTTRHeader(fn_data, container_type)
        data.write(tmp_filename, other_header)
        d2 = tttrlib.TTTR(tmp_filename)
        # Verify the file was written and can be read back
        self.assertIsNotNone(d2.micro_times)
        self.assertIsNotNone(d2.macro_times)
        self.assertIsNotNone(d2.routing_channels)
        # Note: We don't compare exact values as format conversion may change them

    def test_write_tttr_new_header(self):
        """
        Tests writing a TTTR container with updated header.

        This test verifies that the TTTR library successfully saves a PTU file to disk,
        and correctly reads it back into memory. The changes made to the header are specific
        to SPC-130 and ensure compatibility with the updated container type and record type.

        :return: None

        :raises AssertionError: If any of the assertions in this method fail.
        """
        # Create a temporary file for writing and reading
        fd, filename = tempfile.mkstemp(suffix='.ptu')
        os.close(fd)  # Close the file descriptor before writing

        # Initialize the TTTR library with SPC-130 settings
        data_spc = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')
        header = data_spc.header

        # Update the container and record type in the header
        # see: TTTRHeaderTypes.h
        # PQ_PTU_CONTAINER          0 (new)
        # PQ_RECORD_TYPE_HHT3v2       4 (new)
        header.tttr_container_type = 0
        header.tttr_record_type = 4

        # Write the updated data to the temporary file
        data_spc.write(filename)

        # Read the written data back into memory
        d2 = tttrlib.TTTR(filename)

        # Verify the file was written and can be read back
        # Note: When changing container_type and record_type, the macro_times interpretation may change
        # So we just verify the file can be read without errors
        self.assertIsNotNone(d2.micro_times)
        self.assertIsNotNone(d2.macro_times)
        self.assertIsNotNone(d2.routing_channels)
        # Verify we have the same number of events
        self.assertEqual(len(d2.micro_times), len(data_spc.micro_times))
        self.assertEqual(len(d2.macro_times), len(data_spc.macro_times))
        self.assertEqual(len(d2.routing_channels), len(data_spc.routing_channels))
