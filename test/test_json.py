import sys
import os
import json

try:
    import tttrlib
    print("SUCCESS: tttrlib imported")
    
    # Test JSON functionality
    corrections = tttrlib.DecayFitCorrections()
    print("SUCCESS: DecayFitCorrections created")
    
    corrections.gamma = 0.01
    json_str = corrections.to_json_string()
    print("JSON output:", json_str)
    
    # Test parameters
    param = [1.0, 2.0, 3.0]
    json_param = tttrlib.DecayFit.parameters_to_json_string(param, len(param))
    print("Parameters JSON:", json_param)
    
    print("All JSON tests passed!")
    
except Exception as e:
    print("ERROR:", str(e))
    import traceback
    traceback.print_exc()
