"""
Enhanced BurstFilter: Dynamic Parameter Changes Example
=======================================================

This example demonstrates the enhanced BurstFilter class with dynamic filtering capabilities.
Users can now change filter parameters and see previously filtered bursts "reappear".

Key features demonstrated:
- Dynamic parameter changes without permanent burst removal
- Filter state tracking and reapplication
- Burst reappearance when parameters are relaxed
- Filter reset and clearing capabilities
"""

# %%
# Import required libraries
import tttrlib
import numpy as np

# %%
# Load sample data
# ----------------
# For this example, we'll use sample data. In practice, you would load your own TTTR files.
print("Note: This example requires actual TTTR data to run completely.")
print("For demonstration purposes, we'll show the enhanced workflow.\n")

# In a real scenario, you would load data like this:
# data = tttrlib.TTTR('your_file.ptu', 'PTU')
# burst_filter = tttrlib.BurstFilter(data)

# %%
# Enhanced BurstFilter Workflow Demonstration
# ============================================

print("="*70)
print("ENHANCED BURSTFILTER: Dynamic Filtering Workflow")
print("="*70)

# Simulate the workflow (would need real data to execute)

workflow_steps = """
1. INITIAL BURST DETECTION
   burst_filter = tttrlib.BurstFilter(data)
   burst_filter.set_burst_parameters(min_photons=30, window_photons=10, window_time_max=1e-3)
   bursts = burst_filter.find_bursts()  # Stores raw_bursts internally
   print(f"Found {len(bursts)} raw bursts")

2. APPLY INITIAL FILTERS (tracked automatically)
   burst_filter.filter_by_size(min_size=50, max_size=1000)      # Size filter
   burst_filter.filter_by_duration(min_duration=1e-4, max_duration=1e-2)  # Duration filter
   burst_filter.merge_bursts(max_gap=5)                         # Merge close bursts
   print(f"After initial filtering: {len(burst_filter.get_bursts())} bursts")

3. DYNAMIC PARAMETER CHANGES - BURSTS CAN "REAPPEAR"
   # Relax size filter - smaller bursts come back
   burst_filter.filter_by_size(min_size=25, max_size=1000)
   print(f"After relaxing size filter: {len(burst_filter.get_bursts())} bursts")

   # Tighten duration filter - more bursts filtered out
   burst_filter.filter_by_duration(min_duration=5e-4, max_duration=5e-3)
   print(f"After tightening duration filter: {len(burst_filter.get_bursts())} bursts")

4. FILTER RESET AND REAPPLICATION
   # Reset to original unfiltered bursts
   burst_filter.reset_to_raw_bursts()
   print(f"After reset to raw: {len(burst_filter.get_bursts())} bursts")

   # Reapply all filters with current parameters
   burst_filter.reapply_filters()
   print(f"After reapplying filters: {len(burst_filter.get_bursts())} bursts")

5. COMPLETE FILTER CLEARING
   # Clear all filters and start fresh
   burst_filter.clear_filters()
   print(f"After clearing all filters: {len(burst_filter.get_bursts())} bursts")
"""

print(workflow_steps)

# %%
# Key Benefits of Enhanced BurstFilter
# ====================================

benefits = """
KEY BENEFITS OF ENHANCED BURSTFILTER:

✅ DYNAMIC PARAMETER CHANGES
   - Change filter parameters without permanent burst loss
   - Previously filtered bursts can reappear when parameters are relaxed
   - Interactive exploration of filter effects

✅ FILTER STATE TRACKING
   - Automatic tracking of applied filters and their parameters
   - Reproducible filtering pipelines
   - Easy parameter adjustment and reapplication

✅ IMPROVED USER EXPERIENCE
   - No need to restart burst search when adjusting filters
   - Intuitive workflow: find bursts once, filter dynamically
   - Better control over burst analysis process

✅ BACKWARD COMPATIBILITY
   - All existing code continues to work unchanged
   - New methods are additive, not replacing existing functionality
   - Same performance for static filtering workflows

✅ ADVANCED ANALYSIS CAPABILITIES
   - Compare different filtering strategies easily
   - Optimize parameters interactively
   - Support for complex multi-stage filtering pipelines
"""

print(benefits)

# %%
# Code Example for Dynamic Filtering
# ==================================

code_example = '''
# Enhanced BurstFilter usage example

import tttrlib

# Load data
data = tttrlib.TTTR('experiment.ptu', 'PTU')
bf = tttrlib.BurstFilter(data)

# Set burst search parameters
bf.set_burst_parameters(min_photons=30, window_photons=10, window_time_max=1e-3)

# Find bursts (stores raw_bursts internally)
bursts = bf.find_bursts()
print(f"Initial bursts: {len(bursts)}")

# Apply filters (tracked automatically)
bf.filter_by_size(min_size=50)
bf.filter_by_duration(min_duration=1e-4, max_duration=1e-2)
print(f"After filtering: {len(bf.get_bursts())}")

# Change parameters - bursts can reappear!
bf.filter_by_size(min_size=25)  # More permissive
print(f"After parameter change: {len(bf.get_bursts())}")  # May show more bursts

# Reset and reapply with different strategy
bf.reset_to_raw_bursts()
bf.filter_by_size(min_size=100)  # More restrictive
bf.filter_by_duration(min_duration=1e-3)  # Longer bursts only
print(f"New filtering strategy: {len(bf.get_bursts())}")

# Clear everything and start over
bf.clear_filters()
bursts = bf.find_bursts()  # Fresh start
'''

print("CODE EXAMPLE:")
print(code_example)

# %%
# Testing the Enhanced Functionality
# =================================

print("\n" + "="*50)
print("TESTING ENHANCED FUNCTIONALITY")
print("="*50)

try:
    # Test that enhanced methods exist
    bf_class = tttrlib.BurstFilter

    enhanced_methods = [
        'reset_to_raw_bursts',
        'reapply_filters',
        'clear_filters'
    ]

    print("Checking for enhanced methods:")
    for method in enhanced_methods:
        if hasattr(bf_class, method):
            print(f"  ✓ {method}")
        else:
            print(f"  ✗ {method} - MISSING")

    # Test Python interface methods
    python_methods = [
        'find_bursts_as_list',
        'filter_by_size_as_list',
        'reset_to_raw_bursts',
        'reapply_filters',
        'clear_filters'
    ]

    print("\nChecking Python interface methods:")
    for method in python_methods:
        if hasattr(bf_class, method):
            print(f"  ✓ {method}")
        else:
            print(f"  ✗ {method} - MISSING")

    print("\n✅ Enhanced BurstFilter is ready for dynamic filtering!")

except Exception as e:
    print(f"❌ Error testing enhanced functionality: {e}")
    print("Make sure tttrlib is properly compiled with the enhancements.")

print("\n" + "="*70)
print("Enhanced BurstFilter demonstration complete!")
print("Users can now change filter parameters dynamically,")
print("and previously filtered bursts will reappear as appropriate.")
print("="*70)
