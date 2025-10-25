"""CLSM memory and EGFP unit tests"""
from __future__ import division

import os
import unittest
import json
from pathlib import Path
import gc

import tttrlib

settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
settings = json.load(open(settings_path))

repo_root = Path(__file__).resolve().parents[1]
env_root = os.getenv("TTTRLIB_DATA")
if env_root:
    env_root = env_root.strip().strip('\'\"`')
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



if __name__ == '__main__':
    try:
        result = test_eGFP_memory_usage()
        
        if result:
            print("\n" + "=" * 80)
            print("SUMMARY")
            print("=" * 80)
            print(f"Image: {result['n_frames']}×{result['n_lines']}×{result['n_pixels']}")
            print(f"Photons: {result['total_photons']:,} ({result['avg_photons_per_pixel']:.1f}/pixel)")
            print(f"Memory: {format_bytes(result['total_memory'])}")
            print(f"Efficiency: {result['efficiency']:.1f}%")
            print("=" * 80)
    
    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
