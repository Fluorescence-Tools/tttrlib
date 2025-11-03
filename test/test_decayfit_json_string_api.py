"""
Test DecayFit JSON string API - to_json() returns str, from_json() accepts str
"""
import unittest
import json as json_module
import numpy as np
import tttrlib


class TestDecayFitJsonStringAPI(unittest.TestCase):
    """Test that to_json() returns strings and from_json() accepts strings"""

    def test_decayfit_corrections_to_json_returns_string(self):
        """Test DecayFitCorrections.get_json() returns a string"""
        corrections = tttrlib.DecayFitCorrections()
        result = corrections.get_json()
        
        # Should be a string, not a SWIG object
        self.assertIsInstance(result, str)
        
        # Should be valid JSON
        parsed = json_module.loads(result)
        self.assertIsInstance(parsed, dict)

    def test_decayfit_corrections_from_json_accepts_string(self):
        """Test DecayFitCorrections.set_json() accepts a string"""
        corrections1 = tttrlib.DecayFitCorrections()
        
        # Serialize to JSON string
        json_str = corrections1.get_json()
        self.assertIsInstance(json_str, str)
        
        # Create new object and deserialize from string
        corrections2 = tttrlib.DecayFitCorrections()
        corrections2.set_json(json_str)
        
        # Both should produce the same JSON
        json_str2 = corrections2.get_json()
        self.assertEqual(json_str, json_str2)

    def test_decayfit_settings_to_json_returns_string(self):
        """Test DecayFitSettings.get_json() returns a string"""
        settings = tttrlib.DecayFitSettings()
        result = settings.get_json()
        
        # Should be a string
        self.assertIsInstance(result, str)
        
        # Should be valid JSON
        parsed = json_module.loads(result)
        self.assertIsInstance(parsed, dict)

    def test_decayfit_settings_from_json_accepts_string(self):
        """Test DecayFitSettings.set_json() accepts a string"""
        settings1 = tttrlib.DecayFitSettings()
        
        # Serialize to JSON string
        json_str = settings1.get_json()
        self.assertIsInstance(json_str, str)
        
        # Create new object and deserialize from string
        settings2 = tttrlib.DecayFitSettings()
        settings2.set_json(json_str)
        
        # Both should produce the same JSON
        json_str2 = settings2.get_json()
        self.assertEqual(json_str, json_str2)

    def test_decayfit_integrate_signals_to_json_returns_string(self):
        """Test DecayFitIntegrateSignals.get_json() returns a string"""
        integrate = tttrlib.DecayFitIntegrateSignals()
        result = integrate.get_json()
        
        # Should be a string
        self.assertIsInstance(result, str)
        
        # Should be valid JSON
        parsed = json_module.loads(result)
        self.assertIsInstance(parsed, dict)

    def test_decayfit_integrate_signals_from_json_accepts_string(self):
        """Test DecayFitIntegrateSignals.set_json() accepts a string"""
        integrate1 = tttrlib.DecayFitIntegrateSignals()
        
        # Serialize to JSON string
        json_str = integrate1.get_json()
        self.assertIsInstance(json_str, str)
        
        # Create new object and deserialize from string
        integrate2 = tttrlib.DecayFitIntegrateSignals()
        integrate2.set_json(json_str)
        
        # Both should produce the same JSON
        json_str2 = integrate2.get_json()
        self.assertEqual(json_str, json_str2)

    def test_decayfit_has_static_json_methods(self):
        """Test DecayFit has static JSON methods for parameters, data, and model"""
        # DecayFit only has static methods, not instance methods
        self.assertTrue(hasattr(tttrlib.DecayFit, 'parameters_to_json'))
        self.assertTrue(hasattr(tttrlib.DecayFit, 'data_to_json'))
        self.assertTrue(hasattr(tttrlib.DecayFit, 'model_to_json'))
        self.assertTrue(hasattr(tttrlib.DecayFit, 'parameters_from_json'))
        self.assertTrue(hasattr(tttrlib.DecayFit, 'data_from_json'))
        self.assertTrue(hasattr(tttrlib.DecayFit, 'model_from_json'))

    def test_decayfit_from_json_accepts_string(self):
        """Test DecayFit static methods accept strings"""
        # Test with parameters
        param = np.array([2.0, 0.01, 0.38, 1.2])
        json_str = tttrlib.DecayFit.parameters_to_json(param)
        self.assertIsInstance(json_str, str)
        
        # Test with data
        data = np.array([0, 1, 2, 3, 4, 5], dtype=np.int32)
        json_str = tttrlib.DecayFit.data_to_json(data)
        self.assertIsInstance(json_str, str)
        
        # Test with model
        model = np.array([1.0, 2.0, 3.0, 4.0])
        json_str = tttrlib.DecayFit.model_to_json(model)
        self.assertIsInstance(json_str, str)



    def test_json_roundtrip_corrections(self):
        """Test JSON roundtrip for DecayFitCorrections"""
        corrections1 = tttrlib.DecayFitCorrections()
        
        # Serialize and deserialize
        json_str = corrections1.get_json()
        corrections2 = tttrlib.DecayFitCorrections()
        corrections2.set_json(json_str)
        
        # Should be identical
        self.assertEqual(corrections1.get_json(), corrections2.get_json())

    def test_json_roundtrip_settings(self):
        """Test JSON roundtrip for DecayFitSettings"""
        settings1 = tttrlib.DecayFitSettings()
        
        # Serialize and deserialize
        json_str = settings1.get_json()
        settings2 = tttrlib.DecayFitSettings()
        settings2.set_json(json_str)
        
        # Should be identical
        self.assertEqual(settings1.get_json(), settings2.get_json())

    def test_json_roundtrip_integrate_signals(self):
        """Test JSON roundtrip for DecayFitIntegrateSignals"""
        integrate1 = tttrlib.DecayFitIntegrateSignals()
        
        # Serialize and deserialize
        json_str = integrate1.get_json()
        integrate2 = tttrlib.DecayFitIntegrateSignals()
        integrate2.set_json(json_str)
        
        # Should be identical
        self.assertEqual(integrate1.get_json(), integrate2.get_json())

    def test_json_roundtrip_decayfit_parameters(self):
        """Test JSON roundtrip for DecayFit parameters"""
        param1 = np.array([2.0, 0.01, 0.38, 1.2])
        
        # Serialize to JSON string
        json_str = tttrlib.DecayFit.parameters_to_json(param1)
        self.assertIsInstance(json_str, str)
        
        # Deserialize from string
        param2 = np.zeros(4)
        tttrlib.DecayFit.parameters_from_json(json_str, param2)
        
        # Should have valid data
        self.assertTrue(np.any(param2 != 0))

    def test_json_valid_structure_corrections(self):
        """Test that DecayFitCorrections JSON has expected structure"""
        corrections = tttrlib.DecayFitCorrections()
        json_str = corrections.get_json()
        parsed = json_module.loads(json_str)
        
        # Should be a dict
        self.assertIsInstance(parsed, dict)

    def test_json_valid_structure_settings(self):
        """Test that DecayFitSettings JSON has expected structure"""
        settings = tttrlib.DecayFitSettings()
        json_str = settings.get_json()
        parsed = json_module.loads(json_str)
        
        # Should be a dict
        self.assertIsInstance(parsed, dict)

    def test_json_valid_structure_integrate_signals(self):
        """Test that DecayFitIntegrateSignals JSON has expected structure"""
        integrate = tttrlib.DecayFitIntegrateSignals()
        json_str = integrate.get_json()
        parsed = json_module.loads(json_str)
        
        # Should be a dict
        self.assertIsInstance(parsed, dict)

    def test_json_valid_structure_decayfit_parameters(self):
        """Test that DecayFit parameters JSON has expected structure"""
        param = np.array([2.0, 0.01, 0.38, 1.2])
        json_str = tttrlib.DecayFit.parameters_to_json(param)
        parsed = json_module.loads(json_str)
        
        # Should be a dict
        self.assertIsInstance(parsed, dict)


if __name__ == '__main__':
    unittest.main()
