#!/usr/bin/env python3
"""
Test script to verify PDA implementation selection feature.
Tests both PDA_DEFAULT and PDA_OPTIMIZED implementations.
"""

import sys
import math
import numpy as np

import tttrlib

def test_pda_implementation_selection():
    """Test that PDA implementation can be selected and switched."""
    
    print("=" * 60)
    print("Testing PDA Implementation Selection")
    print("=" * 60)
    
    # Create a simple pF distribution (Poisson-like)
    Nmax = 50
    pF = np.zeros(Nmax + 1)
    lam = 10.0  # lambda for Poisson
    for i in range(Nmax + 1):
        pF[i] = np.exp(-lam) * (lam ** i) / math.factorial(i)
    
    # Normalize
    pF = pF / np.sum(pF)
    
    # Test 1: Create PDA with default implementation
    print("\n[Test 1] Creating PDA with PDA_DEFAULT implementation...")
    pda_default = tttrlib.Pda(
        hist2d_nmax=Nmax,
        hist2d_nmin=5,
        background_ch1=0.5,
        background_ch2=0.5,
        pF=pF.tolist()
    )
    
    # Add some species
    pda_default.append(0.5, 0.3)  # 50% amplitude, 30% green probability
    pda_default.append(0.5, 0.7)  # 50% amplitude, 70% green probability
    
    # Evaluate with default implementation
    print("Evaluating with PDA_DEFAULT...")
    pda_default.evaluate()
    s1s2_default = pda_default.s1s2
    dim1, dim2 = s1s2_default.shape
    print(f"  S1S2 matrix shape: {dim1} x {dim2}")
    print(f"  S1S2 matrix sum: {np.sum(s1s2_default):.6f}")
    
    # Test 2: Create PDA with optimized implementation
    print("\n[Test 2] Creating PDA with PDA_OPTIMIZED implementation...")
    # Use numeric value for now (PDA_OPTIMIZED = 1)
    pda_optimized = tttrlib.Pda(
        hist2d_nmax=Nmax,
        hist2d_nmin=5,
        background_ch1=0.5,
        background_ch2=0.5,
        pF=pF.tolist(),
        implementation=1  # PDA_OPTIMIZED
    )
    
    # Add the same species
    pda_optimized.append(0.5, 0.3)
    pda_optimized.append(0.5, 0.7)
    
    # Evaluate with optimized implementation
    print("Evaluating with PDA_OPTIMIZED...")
    pda_optimized.evaluate()
    s1s2_optimized = pda_optimized.s1s2
    dim1, dim2 = s1s2_optimized.shape
    print(f"  S1S2 matrix shape: {dim1} x {dim2}")
    print(f"  S1S2 matrix sum: {np.sum(s1s2_optimized):.6f}")
    
    # Test 3: Verify results are the same
    print("\n[Test 3] Comparing results...")
    max_diff = np.max(np.abs(s1s2_default - s1s2_optimized))
    print(f"  Maximum difference between implementations: {max_diff:.2e}")
    
    assert max_diff < 1e-10, f"Results don't match between implementations (diff: {max_diff})"
    
    # Test 4: Test switching implementations
    print("\n[Test 4] Testing implementation switching...")
    pda_switch = tttrlib.Pda(
        hist2d_nmax=Nmax,
        hist2d_nmin=5,
        background_ch1=0.5,
        background_ch2=0.5,
        pF=pF.tolist()
    )
    pda_switch.append(0.5, 0.3)
    pda_switch.append(0.5, 0.7)
    
    # Start with default
    print("  Initial implementation: PDA_DEFAULT")
    pda_switch.evaluate()
    s1s2_1 = pda_switch.s1s2
    dim1, dim2 = s1s2_1.shape
    
    # Switch to optimized
    print("  Switching to: PDA_OPTIMIZED")
    pda_switch.set_implementation(1)  # PDA_OPTIMIZED
    pda_switch.evaluate()
    s1s2_2 = pda_switch.s1s2
    dim1_2, dim2_2 = s1s2_2.shape
    
    # Compare
    max_diff_switch = np.max(np.abs(s1s2_1 - s1s2_2))
    print(f"  Maximum difference after switching: {max_diff_switch:.2e}")
    
    assert max_diff_switch < 1e-10, f"Implementation switching failed (diff: {max_diff_switch})"
    
    print("\n" + "=" * 60)
    print("All tests completed successfully!")
    print("=" * 60)

if __name__ == "__main__":
    test_pda_implementation_selection()
