"""
Unit tests for DecayFitMLEWrapper
"""

import unittest
import numpy as np
import tttrlib
from misc.compute_irf import model_irf


@unittest.skip("DecayFitMLEWrapper not ready - skipping all tests")
class TestDecayFitMLEWrapper(unittest.TestCase):
    
    def setUp(self):
        """Setup common test data."""
        # IRF parameters
        self.n_channels = 32
        self.period = 32.0
        self.dt = self.period / self.n_channels
        
        # Create IRF
        self.irf, self.time_axis = model_irf(
            n_channels=self.n_channels,
            period=self.period,
            irf_position_p=2.0,
            irf_position_s=18.0,
            irf_width=0.25
        )
        
        # Background
        self.background = np.zeros_like(self.irf)
        
        # Corrections
        self.g_factor = 1.0
        self.l1 = 0.1
        self.l2 = 0.1
        self.convolution_stop = self.n_channels - 1
    
    def test_mle_fit_settings_creation(self):
        """Test MLEFitSettings creation."""
        settings = tttrlib.MLEFitSettings()
        
        # Check defaults
        self.assertEqual(settings.dt, 1.0)
        self.assertEqual(settings.period, 100.0)
        self.assertEqual(settings.g_factor, 1.0)
        self.assertEqual(settings.minimum_n_photons, 10)
        
        # Set values
        settings.dt = self.dt
        settings.period = self.period
        settings.irf = self.irf.tolist()
        settings.background = self.background.tolist()
        
        self.assertEqual(settings.dt, self.dt)
        self.assertEqual(len(settings.irf), len(self.irf))
    
    def test_model23_wrapper_creation(self):
        """Test creation of Model 23 wrapper."""
        
        # Initial parameters for Model 23
        initial_params = [4.0, 0.01, 0.38, 1.2]  # tau, gamma, r0, rho
        fixed = [0, 1, 1, 1]  # Only fit tau
        
        # Create settings
        settings = tttrlib.MLEFitSettings()
        settings.irf = self.irf.tolist()
        settings.background = self.background.tolist()
        settings.dt = self.dt
        settings.period = self.period
        settings.g_factor = self.g_factor
        settings.l1 = self.l1
        settings.l2 = self.l2
        settings.convolution_stop = self.convolution_stop
        settings.initial_parameters = initial_params
        settings.fixed = fixed
        
        # Create wrapper
        wrapper = tttrlib.DecayFitMLEWrapper(
            tttrlib.DecayFitMLEWrapper.MODEL_23,
            settings
        )
        
        self.assertIsNotNone(wrapper)
        self.assertEqual(wrapper.get_model_type(), tttrlib.DecayFitMLEWrapper.MODEL_23)
    
    def test_model24_wrapper_creation(self):
        """Test creation of Model 24 wrapper."""
        
        # Initial parameters for Model 24
        initial_params = [2.0, 0.01, 4.0, 0.5, 0.0]  # tau1, gamma, tau2, A2, offset
        fixed = [0, 1, 0, 0, 1]
        
        # Create settings
        settings = tttrlib.MLEFitSettings()
        settings.irf = self.irf.tolist()
        settings.background = self.background.tolist()
        settings.dt = self.dt
        settings.period = self.period
        settings.initial_parameters = initial_params
        settings.fixed = fixed
        
        # Create wrapper
        wrapper = tttrlib.DecayFitMLEWrapper(
            tttrlib.DecayFitMLEWrapper.MODEL_24,
            settings
        )
        
        self.assertIsNotNone(wrapper)
        self.assertEqual(wrapper.get_model_type(), tttrlib.DecayFitMLEWrapper.MODEL_24)
    
    def test_mle_fit_result(self):
        """Test MLEFitResult structure."""
        result = tttrlib.MLEFitResult()
        
        # Check defaults
        self.assertEqual(result.twoIstar, 0.0)
        self.assertEqual(result.n_photons, 0)
        self.assertFalse(result.success)
        
        # Set values
        result.parameters = [4.0, 0.01, 0.38, 1.2]
        result.twoIstar = 0.5
        result.n_photons = 1000
        result.success = True
        
        self.assertEqual(len(result.parameters), 4)
        self.assertEqual(result.twoIstar, 0.5)
        self.assertEqual(result.n_photons, 1000)
        self.assertTrue(result.success)
    
    def test_python_wrapper_model23(self):
        """Test Python MLEFitter wrapper for Model 23."""
        
        try:
            from tttrlib import create_model23_fitter
        except ImportError:
            self.skipTest("Python wrapper not available")
        
        # Create fitter
        fitter = create_model23_fitter(
            irf=self.irf,
            background=self.background,
            dt=self.dt,
            period=self.period,
            g_factor=self.g_factor,
            l1=self.l1,
            l2=self.l2,
            initial_tau=4.0,
            initial_gamma=0.01,
            initial_r0=0.38,
            initial_rho=1.2,
            fit_lifetime=True,
            fit_gamma=False
        )
        
        self.assertIsNotNone(fitter)
        
        # Check parameter names
        param_names = fitter.get_parameter_names()
        self.assertIn('tau', param_names)
        self.assertIn('gamma', param_names)
        self.assertIn('twoIstar', param_names)
    
    def test_python_wrapper_model24(self):
        """Test Python MLEFitter wrapper for Model 24."""
        
        try:
            from tttrlib import MLEFitter
        except ImportError:
            self.skipTest("Python wrapper not available")
        
        # Create fitter
        fitter = MLEFitter(
            model_type='model24',
            irf=self.irf,
            background=self.background,
            dt=self.dt,
            period=self.period,
            initial_parameters=[2.0, 0.01, 4.0, 0.5, 0.0],
            fixed=[0, 1, 0, 0, 1]
        )
        
        self.assertIsNotNone(fitter)
        
        # Check parameter names
        param_names = fitter.get_parameter_names()
        self.assertIn('tau1', param_names)
        self.assertIn('tau2', param_names)
        self.assertIn('A2', param_names)
    
    def test_invalid_model_type(self):
        """Test that invalid model type raises error."""
        
        try:
            from tttrlib import MLEFitter
        except ImportError:
            self.skipTest("Python wrapper not available")
        
        with self.assertRaises(ValueError):
            fitter = MLEFitter(
                model_type='invalid_model',
                irf=self.irf,
                initial_parameters=[4.0]
            )
    
    def test_missing_irf_error(self):
        """Test that missing IRF raises error."""
        
        try:
            from tttrlib import MLEFitter
        except ImportError:
            self.skipTest("Python wrapper not available")
        
        with self.assertRaises(ValueError):
            fitter = MLEFitter(
                model_type='model23',
                irf=None,
                initial_parameters=[4.0, 0.01, 0.38, 1.2]
            )
    
    def test_missing_initial_parameters_error(self):
        """Test that missing initial parameters raises error."""
        
        try:
            from tttrlib import MLEFitter
        except ImportError:
            self.skipTest("Python wrapper not available")
        
        with self.assertRaises(ValueError):
            fitter = MLEFitter(
                model_type='model23',
                irf=self.irf,
                initial_parameters=None
            )
    
    def test_settings_update(self):
        """Test updating settings."""
        
        initial_params = [4.0, 0.01, 0.38, 1.2]
        
        settings = tttrlib.MLEFitSettings()
        settings.irf = self.irf.tolist()
        settings.background = self.background.tolist()
        settings.dt = self.dt
        settings.period = self.period
        settings.initial_parameters = initial_params
        settings.minimum_n_photons = 50
        
        wrapper = tttrlib.DecayFitMLEWrapper(
            tttrlib.DecayFitMLEWrapper.MODEL_23,
            settings
        )
        
        # Get settings
        retrieved_settings = wrapper.get_settings()
        self.assertEqual(retrieved_settings.minimum_n_photons, 50)
        
        # Update settings
        new_settings = tttrlib.MLEFitSettings()
        new_settings.irf = self.irf.tolist()
        new_settings.background = self.background.tolist()
        new_settings.dt = self.dt
        new_settings.period = self.period
        new_settings.initial_parameters = initial_params
        new_settings.minimum_n_photons = 100
        
        wrapper.set_settings(new_settings)
        
        retrieved_settings = wrapper.get_settings()
        self.assertEqual(retrieved_settings.minimum_n_photons, 100)


if __name__ == '__main__':
    unittest.main()
