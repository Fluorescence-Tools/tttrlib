"""
Burst Analysis with BurstFilter Class and Photon Masking
========================================================

This example demonstrates burst analysis using the BurstFilter class with photon masking.
Photon masking allows you to pre-filter photons by channel, microtime, or other criteria
before performing burst analysis.

.. note:: This example requires a TTTR file. We use a sample file for demonstration.
"""
# %%
# Import required libraries
import tttrlib
import numpy as np
import matplotlib.pyplot as plt

# %%
# Load data
# ---------
# Find a sample file using TTTRLIB_DATA
import os
from pathlib import Path

data_root = os.environ.get('TTTRLIB_DATA')
if not data_root:
    print("TTTRLIB_DATA is not set. Please set it to the root of your tttr-data repository.")
    raise SystemExit(0)

sm_dna_folder = Path(data_root) / 'bh' / 'bh_spc132_sm_dna'
spc_files = sorted(sm_dna_folder.glob('*.spc'))
if not spc_files:
    print(f"No SPC files found in {sm_dna_folder}")
    raise SystemExit(0)

file_path = str(spc_files[0])
print(f"Loading file: {Path(file_path).name}")
data = tttrlib.TTTR(file_path, "SPC-130")
print(f"Total events: {len(data)}")

# %%
# Create photon mask for pre-filtering
# ------------------------------------
# Create a TTTRMask to filter photons by microtime ranges
# This allows selecting only photons within specific time windows
photon_mask = tttrlib.TTTRMask(data.Get())

# Select photons within specific microtime ranges (e.g., for channel selection)
# Example: select photons in microtime ranges 500-1500 and 2000-3000
microtime_ranges = [(500, 1500), (2000, 3000)]
photon_mask.select_microtime_ranges(data.Get(), microtime_ranges)

selected_indices = photon_mask.get_indices(True)  # Get selected (unmasked) photons
print(f"Photon mask created with {len(selected_indices)} selected photons")

# %%
# Create filtered TTTR data
# -------------------------
# Create a new TTTR object containing only the selected photons
filtered_data = data.select(selected_indices)
print(f"Filtered TTTR data contains {len(filtered_data)} events")

# %%
# Create BurstFilter instance with filtered data
# -----------------------------------------------
# Create a BurstFilter instance using the pre-filtered photons
burst_filter = tttrlib.BurstFilter(filtered_data.Get())

# %%
# Set burst search parameters
# ---------------------------
# Configure burst search parameters similar to FRETBursts
burst_filter.set_burst_parameters(
    min_photons=30,      # L: minimum photons per burst
    window_photons=10,   # m: window size (photons)
    window_time_max=0.5e-3  # T: window duration in seconds (0.5ms)
)

# %%
# Perform burst search on masked photons
# ------------------------------------
# Find bursts in the data using only the masked (selected) photons
bursts_np = burst_filter.find_bursts()
print(f"Burst search completed on masked data: {len(bursts_np)} bursts")

# %%
# Estimate background rates
# -------------------------
# Estimate background rates for filtering
background_rates = burst_filter.estimate_background()
print(f"Estimated background rates in {len(background_rates)} time periods")

# %%
# Get burst properties
# --------------------
# Extract properties of all bursts
print("\nBurst filtering and analysis:")
print("  Filtering by size (50-1000 photons)...")
filtered_bursts_np = burst_filter.filter_by_size(min_size=50, max_size=1000)

print("  Filtering by duration (0.1-5 ms)...")
filtered_bursts_np = burst_filter.filter_by_duration(min_duration=0.1e-3, max_duration=5e-3)

print("  Filtering by background ratio (max 5.0)...")
filtered_bursts_np = burst_filter.filter_by_background(max_background_ratio=5.0)

print("  Merging nearby bursts (gap < 5 photons)...")
merged_bursts_np = burst_filter.merge_bursts(max_gap=5)

# %%
# Enhanced: Dynamic Filter Parameter Changes
# ------------------------------------------
# Demonstrate the new dynamic filtering capabilities
print("\n" + "="*60)
print("ENHANCED: Dynamic Filter Parameter Changes")
print("="*60)

print(f"Current burst count after filtering: {len(burst_filter.get_bursts())}")

# %%
# Change filter parameters and see bursts "reappear"
# -------------------------------------------------
print("\nChanging filter parameters to demonstrate burst reappearance:")

# Relax the size filter - should bring back smaller bursts
print("  Relaxing size filter from min_size=50 to min_size=25...")
burst_filter.filter_by_size(min_size=25, max_size=1000)
print(f"  Burst count after size filter change: {len(burst_filter.get_bursts())}")

# Tighten the duration filter - should remove more bursts
print("  Tightening duration filter to 0.5-2 ms...")
burst_filter.filter_by_duration(min_duration=0.5e-3, max_duration=2e-3)
print(f"  Burst count after duration filter change: {len(burst_filter.get_bursts())}")

# %%
# Reapply entire filtering pipeline with different parameters
# ----------------------------------------------------------
print("\nReapplying entire filter pipeline with updated parameters:")

# Change background filter to be more permissive
print("  Making background filter more permissive (max ratio: 10.0 instead of 5.0)...")
burst_filter.filter_by_background(max_background_ratio=10.0)
print(f"  Burst count after background filter change: {len(burst_filter.get_bursts())}")

# Change merge parameters
print("  Increasing merge gap to 10 photons...")
burst_filter.merge_bursts(max_gap=10)
print(f"  Final burst count after merge change: {len(burst_filter.get_bursts())}")

# %%
# Demonstrate filter reset and reapplication
# ------------------------------------------
print("\nDemonstrating filter reset and reapplication:")

print(f"  Current burst count: {len(burst_filter.get_bursts())}")

# Reset to raw bursts (before any filtering)
print("  Resetting to raw (unfiltered) bursts...")
burst_filter.reset_to_raw_bursts()
print(f"  Burst count after reset: {len(burst_filter.get_bursts())}")

# Reapply all filters with current parameters
print("  Reapplying all filters with current parameters...")
burst_filter.reapply_filters()
print(f"  Burst count after reapplying filters: {len(burst_filter.get_bursts())}")

# Clear all filters completely
print("  Clearing all filters...")
burst_filter.clear_filters()
print(f"  Burst count after clearing all filters: {len(burst_filter.get_bursts())}")

# %%
# Get burst properties
# --------------------
# Extract properties of all bursts as a structured NumPy array
props = burst_filter.burst_properties

if len(props) > 0:
    burst_sizes = props['size']               # Integer photon counts per burst
    burst_durations = props['duration']       # Duration in seconds
    
    # %%
    # Plotting
    # --------
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))
    
    # Burst size distribution
    ax[0].hist(burst_sizes, bins=30, alpha=0.7, color='blue')
    ax[0].set_xlabel('Photons per burst')
    ax[0].set_ylabel('Count')
    ax[0].set_title('Burst size distribution')
    
    # Burst duration distribution
    ax[1].hist(burst_durations * 1e3, bins=30, alpha=0.7, color='red')  # Convert to milliseconds
    ax[1].set_xlabel('Burst duration (ms)')
    ax[1].set_ylabel('Count')
    ax[1].set_title('Burst duration distribution')
    
    plt.tight_layout()
    plt.show()

# %%
# Compare with unfiltered burst analysis
# ----------------------------------------
# For comparison, perform burst analysis on the full data without masking
burst_filter_full = tttrlib.BurstFilter(data)
burst_filter_full.set_burst_parameters(
    min_photons=30,
    window_photons=10,
    window_time_max=0.5e-3
)
bursts_full_np = burst_filter_full.find_bursts()
print(f"\nComparison:")
print(f"  Burst search completed on full data: {len(bursts_full_np)} bursts")
print(f"  Photon reduction: {(1 - len(filtered_data)/len(data))*100:.1f}%")

# %%
# Extract burst photons
# ---------------------
# Create a TTTR object containing only photons from selected bursts
if len(merged_bursts_np) > 0:
    burst_photons = burst_filter.get_burst_photons(merged_bursts_np)
    print(f"Extracted {len(burst_photons)} photons from {len(merged_bursts_np)} bursts")
else:
    print("No bursts available for photon extraction after filtering/merging.")
