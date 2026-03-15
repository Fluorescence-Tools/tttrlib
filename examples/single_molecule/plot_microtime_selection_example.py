"""
Microtime Range Selection Example
===============================

This example demonstrates how to implement microtime range selection for burst analysis.
While the BurstFilter class doesn't currently have built-in microtime selection,
this example shows how to achieve the same functionality by pre-filtering the TTTR data.

The approach uses the TTTR class's ability to select photons based on their microtime values
and then performs burst analysis on the filtered data.
"""
# %%
# Import required libraries
import tttrlib
import numpy as np
import matplotlib.pyplot as plt

# %%
# Load data
# ---------
# For this example, we'll use a sample file
# Replace with your actual file path
from examples._example_data import get_data_path
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
# For example, select photons within specific time windows
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
print(f"\nFound {len(bursts)} bursts in filtered data")

# %%
# Compare with full data analysis
# -------------------------------
# For comparison, let's also analyze the full data
burst_filter_full = tttrlib.BurstFilter(data.Get())
burst_filter_full.set_burst_parameters(20, 10, 1.0e-3)
burst_filter_full.set_background_parameters(10.0, 0.0)
bursts_full = burst_filter_full.find_bursts()
print(f"Found {len(bursts_full)} bursts in full data")

# %%
# Visualize results
# -----------------
# Compare burst properties between filtered and full data
if len(bursts) > 0 and len(bursts_full) > 0:
    # Get burst properties for filtered data
    burst_properties_filtered = burst_filter.get_all_burst_properties()
    properties_array_filtered = np.array(burst_properties_filtered)
    burst_sizes_filtered = properties_array_filtered[:, 2]
    burst_durations_filtered = properties_array_filtered[:, 3]
    
    # Get burst properties for full data
    burst_properties_full = burst_filter_full.get_all_burst_properties()
    properties_array_full = np.array(burst_properties_full)
    burst_sizes_full = properties_array_full[:, 2]
    burst_durations_full = properties_array_full[:, 3]
    
    # Plot comparison
    fig, ax = plt.subplots(2, 2, figsize=(12, 10))
    fig.suptitle('Burst Analysis Comparison: Full vs Filtered Data', fontsize=16)
    
    # Burst size distribution comparison
    ax[0, 0].hist(burst_sizes_full, bins=30, alpha=0.7, label='Full data', color='blue')
    ax[0, 0].hist(burst_sizes_filtered, bins=30, alpha=0.7, label='Filtered data', color='red')
    ax[0, 0].set_xlabel('Photons per burst')
    ax[0, 0].set_ylabel('Count')
    ax[0, 0].set_title('Burst size distribution')
    ax[0, 0].legend()
    ax[0, 0].grid(True, alpha=0.3)
    
    # Burst duration distribution comparison
    ax[0, 1].hist(burst_durations_full * 1e3, bins=30, alpha=0.7, label='Full data', color='blue')
    ax[0, 1].hist(burst_durations_filtered * 1e3, bins=30, alpha=0.7, label='Filtered data', color='red')
    ax[0, 1].set_xlabel('Burst duration (ms)')
    ax[0, 1].set_ylabel('Count')
    ax[0, 1].set_title('Burst duration distribution')
    ax[0, 1].legend()
    ax[0, 1].grid(True, alpha=0.3)
    
    # Scatter plot: size vs duration for filtered data
    ax[1, 0].scatter(burst_durations_filtered * 1e3, burst_sizes_filtered, alpha=0.6, s=20, color='red')
    ax[1, 0].set_xlabel('Burst duration (ms)')
    ax[1, 0].set_ylabel('Photons per burst')
    ax[1, 0].set_title('Filtered data: Burst size vs duration')
    ax[1, 0].grid(True, alpha=0.3)
    
    # Scatter plot: size vs duration for full data
    ax[1, 1].scatter(burst_durations_full * 1e3, burst_sizes_full, alpha=0.6, s=20, color='blue')
    ax[1, 1].set_xlabel('Burst duration (ms)')
    ax[1, 1].set_ylabel('Photons per burst')
    ax[1, 1].set_title('Full data: Burst size vs duration')
    ax[1, 1].grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.show()
    
    # Print statistics
    print("\n=== Burst Analysis Statistics ===")
    print(f"Full data: {len(bursts_full)} bursts, avg size: {burst_sizes_full.mean():.1f} photons")
    print(f"Filtered data: {len(bursts)} bursts, avg size: {burst_sizes_filtered.mean():.1f} photons")
    print(f"Size reduction: {len(bursts_full) - len(bursts)} bursts ({(1 - len(bursts)/len(bursts_full))*100:.1f}%)")

print("\nMicrotime selection example completed successfully!")
