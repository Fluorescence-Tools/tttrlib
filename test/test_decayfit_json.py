from __future__ import division

import unittest
import numpy as np
import json

import tttrlib
from misc.compute_irf import model_irf


class Tests(unittest.TestCase):

    def test_decayfit_corrections_json(self):
        # Test JSON serialization/deserialization for DecayFitCorrections
        corrections = tttrlib.DecayFitCorrections()
        corrections.gamma = 0.01
        corrections.g = 1.0
        corrections.l1 = 0.1
        corrections.l2 = 0.2
        corrections.period = 32.0
        corrections.convolution_stop = 63
        
        # Serialize to JSON
        json_str = corrections.get_json()
        print("Corrections JSON:", json_str)
        
        # Deserialize from JSON
        corrections2 = tttrlib.DecayFitCorrections()
        corrections2.set_json(json_str)
        
        # Check values
        self.assertAlmostEqual(corrections2.gamma, 0.01)
        self.assertAlmostEqual(corrections2.g, 1.0)
        self.assertAlmostEqual(corrections2.l1, 0.1)
        self.assertAlmostEqual(corrections2.l2, 0.2)
        self.assertAlmostEqual(corrections2.period, 32.0)
        self.assertEqual(corrections2.convolution_stop, 63)
        
    def test_decayfit_parameters_json(self):
        # Test JSON interface for parameters
        param = np.array([2.0, 0.01, 0.38, 1.2])
        
        # Serialize to JSON (using simplified name)
        json_str = tttrlib.DecayFit.parameters_to_json(param)
        print("Parameters JSON:", json_str)
        
        # Parse JSON to verify
        json_data = json.loads(json_str)
        self.assertIn('parameters', json_data)
        self.assertEqual(len(json_data['parameters']), 4)
        self.assertAlmostEqual(json_data['parameters'][0], 2.0)
        self.assertAlmostEqual(json_data['parameters'][1], 0.01)
        self.assertAlmostEqual(json_data['parameters'][2], 0.38)
        self.assertAlmostEqual(json_data['parameters'][3], 1.2)
        
    def test_decayfit_data_json(self):
        # Test JSON interface for data
        data = np.array([0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
                1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], dtype=np.int32)
        
        # Serialize to JSON (using simplified name)
        json_str = tttrlib.DecayFit.data_to_json(data)
        print("Data JSON:", json_str)
        
        # Parse JSON to verify
        json_data = json.loads(json_str)
        self.assertEqual(len(json_data['data']), len(data))
        for i in range(len(data)):
            self.assertEqual(json_data['data'][i], data[i])
            
    # Note: test_decayfit23_modelf_json requires additional methods not yet implemented
    # def test_decayfit23_modelf_json(self):
    #     pass
        
    def test_complete_workflow_json(self):
        # Test a complete workflow with JSON serialization
        
        # Setup parameters
        n_channels = 32
        irf_position_p = 2.0
        irf_position_s = 18.0
        irf_width = 0.25
        period, g, l1, l2, conv_stop = 32, 1.0, 0.1, 0.1, n_channels // 2
        tau, gamma, r0, rho = 2.0, 0.01, 0.38, 1.2
        np.random.seed(0)
        
        # Create IRF and data
        irf_np, time_axis = model_irf(
            n_channels=n_channels,
            period=period,
            irf_position_p=irf_position_p,
            irf_position_s=irf_position_s,
            irf_width=irf_width
        )
        dt = time_axis[1] - time_axis[0]
        conv_stop = min(len(time_axis) // 2 - 1, conv_stop)
        corrections = np.array([period, g, l1, l2, conv_stop])
        
        # Create test data
        data = [
            0, 0, 0, 1, 9, 7, 5, 5, 5, 2, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 2, 2, 2, 3, 0, 1, 0,
            1, 1, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        ]
        bg = np.zeros_like(irf_np)
        
        # Create MParam structure
        m_param = tttrlib.CreateMParam(
            irf=irf_np,
            background=bg,
            corrections=corrections,
            dt=dt,
            data=data
        )
        
        # Setup fitting parameters
        bifl_scatter = -1
        p_2s = 0
        tau = 2.1
        x = np.zeros(8, dtype=np.float64)
        x[:6] = [tau, gamma, r0, rho, bifl_scatter, p_2s]
        fixed = np.array([0, 0, 1, 1], dtype=np.int16)  # lifetime fitted
        
        # Serialize initial parameters to JSON (using simplified names)
        initial_params_json = tttrlib.DecayFit.parameters_to_json(x)
        fixed_params_json = tttrlib.DecayFit.data_to_json(fixed)
        
        print("Initial parameters JSON:", initial_params_json)
        print("Fixed parameters JSON:", fixed_params_json)
        
        # Perform fit (using wrapper that handles numpy arrays)
        twoIstar = tttrlib.DecayFit23.fit(x, fixed, m_param)
        print("Fit result:", twoIstar)
        
        # Serialize fit results to JSON (using simplified names)
        params_json = tttrlib.DecayFit23.to_json(x, fixed, m_param, twoIstar)
        print("Fit JSON:", params_json[:200], "...")
        
        # Verify JSON structure
        result_data = json.loads(params_json)
        self.assertIn('parameters', result_data)
        self.assertIn('fixed', result_data)
        self.assertIn('result', result_data)
        self.assertEqual(len(result_data['parameters']), 8)
        self.assertEqual(len(result_data['fixed']), 4)
        self.assertAlmostEqual(result_data['result'], twoIstar)
        
        # Test deserialization from JSON
        params_json_str = tttrlib.DecayFit23.to_json(x, fixed, m_param, twoIstar)
        x_restored = np.zeros(8, dtype=np.float64)
        fixed_restored = np.zeros(4, dtype=np.int16)
        tttrlib.DecayFit23.from_json(params_json_str, x_restored, fixed_restored)
        
        # Verify restored values
        np.testing.assert_array_almost_equal(x_restored, x)
        np.testing.assert_array_equal(fixed_restored, fixed)

if __name__ == '__main__':
    unittest.main()
