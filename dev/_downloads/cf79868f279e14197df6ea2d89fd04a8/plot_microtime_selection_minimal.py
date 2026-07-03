"""
Minimal Microtime Range Selection Example
=========================================

This example demonstrates microtime range selection for burst analysis without plotting.
"""
import tttrlib
import numpy as np

def select_photons_by_microtime(tttr_data, ranges):
    """Select photons that fall within specified microtime ranges."""
    micro_times = tttr_data.get_micro_times()
    selected_indices = []
    
    for i, micro_time in enumerate(micro_times):
        # Check if microtime falls within any of the specified ranges
        for min_mt, max_mt in ranges:
            if min_mt <= micro_time <= max_mt:
                selected_indices.append(i)
                break  # Found a match, no need to check other ranges
    
    return selected_indices

from examples._example_data import get_data_path

# Load data
data = tttrlib.TTTR(str(get_data_path("bh/bh_spc132.spc")), "SPC-130")
print(f"Total events: {len(data)}")

# Analyze microtime distribution
micro_times = data.get_micro_times()
print(f"Microtime range: {micro_times.min()} - {micro_times.max()}")

# Define microtime ranges for selection
microtime_ranges = [
    (0, 1000),      # Early photons
    (2000, 3000),   # Middle photons  
    (3500, 4095)    # Late photons
]

print("\nSelected microtime ranges:")
for i, (min_mt, max_mt) in enumerate(microtime_ranges):
    print(f"  Range {i+1}: {min_mt} - {max_mt} channels")

# Select photons within the specified ranges
selected_indices = select_photons_by_microtime(data, microtime_ranges)
print(f"\nSelected {len(selected_indices)} photons ({len(selected_indices)/len(data)*100:.2f}% of total)")

# Create filtered TTTR object
filtered_data = data.select(selected_indices)
print(f"Filtered data contains {len(filtered_data)} events")

# Perform burst analysis on filtered data
burst_filter = tttrlib.BurstFilter(filtered_data.Get())
burst_filter.set_burst_parameters(20, 10, 1.0e-3)
burst_filter.set_background_parameters(10.0, 0.0)

# Perform burst search
bursts = burst_filter.find_bursts()
print(f"\nBurst search completed on filtered data")

print("\nMicrotime selection example completed successfully!")
