"""
Test BurstFilter JSON functionality
==================================

This test verifies that the BurstFilter class correctly handles JSON serialization
and deserialization of parameters.
"""
import unittest
import json
import tempfile
import os

class TestBurstFilterJSON(unittest.TestCase):
    
    def setUp(self):
        """Set up test fixtures."""
        # Import tttrlib
        try:
            import tttrlib
            self.tttrlib = tttrlib
        except ImportError:
            self.skipTest("tttrlib not available")
    
    def test_to_dict_from_dict(self):
        """Test to_dict and from_dict methods."""
        # Create a dummy TTTR object (we won't actually use it for burst search)
        tttr_data = self.tttrlib.TTTR()
        burst_filter = self.tttrlib.BurstFilter(tttr_data.Get())
        
        # Set some parameters
        burst_filter.set_burst_parameters(20, 10, 1.0e-3)
        burst_filter.set_background_parameters(10.0, 0.0)
        
        # Convert to dictionary
        params_dict = burst_filter.to_dict()
        
        # Verify structure
        self.assertIn("burst_parameters", params_dict)
        self.assertIn("background_parameters", params_dict)
        
        # Create a new filter and load parameters
        burst_filter2 = self.tttrlib.BurstFilter(tttr_data.Get())
        burst_filter2.from_dict(params_dict)
        
        # Verify parameters were loaded correctly
        # Note: We can't directly access private members, so we check via JSON
        params_dict2 = burst_filter2.to_dict()
        self.assertEqual(params_dict, params_dict2)
    
    def test_save_load_parameters(self):
        """Test save_parameters and load_parameters methods."""
        # Create a dummy TTTR object
        tttr_data = self.tttrlib.TTTR()
        burst_filter = self.tttrlib.BurstFilter(tttr_data.Get())
        
        # Set some parameters
        burst_filter.set_burst_parameters(25, 15, 2.0e-3)
        burst_filter.set_background_parameters(15.0, 1.0)
        
        # Save to temporary file
        with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as tmp_file:
            temp_filename = tmp_file.name
        
        try:
            burst_filter.save_parameters(temp_filename)
            
            # Verify file was created and is valid JSON
            self.assertTrue(os.path.exists(temp_filename))
            
            with open(temp_filename, 'r') as f:
                saved_data = json.load(f)
            
            self.assertIn("burst_parameters", saved_data)
            self.assertIn("background_parameters", saved_data)
            
            # Create a new filter and load parameters
            burst_filter2 = self.tttrlib.BurstFilter(tttr_data.Get())
            burst_filter2.load_parameters(temp_filename)
            
            # Verify parameters were loaded correctly
            original_dict = burst_filter.to_dict()
            loaded_dict = burst_filter2.to_dict()
            self.assertEqual(original_dict, loaded_dict)
            
        finally:
            # Clean up temporary file
            if os.path.exists(temp_filename):
                os.unlink(temp_filename)
    
    def test_to_json_string_from_json_string(self):
        """Test to_json_string and from_json_string methods."""
        # Create a dummy TTTR object
        tttr_data = self.tttrlib.TTTR()
        burst_filter = self.tttrlib.BurstFilter(tttr_data.Get())
        
        # Set some parameters
        burst_filter.set_burst_parameters(30, 20, 3.0e-3)
        burst_filter.set_background_parameters(20.0, 2.0)
        
        # Convert to JSON string
        json_string = burst_filter.to_json_string()
        
        # Verify it's a valid JSON string
        self.assertIsInstance(json_string, str)
        parsed_data = json.loads(json_string)
        self.assertIn("burst_parameters", parsed_data)
        self.assertIn("background_parameters", parsed_data)
        
        # Create a new filter and load parameters from string
        burst_filter2 = self.tttrlib.BurstFilter(tttr_data.Get())
        burst_filter2.from_json_string(json_string)
        
        # Verify parameters were loaded correctly
        original_string = burst_filter.to_json_string()
        loaded_string = burst_filter2.to_json_string()
        self.assertEqual(original_string, loaded_string)

if __name__ == '__main__':
    unittest.main()
