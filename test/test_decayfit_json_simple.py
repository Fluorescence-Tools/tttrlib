"""Test JSON serialization for DecayFit classes"""

from __future__ import division

import unittest
import numpy as np
import json

import tttrlib
from misc.compute_irf import model_irf


class TestDecayFitJSON(unittest.TestCase):

    def test_decayfit_corrections_json(self):
        """Test JSON serialization for DecayFitCorrections"""
        corrections = tttrlib.DecayFitCorrections()
        corrections.gamma = 0.01
        corrections.g = 1.0
        corrections.l1 = 0.1
        corrections.l2 = 0.2
        corrections.period = 32.0
        corrections.convolution_stop = 63
        
        # Serialize to JSON string
        json_str = corrections.get_json()
        j = json.loads(json_str)
        self.assertEqual(j['gamma'], 0.01)
        self.assertEqual(j['g'], 1.0)
        self.assertEqual(j['l1'], 0.1)
        self.assertEqual(j['l2'], 0.2)
        self.assertEqual(j['period'], 32.0)
        self.assertEqual(j['convolution_stop'], 63)
        
        # Deserialize from JSON string
        corrections2 = tttrlib.DecayFitCorrections()
        corrections2.set_json(json_str)
        
        # Check values
        self.assertAlmostEqual(corrections2.gamma, 0.01)
        self.assertAlmostEqual(corrections2.g, 1.0)
        self.assertAlmostEqual(corrections2.l1, 0.1)
        self.assertAlmostEqual(corrections2.l2, 0.2)
        self.assertAlmostEqual(corrections2.period, 32.0)
        self.assertEqual(corrections2.convolution_stop, 63)

    def test_decayfit_signals_json(self):
        """Test JSON serialization for DecayFitIntegrateSignals"""
        signals = tttrlib.DecayFitIntegrateSignals()
        signals.Sp = 100.0
        signals.Ss = 50.0
        signals.Bp = 10.0
        signals.Bs = 5.0
        signals.B = 15.0
        signals.Bexpected = 14.5
        
        # Serialize to JSON string
        json_str = signals.get_json()
        j = json.loads(json_str)
        self.assertEqual(j['Sp'], 100.0)
        self.assertEqual(j['Ss'], 50.0)
        self.assertEqual(j['Bp'], 10.0)
        self.assertEqual(j['Bs'], 5.0)
        self.assertEqual(j['B'], 15.0)
        self.assertEqual(j['Bexpected'], 14.5)
        
        # Deserialize from JSON string
        signals2 = tttrlib.DecayFitIntegrateSignals()
        signals2.set_json(json_str)
        
        # Check values
        self.assertAlmostEqual(signals2.Sp, 100.0)
        self.assertAlmostEqual(signals2.Ss, 50.0)
        self.assertAlmostEqual(signals2.Bp, 10.0)
        self.assertAlmostEqual(signals2.Bs, 5.0)
        self.assertAlmostEqual(signals2.B, 15.0)
        self.assertAlmostEqual(signals2.Bexpected, 14.5)

    def test_decayfit_settings_json(self):
        """Test JSON serialization for DecayFitSettings"""
        settings = tttrlib.DecayFitSettings()
        settings.fixedrho = 1
        settings.softbifl = 0
        settings.p2s_twoIstar = 1
        settings.firstcall = 0
        settings.penalty = 0.5
        
        # Serialize to JSON string
        json_str = settings.get_json()
        j = json.loads(json_str)
        self.assertEqual(j['fixedrho'], 1)
        self.assertEqual(j['softbifl'], 0)
        self.assertEqual(j['p2s_twoIstar'], 1)
        self.assertEqual(j['firstcall'], 0)
        self.assertEqual(j['penalty'], 0.5)
        
        # Deserialize from JSON string
        settings2 = tttrlib.DecayFitSettings()
        settings2.set_json(json_str)
        
        # Check values
        self.assertEqual(settings2.fixedrho, 1)
        self.assertEqual(settings2.softbifl, 0)
        self.assertEqual(settings2.p2s_twoIstar, 1)
        self.assertEqual(settings2.firstcall, 0)
        self.assertAlmostEqual(settings2.penalty, 0.5)

    def test_decayfit23_json(self):
        """Test JSON serialization for DecayFit23 fit results"""
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
        
        # Perform fit
        twoIstar = tttrlib.DecayFit23.fit(x, fixed, m_param)
        
        # Serialize to JSON string
        json_str = tttrlib.DecayFit23.to_json(x, fixed, m_param, twoIstar)
        j = json.loads(json_str)
        
        # Verify JSON structure
        self.assertIn('parameters', j)
        self.assertIn('fixed', j)
        self.assertIn('result', j)
        self.assertEqual(len(j['parameters']), 8)
        self.assertEqual(len(j['fixed']), 4)
        self.assertAlmostEqual(j['result'], twoIstar)
        
        # Test deserialization
        x2 = np.zeros(8, dtype=np.float64)
        fixed2 = np.zeros(4, dtype=np.int16)
        tttrlib.DecayFit23.from_json(json_str, x2, fixed2)
        
        # Check values
        np.testing.assert_array_almost_equal(x2, x)
        np.testing.assert_array_equal(fixed2, fixed)


if __name__ == '__main__':
    unittest.main()
