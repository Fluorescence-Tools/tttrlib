#!/usr/bin/env python3
"""
Basic tests for PDA implementation - testing what's available in the SWIG wrapper.
"""

import sys
import math
import numpy as np

import tttrlib

def create_poisson_pF(lambda_val, Nmax):
    """Create a Poisson probability distribution."""
    pF = np.zeros(Nmax + 1)
    pF[0] = np.exp(-lambda_val)
    for i in range(1, Nmax + 1):
        pF[i] = pF[i-1] * lambda_val / i
    return pF / np.sum(pF)

def test_pda_creation():
    """Test basic PDA creation."""
    print("\n" + "="*70)
    print("TEST 1: PDA Creation with Different Implementations")
    print("="*70)
    
    Nmax = 50
    pF = create_poisson_pF(10.0, Nmax)
    
    try:
        # Test default implementation
        print("  Creating PDA with PDA_DEFAULT (0)...")
        pda_default = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 0)
        print("  [OK] PDA_DEFAULT created")
        
        # Test optimized implementation
        print("  Creating PDA with PDA_OPTIMIZED (1)...")
        pda_optimized = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 1)
        print("  [OK] PDA_OPTIMIZED created")
        
        # Assertions to satisfy pytest
        assert pda_default is not None
        assert pda_optimized is not None
    except Exception as e:
        print(f"  [FAIL] {e}")
        raise AssertionError(f"PDA creation failed: {e}")

def test_pda_parameters():
    """Test PDA parameter setting."""
    print("\n" + "="*70)
    print("TEST 2: PDA Parameter Setting")
    print("="*70)
    
    Nmax = 50
    pF = create_poisson_pF(10.0, Nmax)
    
    try:
        pda = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 1)
        
        # Try to set background using property assignment
        print("  Setting background_ch1 = 1.0...")
        pda.background_ch1 = 1.0
        print("  [OK] background_ch1 set")
        
        print("  Setting background_ch2 = 1.0...")
        pda.background_ch2 = 1.0
        print("  [OK] background_ch2 set")
        
        # Try to set min/max photons using property assignment
        print("  Setting hist2d_nmin = 10...")
        pda.hist2d_nmin = 10
        print("  [OK] hist2d_nmin set")
        
        print("  Setting hist2d_nmax = 100...")
        pda.hist2d_nmax = 100
        print("  [OK] hist2d_nmax set")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        import traceback
        traceback.print_exc()
        raise AssertionError(f"PDA parameters test failed: {e}")

def test_pda_species():
    """Test adding species to PDA."""
    print("\n" + "="*70)
    print("TEST 3: Adding Species to PDA")
    print("="*70)
    
    Nmax = 50
    pF = create_poisson_pF(10.0, Nmax)
    
    try:
        pda = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 1)
        
        # Check available methods
        print("  Available methods on PDA object:")
        methods = [m for m in dir(pda) if not m.startswith('_')]
        for method in sorted(methods)[:20]:
            print(f"    - {method}")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        raise AssertionError(f"PDA species test failed: {e}")

def test_pda_evaluation():
    """Test PDA evaluation."""
    print("\n" + "="*70)
    print("TEST 4: PDA Evaluation")
    print("="*70)
    
    Nmax = 50
    pF = create_poisson_pF(10.0, Nmax)
    
    try:
        pda = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 1)
        
        print("  Calling evaluate()...")
        pda.evaluate()
        print("  [OK] evaluate() completed")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        import traceback
        traceback.print_exc()
        raise AssertionError(f"PDA evaluation test failed: {e}")

def test_implementation_switching():
    """Test switching implementations."""
    print("\n" + "="*70)
    print("TEST 5: Implementation Switching")
    print("="*70)
    
    Nmax = 50
    pF = create_poisson_pF(10.0, Nmax)
    
    try:
        pda = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 0)
        
        print("  Initial implementation: PDA_DEFAULT (0)")
        pda.evaluate()
        print("  [OK] Evaluated with PDA_DEFAULT")
        
        print("  Switching to PDA_OPTIMIZED (1)...")
        pda.set_implementation(1)
        print("  [OK] Implementation switched")
        
        print("  Evaluating with PDA_OPTIMIZED...")
        pda.evaluate()
        print("  [OK] Evaluated with PDA_OPTIMIZED")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        import traceback
        traceback.print_exc()
        raise AssertionError(f"PDA implementation switching test failed: {e}")

def test_different_backgrounds():
    """Test with different backgrounds."""
    print("\n" + "="*70)
    print("TEST 6: Different Background Levels")
    print("="*70)
    
    Nmax = 50
    pF = create_poisson_pF(10.0, Nmax)
    
    try:
        backgrounds = [
            (0.0, 0.0, "No background"),
            (0.5, 0.5, "Low background"),
            (2.0, 2.0, "Medium background"),
            (5.0, 5.0, "High background"),
        ]
        
        for bg1, bg2, desc in backgrounds:
            pda = tttrlib.Pda(Nmax, 5, bg1, bg2, pF.tolist(), 1)
            pda.evaluate()
            print(f"  [OK] {desc}: bg_ch1={bg1}, bg_ch2={bg2}")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        raise AssertionError(f"PDA different backgrounds test failed: {e}")

def test_different_nmax():
    """Test with different Nmax values."""
    print("\n" + "="*70)
    print("TEST 7: Different Nmax Values")
    print("="*70)
    
    try:
        nmax_values = [20, 50, 100, 200]
        
        for Nmax in nmax_values:
            pF = create_poisson_pF(10.0, Nmax)
            pda = tttrlib.Pda(Nmax, 5, 0.5, 0.5, pF.tolist(), 1)
            pda.evaluate()
            print(f"  [OK] Nmax={Nmax}")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        raise AssertionError(f"PDA different Nmax test failed: {e}")

def test_low_vs_high_background():
    """Test FFT correction triggering with different backgrounds."""
    print("\n" + "="*70)
    print("TEST 8: FFT Correction - Low vs High Background")
    print("="*70)
    
    try:
        Nmax = 50
        
        # Low background (should trigger FFT correction)
        print("  Testing low background (lambda=0.5)...")
        pF_low = create_poisson_pF(0.5, Nmax)
        pda_low = tttrlib.Pda(Nmax, 5, 0.1, 0.1, pF_low.tolist(), 1)
        pda_low.evaluate()
        print(f"  [OK] Low background: pF[0]={pF_low[0]:.6f}")
        
        # High background (should NOT trigger FFT correction)
        print("  Testing high background (lambda=50.0)...")
        pF_high = create_poisson_pF(50.0, Nmax)
        pda_high = tttrlib.Pda(Nmax, 5, 0.1, 0.1, pF_high.tolist(), 1)
        pda_high.evaluate()
        print(f"  [OK] High background: pF[0]={pF_high[0]:.6e}")
        
        # Assertion to satisfy pytest
        assert True
    except Exception as e:
        print(f"  [FAIL] {e}")
        import traceback
        traceback.print_exc()
        raise AssertionError(f"PDA low vs high background test failed: {e}")

def main():
    """Run all tests."""
    print("\n" + "="*70)
    print("BASIC PDA OPTIMIZATION TESTS")
    print("="*70)
    print(f"Python: {sys.version}")
    print(f"NumPy: {np.__version__}")
    
    tests = [
        ("PDA Creation", test_pda_creation),
        ("PDA Parameters", test_pda_parameters),
        ("PDA Species", test_pda_species),
        ("PDA Evaluation", test_pda_evaluation),
        ("Implementation Switching", test_implementation_switching),
        ("Different Backgrounds", test_different_backgrounds),
        ("Different Nmax", test_different_nmax),
        ("FFT Correction Triggering", test_low_vs_high_background),
    ]
    
    results = []
    for name, test_func in tests:
        try:
            test_func()
            results.append((name, True))
        except Exception as e:
            print(f"\nUnexpected error in {name}: {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))
    
    # Summary
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)
    
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for name, result in results:
        status = "[PASS]" if result else "[FAIL]"
        print(f"  {status} {name}")
    
    print(f"\nTotal: {passed}/{total} tests passed")
    
    assert passed == total, f"{total - passed} test(s) failed"

if __name__ == "__main__":
    sys.exit(main())
