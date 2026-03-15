"""
=======================================
Confocor3 two channel cross-correlation (using merge method)
=======================================

The raw FCS data format of the Zeiss Confocor3 is relatively simple.
Zeiss Confocor3 raw files store time-difference between photons.
A relatively small header is followed by a set of 32-bit integers that
contain the time difference to the previous registered photon. Photons
registered by different channels are stored in separate files.

This example demonstrates how to use the new TTTR merge method to combine
multiple Confocor3 channels into a single photon stream for cross-correlation
analysis. The merge method automatically handles time sorting and is more
efficient than manual concatenation.

"""
import pathlib
import numpy as np

import pylab as plt
import tttrlib

#%% Reading data
# ------------
# The photon data registered by different detectors are saved in separate files.
from examples._example_data import get_data_root
# Read the data of all channels that should be correlated into separate containers.
fns = sorted([str(p) for p in (get_data_root() / 'cz/fcs').glob('5a6ce6a348a08e3da9f7c0ab4ee0ce94_R1_P1_K1_Ch*.raw')])

tttr_data = [tttrlib.TTTR(fn, 'CZ-RAW') for fn in fns]

print(f"Found {len(tttr_data)} channels")
for i, tttr in enumerate(tttr_data):
    print(f"  Channel {i+1}: {tttr.get_number_of_records()} events")

#%% Check count rates
# ----------------
# You can check the count rates of the channels using the macro time resolution 
# contained in the header
header = tttr_data[0].header
macro_time_resolution = header.macro_time_resolution
count_rates = [len(t) / (t.macro_times[-1] * macro_time_resolution) for t in tttr_data]
print("Count rates:", count_rates)

#%% Merge channels using TTTR merge method
# ---------------------------------------
# Use the new merge method to combine channels automatically
# The merge method with strategy=1 (interleave) automatically sorts by time
tttr_merged = tttrlib.TTTR()
tttr_merged.set_header(header)

# Copy first channel
tttr_merged.append_events(
    macro_times=tttr_data[0].macro_times,
    micro_times=tttr_data[0].micro_times,
    routing_channels=tttr_data[0].routing_channels,
    event_types=tttr_data[0].event_types
)

# Merge remaining channels with interleave strategy (chronological order)
for i in range(1, len(tttr_data)):
    # Use channel offset to ensure unique channel numbers
    # Channel numbers from Confocor3 files might be the same, so we offset them
    channel_offset = i if tttr_data[i].routing_channels[0] == tttr_data[0].routing_channels[0] else 0
    tttr_merged.merge(tttr_data[i], 0, channel_offset, 1)  # strategy=1 for interleave

print(f"Merged data: {tttr_merged.get_number_of_records()} total events")
merged_channels = sorted(set(tttr_merged.routing_channels))
print(f"Merged channels: {merged_channels}")

#%% Verify chronological order
# -------------------------
# The interleave merge strategy ensures events are sorted by time
times = tttr_merged.macro_times
if np.all(np.diff(times) >= 0):
    print("✓ Events are in chronological order")
else:
    print("✗ Events are NOT in chronological order")

#%% Cross-correlation analysis
# ---------------------------
# The merged container can be used for standard analysis, e.g., correlations.
settings = {
    "n_bins": 9,  # n_bins and n_casc defines the settings of the multi-tau
    "n_casc": 19,  # correlation algorithm
}

# Create correlator
# Caution: x-axis in units of macro time counter
# tttrlib.Correlator is unaware of the calibration in the TTTR object
correlator = tttrlib.Correlator(
    channels=([merged_channels[0]], [merged_channels[1]]),  # Use merged channel numbers
    tttr=tttr_merged,
    **settings
)
plt.semilogx(
    correlator.x_axis,
    correlator.correlation,
    label=f"Corr({merged_channels[0]},{merged_channels[1]})"
)

# Auto-correlation for first channel
correlator = tttrlib.Correlator(
    channels=([merged_channels[0]], [merged_channels[0]]),
    tttr=tttr_merged,
    **settings
)
plt.semilogx(
    correlator.x_axis,
    correlator.correlation,
    label=f"Corr({merged_channels[0]},{merged_channels[0]})"
)

# Auto-correlation for second channel
correlator = tttrlib.Correlator(
    channels=([merged_channels[1]], [merged_channels[1]]),
    tttr=tttr_merged,
    **settings
)
plt.semilogx(
    correlator.x_axis,
    correlator.correlation,
    label=f"Corr({merged_channels[1]},{merged_channels[1]})"
)

plt.xlabel("Lag Time (macro time units)")
plt.ylabel("Correlation")
plt.legend()
plt.ylim(0.98, 1.30)
plt.title("Confocor3 Cross-correlation (using TTTR merge method)")
plt.show()

#%% Performance comparison (optional)
# --------------------------------
# Compare the new merge method with the old manual approach
import time

# Old method (manual concatenation and sorting)
start_time = time.time()
macro_times_old_list = []
routing_channels_old_list = []
for i, t in enumerate(tttr_data):
    macro_times_old_list.append(t.macro_times)
    # Match the channel offset logic of the new method
    channel_offset = i if t.routing_channels[0] == tttr_data[0].routing_channels[0] else 0
    routing_channels_old_list.append(t.routing_channels + channel_offset)

macro_times_old = np.concatenate(macro_times_old_list)
routing_channels_old = np.concatenate(routing_channels_old_list)
sorted_indices = np.argsort(macro_times_old, kind='stable')
routing_channels_old = routing_channels_old[sorted_indices]
macro_times_old = macro_times_old[sorted_indices]
old_time = time.time() - start_time


# New method (TTTR merge)
start_time = time.time()
tttr_new = tttrlib.TTTR()
tttr_new.set_header(header)
tttr_new.append_events(
    macro_times=tttr_data[0].macro_times,
    micro_times=tttr_data[0].micro_times,
    routing_channels=tttr_data[0].routing_channels,
    event_types=tttr_data[0].event_types
)
for i in range(1, len(tttr_data)):
    channel_offset = i if tttr_data[i].routing_channels[0] == tttr_data[0].routing_channels[0] else 0
    tttr_new.merge(tttr_data[i], 0, channel_offset, 1)
new_time = time.time() - start_time

print(f"\nPerformance comparison:")
print(f"Old method: {old_time:.6f} seconds")
print(f"New method: {new_time:.6f} seconds")
print(f"Speedup: {old_time/new_time:.2f}x" if new_time > 0 else "N/A")

# Verify results are identical
assert np.array_equal(macro_times_old, tttr_new.macro_times), "Macro times should be identical"
assert np.array_equal(routing_channels_old, tttr_new.routing_channels), "Routing channels should be identical"
print("✓ Both methods produce identical results")
