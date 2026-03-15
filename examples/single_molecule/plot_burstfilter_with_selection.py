"""
BurstFilter with TTTRSelection Example
====================================

This example demonstrates how to use TTTRSelection with BurstFilter for advanced
photon selection and burst analysis.

While BurstFilter currently only accepts TTTR objects directly, this example
shows how to achieve the same functionality by creating a new TTTR object
from selected photons using TTTRSelection.
"""
# %%
# Import required libraries
import tttrlib
import numpy as np

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
# Create TTTRSelection with specific criteria
# ------------------------------------------
# Create a selection object
selection = tttrlib.TTTRSelection()

# Set selection criteria (e.g., a specific time range)
selection.set_range(1000, 50000)  # Select photons from index 1000 to 50000
selection.set_dense(True)  # Use dense representation

print(f"Selection range: {selection.get_start()} - {selection.get_stop()}")
print(f"Selection is dense: {selection.is_dense()}")

# %%
# Export and import selection as JSON
# -----------------------------------
# Export selection to JSON string
selection_json = selection.to_json()
print(f"\nSelection as JSON: {selection_json}")

# Create a new selection from JSON
new_selection = tttrlib.TTTRSelection()
new_selection.from_json(selection_json)

print(f"New selection range: {new_selection.get_start()} - {new_selection.get_stop()}")
print(f"New selection is dense: {new_selection.is_dense()}")

# %%
# Apply selection to create filtered TTTR object
# ---------------------------------------------
# Get the indices from the selection
indices = selection.get_tttr_indices()
print(f"\nSelected {len(indices)} photon indices")

# Create a new TTTR object with only the selected photons
filtered_data = data.select(indices)
print(f"Filtered TTTR object contains {len(filtered_data)} events")

# %%
# Use BurstFilter with the filtered data
# --------------------------------------
# Create BurstFilter with the filtered data
burst_filter = tttrlib.BurstFilter(filtered_data.Get())

# Configure burst search parameters
burst_filter.set_burst_parameters(
    min_photons=10,      # L: minimum photons per burst
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
# Analyze burst properties
# ------------------------
if len(bursts) > 0:
    # Use structured NumPy array with named fields
    props = burst_filter.burst_properties
    sizes = props['size']
    durations = props['duration']
    
    print(f"\nBurst statistics:")
    print(f"  Size: min={sizes.min():.0f}, max={sizes.max():.0f}, mean={sizes.mean():.1f}")
    print(f"  Duration: min={durations.min()*1e3:.3f}ms, max={durations.max()*1e3:.1f}ms, mean={durations.mean()*1e3:.2f}ms")

print("\nBurstFilter with TTTRSelection example completed successfully!")
