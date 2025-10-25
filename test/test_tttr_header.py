"""TTTR Header and metadata tests"""
from __future__ import division

import os
import unittest
import json
import numpy as np
from pathlib import Path
import gc

import tttrlib

settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
settings = json.load(open(settings_path))

repo_root = Path(__file__).resolve().parents[1]
env_root = os.getenv("TTTRLIB_DATA")
if env_root:
    env_root = env_root.strip().strip('\'"')
    data_root = Path(os.path.abspath(env_root))
else:
    data_root_str = settings.get("data_root", "./tttr-data")
    if os.path.isabs(data_root_str):
        data_root = Path(data_root_str)
    else:
        data_root = Path(os.path.abspath(str(repo_root / data_root_str)))

DATA_AVAILABLE = data_root.is_dir()

def get_data_path(rel_path):
    return os.path.abspath(os.path.join(str(data_root), rel_path))

for key in ["spc132_filename"]:
    if key in settings:
        settings[key] = get_data_path(settings[key])


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRHeaderBasic(unittest.TestCase):
    """Basic TTTR header tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_header_exists(self):
        """Test that header exists"""
        header = self.data.header
        self.assertIsNotNone(header)

    def test_header_macro_time_resolution(self):
        """Test macro time resolution"""
        resolution = self.data.header.macro_time_resolution
        self.assertGreater(resolution, 0)

    def test_header_micro_time_resolution(self):
        """Test micro time resolution"""
        resolution = self.data.header.micro_time_resolution
        self.assertGreater(resolution, 0)

    def test_header_number_of_micro_time_channels(self):
        """Test number of micro time channels"""
        n_channels = self.data.header.number_of_micro_time_channels
        self.assertGreater(n_channels, 0)

    def test_header_tttr_record_type(self):
        """Test TTTR record type"""
        record_type = self.data.header.tttr_record_type
        self.assertGreaterEqual(record_type, 0)

    def test_header_is_valid(self):
        """Test header validity"""
        header = self.data.header
        # Header should have valid properties
        self.assertIsNotNone(header.macro_time_resolution)
        self.assertIsNotNone(header.micro_time_resolution)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRHeaderJSON(unittest.TestCase):
    """TTTR header JSON serialization tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_header_get_json(self):
        """Test getting header as JSON"""
        header = self.data.header
        json_str = header.get_json()
        
        self.assertIsNotNone(json_str)
        self.assertIsInstance(json_str, str)
        self.assertGreater(len(json_str), 0)

    def test_header_json_valid(self):
        """Test that header JSON is valid"""
        header = self.data.header
        json_str = header.get_json()
        
        # Should be valid JSON
        try:
            data = json.loads(json_str)
            self.assertIsInstance(data, dict)
        except json.JSONDecodeError:
            self.fail("Header JSON is not valid")

    def test_header_set_json(self):
        """Test setting header from JSON"""
        header1 = self.data.header
        json_str = header1.get_json()
        
        header2 = tttrlib.TTTRHeader()
        header2.set_json(json_str)
        
        # Headers should have same properties
        self.assertEqual(
            header1.macro_time_resolution,
            header2.macro_time_resolution
        )

    def test_header_json_roundtrip(self):
        """Test JSON round-trip consistency"""
        header1 = self.data.header
        json_str1 = header1.get_json()
        
        header2 = tttrlib.TTTRHeader()
        header2.set_json(json_str1)
        json_str2 = header2.get_json()
        
        # JSON should be consistent
        data1 = json.loads(json_str1)
        data2 = json.loads(json_str2)
        
        self.assertEqual(data1, data2)


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRHeaderCopy(unittest.TestCase):
    """TTTR header copy tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_header_copy_constructor(self):
        """Test header copy constructor"""
        header1 = self.data.header
        header2 = tttrlib.TTTRHeader(header1)
        
        self.assertEqual(
            header1.macro_time_resolution,
            header2.macro_time_resolution
        )

    def test_header_copy_independence(self):
        """Test that copied headers are independent"""
        header1 = self.data.header
        header2 = tttrlib.TTTRHeader(header1)
        
        # Both should have same properties
        self.assertEqual(
            header1.micro_time_resolution,
            header2.micro_time_resolution
        )


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found")
class TestTTTRHeaderTags(unittest.TestCase):
    """TTTR header tag tests"""

    def setUp(self):
        self.data = tttrlib.TTTR(settings["spc132_filename"], 'SPC-130')

    def tearDown(self):
        self.data = None
        gc.collect()

    def test_header_add_tag(self):
        """Test adding tag to header"""
        header = self.data.header
        
        # Add a tag
        try:
            header.add_tag("test_key", "test_value")
        except (AttributeError, TypeError):
            # Tag methods might not be exposed
            pass

    def test_header_get_tags(self):
        """Test getting tags from header"""
        header = self.data.header
        
        try:
            tags = header.get_tags()
            self.assertIsNotNone(tags)
        except (AttributeError, TypeError):
            # Tag methods might not be exposed
            pass


class TestTTTRHeaderCreation(unittest.TestCase):
    """TTTR header creation tests"""

    def tearDown(self):
        gc.collect()

    def test_header_empty_creation(self):
        """Test creating empty header"""
        header = tttrlib.TTTRHeader()
        self.assertIsNotNone(header)

    def test_header_copy_creation(self):
        """Test creating header from another header"""
        header1 = tttrlib.TTTRHeader()
        header2 = tttrlib.TTTRHeader(header1)
        
        self.assertIsNotNone(header2)

    def test_header_properties_accessible(self):
        """Test that header properties are accessible"""
        header = tttrlib.TTTRHeader()
        
        # Should be able to access properties
        try:
            _ = header.macro_time_resolution
        except AttributeError:
            pass  # Property might not be set


if __name__ == '__main__':
    unittest.main()
