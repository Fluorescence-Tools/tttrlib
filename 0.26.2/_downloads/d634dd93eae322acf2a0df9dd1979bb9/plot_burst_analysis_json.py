"""
Burst Analysis with JSON Parameter Support
==========================================

This example demonstrates burst analysis using the BurstFilter class with JSON
parameter serialization support. Parameters can be saved to and loaded from JSON files.

.. note:: This example requires a TTTR file. We use a sample file for demonstration.
"""
# %%
# Import required libraries
import tttrlib
import numpy as np
import matplotlib.pyplot as plt
import json

# %%
# Load data
# ---------
import os
from pathlib import Path

# Read the files from the tttrlib-data repository
data_root = os.environ.get('TTTRLIB_DATA')
sm_folder = Path(data_root) / 'bh' / 'bh_spc132_sm_dna'
spc_files = sorted(sm_folder.glob('*.spc'))
file_path = str(spc_files[0])
data = tttrlib.TTTR(file_path, "SPC-130")

print(f"Loaded TTTR: {Path(file_path).name}")
print(f"Total events: {len(data)}")

# %%
# Create BurstFilter instance
# ---------------------------
bf = tttrlib.BurstFilter(data)

# %%
# Set and save burst parameters to JSON
# ------------------------------------
# Configure burst search parameters
bf.set_burst_parameters(
    min_photons=30,      # L: minimum photons per burst
    window_photons=2,   # m: window size (photons)
    window_time_max=0.5e-3  # T: window duration in seconds (0.5ms)
)

# Set background estimation parameters
bf.set_background_parameters(
    time_window=10.0,    # Background time window in seconds
    threshold=0.0        # Background threshold
)

# Save parameters to JSON file
bf.save_parameters("burst_parameters.json")
print("Burst parameters saved to burst_parameters.json")

# %%
# Load parameters from JSON
# -------------------------
# Create a new BurstFilter instance
bf2 = tttrlib.BurstFilter(data.Get())

# Load parameters from JSON file
bf2.load_parameters("burst_parameters.json")
print("Burst parameters loaded from burst_parameters.json")

# %%
# Verify parameters
# -----------------
# Convert to dict and print for verification
params_dict = bf.to_dict()
print("\nSaved parameters:")
print(json.dumps(params_dict, indent=2))

# %%
# Perform burst search with loaded parameters
# ------------------------------------------
bursts_np = bf2.find_bursts()
print(f"\nFound {len(bursts_np)} bursts using loaded parameters")

# %%
# Estimate background rates
# -------------------------
background_rates = bf2.estimate_background()
print(f"Estimated background rates in {len(background_rates)} time periods")

# %%
# Filter bursts
# -------------
# Filter by size
size_filtered_np = bf2.filter_by_size(min_size=20, max_size=1000)
print(f"Filtered to {len(size_filtered_np)} bursts by size (20-1000 photons)")

# Filter by duration
duration_filtered_np = bf2.filter_by_duration(min_duration=0.1e-3, max_duration=5e-3)
print(f"Filtered to {len(duration_filtered_np)} bursts by duration (0.1-5ms)")

# Filter by background ratio
bg_filtered_np = bf2.filter_by_background(max_background_ratio=5.0)
print(f"Filtered to {len(bg_filtered_np)} bursts by background ratio (≤5.0)")

# %%
# Merge bursts
# ------------
merged_bursts_np = bf2.merge_bursts(max_gap=5)
print(f"Merged to {len(merged_bursts_np)} bursts")

# %%
# Get burst properties
# --------------------
# Legacy: raw list of lists (from C++), converted to matrix below for convenience
burst_properties = bf2.get_all_burst_properties()

# Preferred: structured NumPy array with named fields
props_struct = bf2.burst_properties

if len(burst_properties) > 0:
    # Legacy matrix usage (backward compatible)
    properties_array = np.array(burst_properties, dtype=float).reshape((-1, 5))
    burst_sizes = properties_array[:, 2]      # Size (number of photons)
    burst_durations = properties_array[:, 3]  # Duration in seconds

    # Structured array usage (recommended)
    sizes_named = props_struct['size']        # int counts
    durations_named = props_struct['duration']  # float seconds

    # %%
    # Plotting
    # --------
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))
    
    # Burst size distribution (legacy matrix)
    ax[0].hist(burst_sizes, bins=30, alpha=0.7, color='blue', label='matrix')
    ax[0].hist(sizes_named, bins=30, alpha=0.4, color='green', label='structured')
    ax[0].set_xlabel('Photons per burst')
    ax[0].set_ylabel('Count')
    ax[0].set_title('Burst size distribution')
    ax[0].legend()
    
    # Burst duration distribution (structured array)
    ax[1].hist(durations_named * 1e3, bins=30, alpha=0.7, color='red')  # Convert to milliseconds
    ax[1].set_xlabel('Burst duration (ms)')
    ax[1].set_ylabel('Count')
    ax[1].set_title('Burst duration distribution')
    
    plt.tight_layout()
    plt.show()
else:
    print("No bursts detected with current parameters.")

# %%
# Export burst data to JSON
# -------------------------
# Create a dictionary with burst analysis results
results = {
    "total_events": len(data),
    "bursts_found": len(bursts_np),
    "background_periods": len(background_rates),
    "filtered_bursts": {
        "by_size": len(size_filtered_np),
        "by_duration": len(duration_filtered_np),
        "by_background": len(bg_filtered_np),
        "merged": len(merged_bursts_np)
    }
}

# Save results to JSON file
with open("burst_analysis_results.json", "w") as f:
    json.dump(results, f, indent=2)

print("\nBurst analysis results saved to burst_analysis_results.json")
print(json.dumps(results, indent=2))
