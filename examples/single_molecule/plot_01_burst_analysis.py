"""
Burst Analysis Example with BurstFilter
========================================

This example demonstrates burst analysis using the BurstFilter class in tttrlib. It covers:
- Loading TTTR data
- Selecting donor and acceptor channels
- Optional PIE/ALEX micro-time gating
- Performing burst search using BurstFilter
- Filtering bursts by size, duration, and background
- Merging short gaps between bursts
- Computing FRET efficiency
- Visualizing burst size and FRET distributions

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
# Use TTTRLIB_DATA\\bh\\bh_spc132_sm_dna\\*.spc explicitly (no complicated lookup).
import os
from pathlib import Path

data_root = os.environ.get('TTTRLIB_DATA')
if not data_root:
    print("TTTRLIB_DATA is not set. Please set it to your tttr-data root.")
    raise SystemExit(0)

sm_folder = Path(data_root) / 'bh' / 'bh_spc132_sm_dna'
spc_files = sorted(sm_folder.glob('*.spc'))
if not spc_files:
    print(f"No SPC files found in {sm_folder}")
    raise SystemExit(0)

test_file = str(spc_files[0])
fmt = 'SPC-130'
print(f"Loading file: {Path(test_file).name}")

data = tttrlib.TTTR(test_file, fmt)
print(f"Total events: {len(data)}")

# %%
# Define donor and acceptor channels
# -----------------------------------
# Define channels with routing channels and microtime ranges
# Adjust these values based on your instrument configuration
channels_dict = [
    {
        'name': 'donor',
        'components': [
            {'routing_channel': 0, 'microtime_range': (0, 4096)}  # Donor excitation window
        ]
    },
    {
        'name': 'acceptor',
        'components': [
            {'routing_channel': 1, 'microtime_range': (0, 4096)}  # Acceptor excitation window
        ]
    }
]

# Channel system is now the primary approach for channel selection

# %%
# Burst search using BurstFilter with channels
# -----------------------------------------------
# Create BurstFilter instance
burst_filter = tttrlib.BurstFilter(data.Get())

# Load channel definitions
burst_filter.load_channels_from_json_dict(channels_dict)
print(f"Loaded {burst_filter.get_channel_count()} channels: {burst_filter.get_channel_names()}")

# Set burst search parameters via JSON (JSON-first path)
# L: Minimum photons per burst (reduced from 30 to 15)
# m: Window size (photons)
# T: Window duration in seconds (increased from 0.5ms to 1ms)
import json
L = 15      # Minimum photons per burst (reduced from 30)
m = 10      # Window size (photons) - kept the same
T = 1e-3    # Window duration in seconds (increased from 0.5ms)
config = {
    "burst_parameters": {
        "min_photons_per_burst": int(L),
        "window_photons": int(m),
        "window_time": float(T)
    },
    "background_parameters": {
        "background_time_window": 10.0,
        "background_threshold": 0.0
    }
}
config_json = json.dumps(config)
print(f"Burst search parameters (JSON): {config_json}")
# Apply parameters using JSON string API
burst_filter.from_json_string(config_json)

# Run burst search using NumPy-friendly API to avoid SWIG STL proxies
bursts_np = burst_filter.find_bursts()  # ndarray (n,2) int64
print(f"Found {len(bursts_np)} bursts")

# %%
# Filter and merge bursts
# -----------------------
# Print burst statistics before filtering
print("\nBurst statistics before filtering:")
num_bursts = len(burst_filter)
print(f"  Number of bursts: {num_bursts}")

# Get burst sizes via JSON path (using BurstFeatureExtractor JSON)
if num_bursts > 0:
    bfe = tttrlib.BurstFeatureExtractor(burst_filter)
    _bfe_obj = json.loads(bfe.to_json_string())
    _props = _bfe_obj.get("burst_properties", [])
    if _props:
        _props_arr = np.asarray(_props, dtype=float).reshape((-1, 5))
        sizes = _props_arr[:, 2]
        print(f"  Min size: {np.min(sizes):.1f} photons")
        print(f"  Max size: {np.max(sizes):.1f} photons")
        print(f"  Mean size: {np.mean(sizes):.1f} photons")

# Filter bursts by size (20-5000 photons)
print("\nFiltering bursts...")
filtered_bursts_np = burst_filter.filter_by_size(min_size=20, max_size=5000)
print(f"Bursts after size filtering: {len(filtered_bursts_np)}")

# Merge bursts with short gaps (5 photons)
print("Merging bursts with short gaps...")
merged_bursts_np = burst_filter.merge_bursts(max_gap=5)
print(f"Merged to {len(merged_bursts_np)} bursts after merging")

# Print final burst statistics (via JSON)
num_bursts = len(burst_filter)
if num_bursts > 0:
    bfe_final = tttrlib.BurstFeatureExtractor(burst_filter)
    _bfe_final = json.loads(bfe_final.to_json_string())
    _props_final = _bfe_final.get("burst_properties", [])
    if _props_final:
        _arr = np.asarray(_props_final, dtype=float).reshape((-1, 5))
        sizes = _arr[:, 2]
        print("\nFinal burst statistics:")
        print(f"  Number of bursts: {num_bursts}")
        print(f"  Min size: {np.min(sizes):.1f} photons")
        print(f"  Max size: {np.max(sizes):.1f} photons")
        print(f"  Mean size: {np.mean(sizes):.1f} photons")

# %%
# Compute FRET efficiency using JSON data only
# --------------------------------------------
E_values = []
burst_sizes = []
donor_counts = []
acceptor_counts = []
donor_count_rates = []
acceptor_count_rates = []

# Use BurstFeatureExtractor JSON for all per-burst values (JSON-first path)
bfe_all = tttrlib.BurstFeatureExtractor(burst_filter)
_bfe_all = json.loads(bfe_all.to_json_string())
_props = _bfe_all.get("burst_properties", [])
_ch_ph = _bfe_all.get("channel_photons", {})  # dict: name -> list per burst

if _props:
    arr = np.asarray(_props, dtype=float).reshape((-1, 5))
    sizes_all = arr[:, 2]
    rates_all = arr[:, 4]
    # Extract donor/acceptor per-burst counts from JSON dict (defaults to zeros)
    donor_list = _ch_ph.get("donor", [0] * len(sizes_all))
    acceptor_list = _ch_ph.get("acceptor", [0] * len(sizes_all))
    donor_arr = np.asarray(donor_list, dtype=np.int64)
    acceptor_arr = np.asarray(acceptor_list, dtype=np.int64)

    for i in range(len(sizes_all)):
        n_d = int(donor_arr[i])
        n_a = int(acceptor_arr[i])
        total = n_d + n_a
        count_rate = float(rates_all[i])
        if total > 0:
            donor_rate = (n_d / total) * count_rate
            acceptor_rate = (n_a / total) * count_rate
            E = acceptor_rate / (donor_rate + acceptor_rate) if (donor_rate + acceptor_rate) > 0 else 0.0
            E_values.append(E)
            burst_sizes.append(int(sizes_all[i]))
            donor_counts.append(n_d)
            acceptor_counts.append(n_a)
            donor_count_rates.append(donor_rate)
            acceptor_count_rates.append(acceptor_rate)

# %%
# Plotting
# --------
if len(burst_sizes) > 0:
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))

    # Burst size distribution
    ax[0].hist(burst_sizes, bins=30, alpha=0.7, color='blue')
    ax[0].set_xlabel('Photons per burst')
    ax[0].set_ylabel('Count')
    ax[0].set_title('Burst size distribution')

    # FRET efficiency histogram
    ax[1].hist(E_values, bins=30, range=(0, 1), alpha=0.7, color='red')
    ax[1].set_xlabel('FRET Efficiency')
    ax[1].set_ylabel('Count')
    ax[1].set_title('FRET efficiency distribution')

    plt.tight_layout()
    plt.show()
    
    print(f"\nBurst Analysis Summary:")
    print(f"  Total bursts: {len(burst_sizes)}")
    if len(burst_sizes) > 0:
        print(f"  Mean burst size: {np.mean(burst_sizes):.1f} photons")
        print(f"  Mean donor photons: {np.mean(donor_counts):.1f}")
        print(f"  Mean acceptor photons: {np.mean(acceptor_counts):.1f}")
        print(f"  Mean donor count rate: {np.mean(donor_count_rates):.0f} photons/s")
        print(f"  Mean acceptor count rate: {np.mean(acceptor_count_rates):.0f} photons/s")
        print(f"  Mean FRET efficiency (from count rates): {np.mean(E_values):.3f}")
        print(f"  FRET efficiency range: {np.min(E_values):.3f} - {np.max(E_values):.3f}")
    else:
        print("  No bursts found with the current settings.")
        print("  Try adjusting the burst search parameters or check your channel definitions.")
else:
    print("No bursts found after filtering")
