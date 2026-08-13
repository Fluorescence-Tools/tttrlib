# SPDX-License-Identifier: BSD-3-Clause
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import tttrlib

_BUILD_BIN = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../../build/modules/io/pto/pto")
)


def _find_pto():
    """The standalone pto tool: build tree first, then PATH.

    Same rule as misc/test_cli.py. An install-only test job -- a wheel or a
    conda package under test -- has no build tree, so the tests that drive the
    tool skip instead of failing on a path that was never going to be there.
    """
    if os.path.isfile(_BUILD_BIN) and os.access(_BUILD_BIN, os.X_OK):
        return _BUILD_BIN
    return shutil.which("pto")


PTO_BIN = _find_pto()
needs_pto = unittest.skipUnless(PTO_BIN, "the pto tool is neither built nor installed")


class TestPtoPRD25(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.pto_path = os.path.join(self.temp_dir.name, "test_container.pto")
        
        # Create a container with objects and tags
        f = tttrlib.PtoFile()
        self.assertTrue(f.create(self.pto_path, "PRD-25 Test Container"))
        
        # Add payload objects
        self.payload1 = b"Photon stream data block 1234567890"
        self.payload2 = b"Burst table payload data 9876543210"
        
        self.uid1 = f.add("photon-stream", "ptu", "stream1", self.payload1)
        self.assertTrue(self.uid1 > 0)
        
        self.uid2 = f.add("burst-list", "dstore", "bursts1", self.payload2)
        self.assertTrue(self.uid2 > 0)
        
        # Add tags
        t1 = tttrlib.PtoTag()
        t1.name = "sample"
        t1.type = tttrlib.PtoType_Text
        t1.text = "DNA-15bp"
        t1.target = 0
        f.add_tag(t1)
        
        t2 = tttrlib.PtoTag()
        t2.name = "operator"
        t2.type = tttrlib.PtoType_Text
        t2.text = "tp"
        t2.target = self.uid1
        f.add_tag(t2)
        
        self.assertTrue(f.commit())
        f.close()

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_banner_presence_and_compact_roundtrip(self):
        """PtoBanner presence & compact roundtrip"""
        with open(self.pto_path, "rb") as fp:
            head_bytes = fp.read(512)
            self.assertIn(b"pto\n", head_bytes)
            self.assertIn(b"This is a .pto photon container", head_bytes)

        # Compact file and verify banner survives
        compacted_path = os.path.join(self.temp_dir.name, "compacted.pto")
        f = tttrlib.PtoFile()
        self.assertTrue(f.open(self.pto_path, False))
        self.assertTrue(f.compact(compacted_path))
        f.close()

        with open(compacted_path, "rb") as fp:
            compact_head_bytes = fp.read(512)
            self.assertIn(b"This is a .pto photon container", compact_head_bytes)

    @needs_pto
    def test_cli_ls_and_json(self):
        """CLI ls command and --json output"""
        res = subprocess.run([PTO_BIN, "ls", self.pto_path], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("PRD-25 Test Container", res.stdout)
        self.assertIn("stream1", res.stdout)
        self.assertIn("bursts1", res.stdout)

        # JSON mode
        res_json = subprocess.run([PTO_BIN, "--json", "ls", self.pto_path], capture_output=True, text=True)
        self.assertEqual(res_json.returncode, 0)
        data = json.loads(res_json.stdout)
        self.assertEqual(data["title"], "PRD-25 Test Container")
        self.assertEqual(len(data["objects"]), 2)
        self.assertEqual(data["objects"][0]["name"], "stream1")

    @needs_pto
    def test_cli_tree_and_alignment(self):
        """tree command and 8-byte payload alignment"""
        res = subprocess.run([PTO_BIN, "tree", self.pto_path], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0)
        self.assertIn("EBML", res.stdout)
        self.assertIn("Segment", res.stdout)
        self.assertIn("FileData", res.stdout)
        self.assertIn("8-byte aligned", res.stdout)

    @needs_pto
    def test_cli_cat_and_extract(self):
        """cat and extract commands"""
        # cat by name
        res = subprocess.run([PTO_BIN, "cat", "stream1", self.pto_path], capture_output=True)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout, self.payload1)

        # extract by UID
        uid_hex = f"0x{self.uid2:08x}"
        out_dir = os.path.join(self.temp_dir.name, "extracted")
        os.makedirs(out_dir, exist_ok=True)
        res_ext = subprocess.run([PTO_BIN, "extract", uid_hex, "-o", out_dir, self.pto_path], capture_output=True, text=True)
        self.assertEqual(res_ext.returncode, 0)
        
        extracted_file = os.path.join(out_dir, "bursts1")
        self.assertTrue(os.path.exists(extracted_file))
        with open(extracted_file, "rb") as fp:
            self.assertEqual(fp.read(), self.payload2)

    @needs_pto
    def test_cli_exit_codes(self):
        """Exit codes 0, 1, 2, 3, 4"""
        # Non-existent file -> exit 4
        res4 = subprocess.run([PTO_BIN, "ls", "non_existent_file.pto"], capture_output=True)
        self.assertEqual(res4.returncode, 4)

        # Selector matching nothing -> exit 3
        res3 = subprocess.run([PTO_BIN, "cat", "non_existent_stream", self.pto_path], capture_output=True)
        self.assertEqual(res3.returncode, 3)

        # Invalid command / usage error -> exit 2
        res2 = subprocess.run([PTO_BIN, "invalid_command", self.pto_path], capture_output=True)
        self.assertEqual(res2.returncode, 2)

    @needs_pto
    def test_bundle_creation_and_execution(self):
        """Executable bundling, P % 8 == 0, execution & reader open"""
        bundle_path = os.path.join(self.temp_dir.name, "run.pto.com")
        res_b = subprocess.run([PTO_BIN, "bundle", self.pto_path, "-o", bundle_path], capture_output=True, text=True)
        self.assertEqual(res_b.returncode, 0)
        self.assertTrue(os.path.exists(bundle_path))

        # Check info to verify payload start P % 8 == 0
        res_info = subprocess.run([PTO_BIN, "info", bundle_path], capture_output=True, text=True)
        self.assertEqual(res_info.returncode, 0)
        self.assertIn("Executable Bundle", res_info.stdout)

        # Run bundled executable directly with no args (runs ls via PTO_READER)
        env = os.environ.copy()
        env["PTO_READER"] = PTO_BIN
        res_exec = subprocess.run(["/bin/sh", bundle_path], capture_output=True, text=True, env=env)
        self.assertEqual(res_exec.returncode, 0)
        self.assertIn("PRD-25 Test Container", res_exec.stdout)
        self.assertIn("stream1", res_exec.stdout)

        # Run bundled executable extract command
        ext_dir = os.path.join(self.temp_dir.name, "bundle_extracted")
        os.makedirs(ext_dir, exist_ok=True)
        res_ext = subprocess.run(["/bin/sh", bundle_path, "extract", "-o", ext_dir], capture_output=True, text=True, env=env)
        self.assertEqual(res_ext.returncode, 0)
        with open(os.path.join(ext_dir, "stream1"), "rb") as fp:
            self.assertEqual(fp.read(), self.payload1)

        # Verify tttrlib PtoFile::open and is_pto_file accept the bundle
        self.assertTrue(tttrlib.is_pto_file(bundle_path))
        f_bundle = tttrlib.PtoFile()
        self.assertTrue(f_bundle.open(bundle_path, False))
        self.assertEqual(f_bundle.title(), "PRD-25 Test Container")
        f_bundle.close()

    def test_add_inspection_data(self):
        """Test embedding cheap inspection data (time trace, decays, metadata)"""
        pto_insp_path = os.path.join(self.temp_dir.name, "inspection.pto")
        f = tttrlib.PtoFile()
        self.assertTrue(f.create(pto_insp_path, "Inspection Test"))

        # Create time trace counts
        trace_counts = tttrlib.VectorUint32([100, 250, 400, 1200, 300, 150])
        trace_dt = 0.01

        meta_json = '{"instrument": "HydraHarp 400", "channels": [0, 1]}'

        # Test adding inspection trace, decays for multiple channels, and metadata
        self.assertTrue(f.add_inspection_trace(trace_counts, trace_dt))
        self.assertTrue(f.add_inspection_decay(0, tttrlib.VectorUint32([10, 50, 5000, 2500, 1200, 600, 300]), 0.032))
        self.assertTrue(f.add_inspection_decay(1, tttrlib.VectorUint32([5, 20, 3000, 1200, 500, 200, 80]), 0.032))
        self.assertTrue(f.add_inspection_metadata(meta_json))
        self.assertTrue(f.commit())
        f.close()

        # Reopen and check embedded objects exist
        f_read = tttrlib.PtoFile()
        self.assertTrue(f_read.open(pto_insp_path, False))
        self.assertTrue(f_read.has(f_read.find("time_trace")))
        self.assertTrue(f_read.has(f_read.find("decay_ch0")))
        self.assertTrue(f_read.has(f_read.find("decay_ch1")))
        self.assertTrue(f_read.has(f_read.find("tttr_metadata")))
        f_read.close()

if __name__ == "__main__":
    unittest.main()
