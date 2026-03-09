"""
Microtime Range Selection Example (Fixed Version)
===============================================

This example demonstrates how to implement microtime range selection for burst analysis.
While the BurstFilter class doesn't currently have built-in microtime selection,
this example shows how to achieve the same functionality by pre-filtering the TTTR data.
"""
# %%
# Import required libraries
import tttrlib
import numpy as np
import matplotlib.pyplot as plt

# %%
from examples._example_data import get_data_path

# Load data
data = tttrlib.TTTR(str(get_data_path("bh/bh_spc132.spc")), "SPC-130")

print(f"Total events: {len(data)}")

# %%
# Analyze microtime distribution
# ------------------------------
# First, let's look at the microtime distribution to understand the data
micro_times = data.get_micro_times()

# Plot histogram of microtimes
fig, ax = plt.subplots(1, 1, figsize=(10, 6))
n, bins, patches = ax.hist(micro_times, bins=256, alpha=0.7, color='blue', edgecolor='black', linewidth=0.5)
ax.set_xlabel('Microtime channels')
ax.set_ylabel('Count')
ax.set_title('Microtime distribution')
ax.grid(True, alpha=0.3)
plt.show()

print(f"Microtime range: {micro_times.min()} - {micro_times.max()}")
print(f"Most common microtime: {np.argmax(n)} (count: {n.max()})")

# %%
# Define microtime ranges for selection
# ------------------------------------
# Define ranges of interest (min, max) in microtime channels
microtime_ranges = [
    (0, 1000),      # Early photons
    (2000, 3000),   # Middle photons  
    (3500, 4095)    # Late photons
]

print("\nSelected microtime ranges:")
for i, (min_mt, max_mt) in enumerate(microtime_ranges):
    print(f"  Range {i+1}: {min_mt} - {max_mt} channels")

# %%
# Select photons within microtime ranges
# -------------------------------------
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

# Select photons within the specified ranges
selected_indices = select_photons_by_microtime(data, microtime_ranges)
print(f"\nSelected {len(selected_indices)} photons ({len(selected_indices)/len(data)*100:.2f}% of total)")

# %%
# Create filtered TTTR object
# ---------------------------
# Create a new TTTR object containing only the selected photons
filtered_data = data.select(selected_indices)
print(f"Filtered data contains {len(filtered_data)} events")

# %%
# Perform burst analysis on filtered data
# ---------------------------------------
# Create BurstFilter with filtered data
burst_filter = tttrlib.BurstFilter(filtered_data.Get())

# Configure burst search parameters
burst_filter.set_burst_parameters(
    min_photons=20,      # L: minimum photons per burst
    window_photons=10,   # m: window size (photons)
    window_time_max=1.0e-3  # T: window duration in seconds (1.0ms)
)

# Set background estimation parameters
burst_filter.set_background_parameters(
    time_window=10.0,    # Background time window in seconds
    threshold=0.0        # Background threshold
)

# Perform burst search
bursts = burst_filter.find_bursts()
print(f"\nBurst search completed on filtered data")

print("\nMicrotime selection example completed successfully!")
