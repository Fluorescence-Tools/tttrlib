"""
Complete Burst Analysis Pipeline
===============================

This example demonstrates a complete end-to-end burst analysis pipeline using the 
BurstFilter class with JSON parameter serialization support. It covers:

- Loading single-molecule TTTR data (multiple SPC files)
- Configuring burst analysis parameters via Python dictionaries
- Performing burst search with parameter validation
- Applying multiple burst filters
- Merging bursts with short gaps
- Computing burst properties and statistics
- Exporting results to JSON files
- Visualizing burst analysis results

The pipeline uses the bh_spc132_sm_dna dataset which contains single-molecule 
fluorescence data from DNA samples labeled with fluorescent dyes.

.. note:: This example requires the tttr-data repository with the bh_spc132_sm_dna dataset.
"""
# %%
# Import required libraries
import tttrlib
import numpy as np
import matplotlib.pyplot as plt
import json
import os
from pathlib import Path

def get_data_paths():
    """Get paths to all SPC files in the single-molecule DNA data folder.

    Looks under the TTTRLIB_DATA environment variable for the folder
    bh/bh_spc132_sm_dna and returns a sorted list of .spc files.
    """
    data_root = os.environ.get('TTTRLIB_DATA')
    if not data_root:
        print("TTTRLIB_DATA is not set. Please set it to the root of your tttr-data repository.")
        raise SystemExit(0)

    sm_dna_folder = os.path.join(data_root, "bh", "bh_spc132_sm_dna")
    spc_files = sorted(Path(sm_dna_folder).glob("*.spc"))
    if not spc_files:
        print(f"No SPC files found in {sm_dna_folder}")
        raise SystemExit(0)
    return [str(f) for f in spc_files]

# %%
# Load data
# ---------
data_files = get_data_paths()
print(f"Found {len(data_files)} SPC files:")
for i, file in enumerate(data_files):
    print(f"  {i+1:2d}. {Path(file).name}")

# Load all files and concatenate them
print("\nLoading and concatenating data...")
all_data = []

for file in data_files:
    print(f"Loading {Path(file).name}...")
    data = tttrlib.TTTR(file, "SPC-130")
    all_data.append(data)
    print(f"  Events: {len(data):,}")

# Concatenate all data
if len(all_data) > 1:
    print("\nConcatenating all data files...")
    combined_data = all_data[0]
    for data in all_data[1:]:
        combined_data = combined_data + data
else:
    combined_data = all_data[0]

print(f"\nTotal events: {len(combined_data):,}")
print(f"Acquisition time: {combined_data.acquisition_time:.2f} seconds")

# %%
# Create BurstFilter instance
# ---------------------------
burst_filter = tttrlib.BurstFilter(combined_data.Get())

# %%
# Configure burst analysis parameters using Python dictionaries
# -------------------------------------------------------------
# Define burst analysis parameters as a Python dictionary
burst_config = {
    "burst_parameters": {
        "min_photons_per_burst": 20,      # L: minimum photons per burst
        "window_photons": 10,             # m: window size (photons)
        "window_time": 1.0e-3             # T: window duration in seconds (1.0ms)
    },
    "background_parameters": {
        "background_time_window": 10.0,   # Background time window in seconds
        "background_threshold": 0.0       # Background threshold
    }
}

# Convert dictionary to JSON string and apply parameters
config_json = json.dumps(burst_config)
burst_filter.from_json_string(config_json)

# Verify parameters were applied correctly
print("\nApplied burst analysis parameters:")
print(json.dumps(burst_config, indent=2))

# %%
# Save parameters to JSON file using string methods
# -------------------------------------------------
# Convert parameters to JSON string
config_json = burst_filter.to_json_string()
print(f"Burst parameters as JSON string: {config_json}")

# Save the configuration to a JSON file for reproducibility
config_file = "burst_analysis_config.json"
with open(config_file, "w") as f:
    f.write(config_json)
print(f"\nBurst parameters saved to {config_file}")

# %%
# Load parameters from JSON file using string methods
# ---------------------------------------------------
# Create a new BurstFilter instance and load parameters
burst_filter2 = tttrlib.BurstFilter(combined_data.Get())

# Load parameters from JSON file
with open(config_file, "r") as f:
    config_json = f.read()
burst_filter2.from_json_string(config_json)
print("Burst parameters loaded from JSON string")

# %%
# Perform burst search
# ---------------------
print("\nPerforming burst search...")
bursts = burst_filter2.find_bursts()
print(f"Found {len(bursts)} bursts")

# %%
# Estimate background rates
# -------------------------
background_rates = burst_filter2.estimate_background()
print(f"Estimated background rates in {len(background_rates)} time periods")
if len(background_rates) > 0:
    print(f"Average background rate: {np.mean(background_rates):.2f} photons/second")

# %%
# Apply burst filters
# -------------------
print("\nApplying burst filters...")

# Filter by size (remove very small and very large bursts)
size_filtered = burst_filter2.filter_by_size(min_size=15, max_size=1000)
print(f"Filtered to {len(size_filtered)} bursts by size (15-1000 photons)")

# Filter by duration (remove very short and very long bursts)
duration_filtered = burst_filter2.filter_by_duration(min_duration=0.05e-3, max_duration=10e-3)
print(f"Filtered to {len(duration_filtered)} bursts by duration (0.05-10ms)")

# Filter by background ratio (remove bursts with excessive background)
bg_filtered = burst_filter2.filter_by_background(max_background_ratio=10.0)
print(f"Filtered to {len(bg_filtered)} bursts by background ratio (≤10.0)")

# Use the most restrictive filter (size filter in this case)
filtered_bursts = size_filtered

# %%
# Merge bursts with short gaps
# ----------------------------
print("\nMerging bursts with short gaps...")
merged_bursts = burst_filter2.merge_bursts(max_gap=10)
print(f"Merged to {len(merged_bursts)} bursts")

# %%
# Get burst properties
# --------------------
print("\nComputing burst properties...")
burst_properties = burst_filter2.get_all_burst_properties()

if len(burst_properties) > 0:
    # Convert to numpy array for easier handling
    properties_array = np.array(burst_properties)
    burst_start_indices = properties_array[:, 0]   # Start indices
    burst_stop_indices = properties_array[:, 1]    # Stop indices
    burst_sizes = properties_array[:, 2]           # Size (number of photons)
    burst_durations = properties_array[:, 3]       # Duration in seconds
    
    print(f"Burst statistics:")
    print(f"  Size: min={burst_sizes.min():.0f}, max={burst_sizes.max():.0f}, mean={burst_sizes.mean():.1f}")
    print(f"  Duration: min={burst_durations.min()*1e3:.3f}ms, max={burst_durations.max()*1e3:.1f}ms, mean={burst_durations.mean()*1e3:.2f}ms")
    
    # %%
    # Extract burst photons
    # ---------------------
    print("\nExtracting photons from bursts...")
    burst_photons = burst_filter2.get_burst_photons(merged_bursts)
    print(f"Extracted {len(burst_photons)} photons from {len(merged_bursts)} bursts")
    
    # %%
    # Create burst analysis results dictionary
    # ----------------------------------------
    results = {
        "data_files": [Path(f).name for f in data_files],
        "total_events": len(combined_data),
        "acquisition_time_seconds": combined_data.acquisition_time,
        "parameters": burst_config,
        "bursts_found": len(bursts),
        "background_periods": len(background_rates),
        "background_rates": background_rates,
        "filtered_bursts": {
            "by_size": len(size_filtered),
            "by_duration": len(duration_filtered),
            "by_background": len(bg_filtered),
            "merged": len(merged_bursts)
        },
        "burst_statistics": {
            "count": len(burst_properties),
            "size": {
                "min": int(burst_sizes.min()),
                "max": int(burst_sizes.max()),
                "mean": float(burst_sizes.mean()),
                "median": float(np.median(burst_sizes))
            },
            "duration_ms": {
                "min": float(burst_durations.min() * 1e3),
                "max": float(burst_durations.max() * 1e3),
                "mean": float(burst_durations.mean() * 1e3),
                "median": float(np.median(burst_durations) * 1e3)
            }
        }
    }
    
    # %%
    # Save results to JSON file
    # -------------------------
    results_file = "burst_analysis_results.json"
    with open(results_file, "w") as f:
        json.dump(results, f, indent=2)
    
    print(f"\nBurst analysis results saved to {results_file}")
    
    # %%
    # Visualization
    # -------------
    fig, ax = plt.subplots(2, 2, figsize=(12, 10))
    fig.suptitle('Single-Molecule DNA Burst Analysis Results', fontsize=16)
    
    # Burst size distribution
    ax[0, 0].hist(burst_sizes, bins=50, alpha=0.7, color='blue', edgecolor='black', linewidth=0.5)
    ax[0, 0].set_xlabel('Photons per burst')
    ax[0, 0].set_ylabel('Count')
    ax[0, 0].set_title(f'Burst size distribution (N={len(burst_sizes)})')
    ax[0, 0].grid(True, alpha=0.3)
    
    # Burst duration distribution
    ax[0, 1].hist(burst_durations * 1e3, bins=50, alpha=0.7, color='red', edgecolor='black', linewidth=0.5)
    ax[0, 1].set_xlabel('Burst duration (ms)')
    ax[0, 1].set_ylabel('Count')
    ax[0, 1].set_title(f'Burst duration distribution')
    ax[0, 1].grid(True, alpha=0.3)
    
    # Burst size vs duration
    ax[1, 0].scatter(burst_durations * 1e3, burst_sizes, alpha=0.6, s=20)
    ax[1, 0].set_xlabel('Burst duration (ms)')
    ax[1, 0].set_ylabel('Photons per burst')
    ax[1, 0].set_title('Burst size vs duration')
    ax[1, 0].grid(True, alpha=0.3)
    
    # Background rates
    if len(background_rates) > 0:
        time_periods = np.arange(len(background_rates)) * burst_config["background_parameters"]["background_time_window"]
        ax[1, 1].plot(time_periods, background_rates, 'o-', markersize=4)
        ax[1, 1].set_xlabel('Time (s)')
        ax[1, 1].set_ylabel('Background rate (photons/s)')
        ax[1, 1].set_title(f'Background rates (N={len(background_rates)} periods)')
        ax[1, 1].grid(True, alpha=0.3)
    else:
        ax[1, 1].text(0.5, 0.5, 'No background data', ha='center', va='center', transform=ax[1, 1].transAxes)
        ax[1, 1].set_title('Background rates')
    
    plt.tight_layout()
    plt.show()
    
    # %%
    # Summary statistics
    # ------------------
    print("\n=== Burst Analysis Summary ===")
    print(f"Data files: {len(data_files)} SPC files")
    print(f"Total photons: {len(combined_data):,}")
    print(f"Acquisition time: {combined_data.get_acquisition_time():.1f} seconds")
    print(f"Initial bursts detected: {len(bursts)}")
    print(f"Final bursts after filtering: {len(merged_bursts)}")
    print(f"Burst rate: {len(merged_bursts)/combined_data.get_acquisition_time():.2f} bursts/second")
    print(f"Average burst size: {burst_sizes.mean():.1f} photons")
    print(f"Average burst duration: {burst_durations.mean()*1e3:.2f} ms")

else:
    print("No bursts found with the current parameters.")
    
    # Create empty results
    results = {
        "data_files": [Path(f).name for f in data_files],
        "total_events": len(combined_data),
        "acquisition_time_seconds": combined_data.get_acquisition_time(),
        "parameters": burst_config,
        "bursts_found": 0,
        "background_periods": len(background_rates),
        "filtered_bursts": {
            "by_size": 0,
            "by_duration": 0,
            "by_background": 0,
            "merged": 0
        },
        "burst_statistics": {
            "count": 0
        }
    }

print("\nBurst analysis pipeline completed successfully!")
