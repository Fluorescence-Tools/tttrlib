"""
FRET Burst Analysis with BurstFilter Class
=========================================

This example demonstrates FRET burst analysis using the new BurstFilter class in tttrlib.
The BurstFilter class provides a cleaner interface for burst analysis similar to FRETBursts,
with support for multi-channel FRET analysis.

.. note:: This example requires a TTTR file with multiple channels (donor and acceptor).
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
    print("TTTRLIB_DATA is not set. Please set it to the root of your tttr-data repository.")
    raise SystemExit(0)

sm_folder = Path(data_root) / 'bh' / 'bh_spc132_sm_dna'
spc_files = sorted(sm_folder.glob('*.spc'))
if not spc_files:
    print(f"No SPC files found in {sm_folder}")
    raise SystemExit(0)

file_path = str(spc_files[0])
fmt = 'SPC-130'
print(f"Loading file: {Path(file_path).name} ({fmt})")
data = tttrlib.TTTR(file_path, fmt)
print(f"Total events: {len(data)}")
print(f"Used routing channels: {data.get_used_routing_channels()}")

# %%
# Channel selection
# -----------------
# Define donor and acceptor channels (adjust according to your data)
donor_ch = [0]  # Donor channel
acceptor_ch = [1]  # Acceptor channel

# Get selections for donor and acceptor photons
idx_d = data.get_selection_by_channel(donor_ch)
idx_a = data.get_selection_by_channel(acceptor_ch)

tttr_d = data[idx_d]
tttr_a = data[idx_a]

print(f"Donor photons: {len(tttr_d)}")
print(f"Acceptor photons: {len(tttr_a)}")

# %%
# Optional PIE/ALEX micro-time gating
# -----------------------------------
# Example prompt windows in micro-time channels (adjust to instrument)
donor_prompt = (0, 1600)    # [min, max] for donor excitation window
acceptor_prompt = (2000, 3600)  # [min, max] for acceptor excitation window

mt = data.micro_times
rc = data.routing_channels

# Masks for donor/acceptor detection during their respective excitation prompts
mask_d_prompt = (mt >= donor_prompt[0]) & (mt < donor_prompt[1]) & np.isin(rc, donor_ch)
mask_a_prompt = (mt >= acceptor_prompt[0]) & (mt < acceptor_prompt[1]) & np.isin(rc, acceptor_ch)

# Indices for gated donor/acceptor photons
idx_dg = np.nonzero(mask_d_prompt)[0].astype(np.int64)
idx_ag = np.nonzero(mask_a_prompt)[0].astype(np.int64)

# Create gated TTTR objects
tttr_dg = data[idx_dg]
tttr_ag = data[idx_ag]

print(f"Gated donor photons: {len(tttr_dg)}")
print(f"Gated acceptor photons: {len(tttr_ag)}")

# %%
# Create BurstFilter instance
# ---------------------------
# Create a BurstFilter instance for analyzing the TTTR data
# Using all photons for burst detection
burst_filter = tttrlib.BurstFilter(data.Get())

# %%
# Set burst search parameters
# ---------------------------
# Configure burst search parameters
burst_filter.set_burst_parameters(
    min_photons=20,      # L: minimum photons per burst
    window_photons=10,   # m: window size (photons)
    window_time_max=0.5e-3  # T: window duration in seconds (0.5ms)
)

# %%
# Perform burst search
# --------------------
# Find bursts in the data using NumPy-friendly API to avoid SWIG STL proxies
bursts_np = burst_filter.find_bursts()  # ndarray of shape (n,2) int64
print(f"Found {len(bursts_np)} bursts")

# %%
# Compute FRET efficiency for each burst
# -------------------------------------
# Calculate FRET efficiency for each detected burst
E_values = []
burst_sizes = []

for start, stop in bursts_np:
    start = int(start)
    stop = int(stop)
    # Extract photons in this burst
    burst_photons = data[start:stop+1]
    
    # Count donor and acceptor photons
    n_d = np.count_nonzero(np.isin(burst_photons.routing_channels, donor_ch))
    n_a = np.count_nonzero(np.isin(burst_photons.routing_channels, acceptor_ch))
    total = n_d + n_a
    
    # Calculate FRET efficiency
    if total > 0:
        E = n_a / total
        E_values.append(E)
        burst_sizes.append(total)
    else:
        E_values.append(0.0)
        burst_sizes.append(0)

print(f"Calculated FRET efficiency for {len(E_values)} bursts")

# %%
# Filter bursts based on FRET efficiency
# -------------------------------------
# Filter bursts to select only those with reasonable FRET efficiency
valid_e_indices = [i for i, e in enumerate(E_values) if 0.0 <= e <= 1.0 and burst_sizes[i] > 0]
# Create a Python list of (start, stop) tuples from the NumPy array
bursts_list = [(int(s), int(e)) for (s, e) in bursts_np]
filtered_bursts = [bursts_list[i] for i in valid_e_indices]
filtered_E_values = [E_values[i] for i in valid_e_indices]
filtered_burst_sizes = [burst_sizes[i] for i in valid_e_indices]

print(f"Filtered to {len(filtered_bursts)} bursts with valid FRET efficiency")

# %%
# Plotting
# --------
fig, ax = plt.subplots(2, 2, figsize=(12, 10))

# Burst size distribution
ax[0, 0].hist(filtered_burst_sizes, bins=30, alpha=0.7, color='blue')
ax[0, 0].set_xlabel('Photons per burst')
ax[0, 0].set_ylabel('Count')
ax[0, 0].set_title('Burst size distribution')

# FRET efficiency histogram
ax[0, 1].hist(filtered_E_values, bins=30, range=(0, 1), alpha=0.7, color='green')
ax[0, 1].set_xlabel('FRET Efficiency')
ax[0, 1].set_ylabel('Count')
ax[0, 1].set_title('FRET efficiency distribution')

# Burst size vs FRET efficiency
scatter = ax[1, 0].scatter(filtered_burst_sizes, filtered_E_values, alpha=0.6, c=filtered_burst_sizes, cmap='viridis')
ax[1, 0].set_xlabel('Burst size (photons)')
ax[1, 0].set_ylabel('FRET Efficiency')
ax[1, 0].set_title('Burst size vs FRET efficiency')
plt.colorbar(scatter, ax=ax[1, 0], label='Burst size')

# FRET efficiency vs burst index
ax[1, 1].plot(filtered_E_values, 'o-', markersize=3, alpha=0.7)
ax[1, 1].set_xlabel('Burst index')
ax[1, 1].set_ylabel('FRET Efficiency')
ax[1, 1].set_title('FRET efficiency vs burst index')
ax[1, 1].set_ylim(0, 1)

plt.tight_layout()
plt.show()

# %%
# Summary statistics
# ------------------
if len(filtered_E_values) > 0:
    mean_E = np.mean(filtered_E_values)
    std_E = np.std(filtered_E_values)
    mean_size = np.mean(filtered_burst_sizes)
    std_size = np.std(filtered_burst_sizes)
    
    print(f"\nSummary statistics:")
    print(f"Mean FRET efficiency: {mean_E:.3f} ± {std_E:.3f}")
    print(f"Mean burst size: {mean_size:.1f} ± {std_size:.1f} photons")
    print(f"Number of valid bursts: {len(filtered_E_values)}")

# %%
# Extract burst photons for further analysis
# -----------------------------------------
# Create a TTTR object containing only photons from selected bursts
if len(filtered_bursts) > 0:
    # Select first 10 bursts for demonstration
    selected_bursts = filtered_bursts[:min(10, len(filtered_bursts))]
    import numpy as np
    burst_photons = burst_filter.get_burst_photons(np.asarray(selected_bursts, dtype=np.int64).reshape(-1,2))
    print(f"\nExtracted {len(burst_photons)} photons from {len(selected_bursts)} bursts")
