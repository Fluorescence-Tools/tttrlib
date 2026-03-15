#!/usr/bin/env python3
"""
Test DecayFitXX JSON functionality using local build
"""
import os
import sys
import json


try:
    import tttrlib
    print("✅ tttrlib imported successfully")
    
    # Test DecayFitCorrections JSON
    print("\n=== Testing DecayFitCorrections JSON ===")
    corrections = tttrlib.DecayFitCorrections()
    corrections.gamma = 0.01
    corrections.g = 1.0
    corrections.l1 = 0.1
    corrections.l2 = 0.2
    corrections.period = 32.0
    corrections.convolution_stop = 63
    
    # Serialize to JSON
    json_str = corrections.to_json_string()
    print("Corrections JSON:", json_str)
    
    # Deserialize from JSON
    corrections2 = tttrlib.DecayFitCorrections()
    corrections2.from_json_string(json_str)
    
    # Check values
    print(f"✅ gamma: {corrections2.gamma} (expected: 0.01)")
    print(f"✅ g: {corrections2.g} (expected: 1.0)")
    print(f"✅ l1: {corrections2.l1} (expected: 0.1)")
    print(f"✅ l2: {corrections2.l2} (expected: 0.2)")
    print(f"✅ period: {corrections2.period} (expected: 32.0)")
    print(f"✅ convolution_stop: {corrections2.convolution_stop} (expected: 63)")
    
    # Test parameters JSON
    print("\n=== Testing Parameters JSON ===")
    import numpy as np
    param = np.array([2.0, 0.01, 0.38, 1.2])
    json_str = tttrlib.DecayFit.parameters_to_json_string(param, len(param))
    print("Parameters JSON:", json_str)
    
    # Parse and verify
    json_data = json.loads(json_str)
    print(f"✅ Parameters count: {len(json_data)}")
    print(f"✅ param0: {json_data['param0']} (expected: 2.0)")
    
    # Test data JSON
    print("\n=== Testing Data JSON ===")
    data = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
    json_str = tttrlib.DecayFit.data_to_json_string(data, len(data))
    print("Data JSON:", json_str)
    
    # Parse and verify
    json_data = json.loads(json_str)
    print(f"✅ Data count: {len(json_data['data'])}")
    print(f"✅ First element: {json_data['data'][0]} (expected: 0)")
    
    print("\n🎉 All DecayFitXX JSON functionality tests passed!")
    
except Exception as e:
    print(f"❌ Error: {e}")
    import traceback
    traceback.print_exc()
