"""
Basic Example 3: Selections — count-rate filter, bursts, and intensity trace
----------------------------------------------------------------------------
This example shows:

1) Using tttrlib's built-in count-rate filter to get LOW/HIGH count-rate selections
2) Micro-time histograms at native tick resolution on a log y-axis
3) Sliding-window burst search and comparison of burst vs all micro-time histograms
4) Channel-resolved (green/red) 1 ms intensity traces for the FIRST 3 SECONDS,
   with the area under the curves FILLED where bursts are present.

Before you start:
- Ensure demo data exists (see ``test/test_settings.py``).
- Set ``TTTRLIB_DATA`` to the directory containing the reference files.
"""

# sphinx_gallery_thumbnail_number = 3

import numpy as np
import matplotlib.pyplot as plt
import tttrlib
from examples._example_data import get_data_path

# ------------------------------------------------------------
# 1) Load the Photon-HDF5 demo file
# ------------------------------------------------------------
hdf_path = get_data_path("hdf/1a_1b_Mix.hdf5")
tttr = tttrlib.TTTR(str(hdf_path), "PHOTON-HDF5")
print("Total events:", len(tttr))

# Arrays and timing info
macro = np.asarray(tttr.macro_times, dtype=np.int64)
micro = np.asarray(tttr.micro_times, dtype=np.int32)
mt_res = tttr.header.macro_time_resolution      # seconds per macro tick
mic_res = tttr.header.micro_time_resolution     # seconds per micro tick
t = macro * mt_res                              # arrival times (s)

# ------------------------------------------------------------
# 2) Choose LOW/HIGH thresholds from a quick 1 ms intensity trace
#    Then use the tttrlib **count-rate filter** (Python API) to get selections.
# ------------------------------------------------------------
bin_width_s = 1e-3  # 1 ms
t_min, t_max = float(t.min()), float(t.max())
n_bins = int(np.ceil((t_max - t_min) / bin_width_s)) + 1
edges = t_min + np.arange(n_bins + 1, dtype=float) * bin_width_s  # (n_bins+1,) edges

counts_per_bin, _ = np.histogram(t, bins=edges)
low_thr  = int(np.percentile(counts_per_bin, 25.0))
high_thr = int(np.percentile(counts_per_bin, 75.0))
print(f"Counts per 1 ms: 25%={low_thr}, 75%={high_thr}")

# Count-rate selections (return full TTTR objects directly)
tttr_low  = tttr.get_tttr_by_count_rate(1.0e-3, low_thr,  invert=False)  # ≤ low_thr
tttr_high = tttr.get_tttr_by_count_rate(1.0e-3, high_thr, invert=True)   # > high_thr

print("LOW-CR photons:", len(tttr_low))
print("HIGH-CR photons:", len(tttr_high))

# ------------------------------------------------------------
# 3) Micro-time histograms at native tick resolution (log-y)
# ------------------------------------------------------------
tick_max = int(micro.max())
ticks = np.arange(tick_max + 1, dtype=np.int32)
times_ns = ticks * (mic_res * 1e9)  # micro-time axis in ns

counts_all  = np.bincount(micro, minlength=tick_max + 1)
counts_low  = (np.bincount(np.asarray(tttr_low.micro_times, dtype=np.int32),  minlength=tick_max + 1)
               if len(tttr_low)  else np.zeros(tick_max + 1, dtype=np.int64))
counts_high = (np.bincount(np.asarray(tttr_high.micro_times, dtype=np.int32), minlength=tick_max + 1)
               if len(tttr_high) else np.zeros(tick_max + 1, dtype=np.int64))

fig1, ax1 = plt.subplots(figsize=(5.6, 3.2))
ax1.step(times_ns, counts_low,  where="mid", label="LOW count-rate")
ax1.step(times_ns, counts_high, where="mid", label="HIGH count-rate")
ax1.step(times_ns, counts_all,  where="mid", label="ALL photons")
ax1.set_yscale("log")
ax1.set_xlabel("Micro time (ns)")
ax1.set_ylabel("Counts (log scale)")
ax1.set_title("Micro-time histograms (native ticks) via count-rate filter")
ax1.legend()
fig1.tight_layout()

# ------------------------------------------------------------
# 4) Burst selection (sliding-window) and burst vs all histograms
#    Also build a PER-BIN BURST MASK by converting burst start/stop INDICES to bin indices.
# ------------------------------------------------------------
L = 30        # minimum photons per burst
m = 5         # photons in the sliding window
T = 0.40e-3   # maximum duration (s) of the m-photon window

burst_ranges = tttr.burst_search(L=L, m=m, T=T)  # returns [s0, e0, s1, e1, ...] (indices)
n_bursts = len(burst_ranges) // 2
print("Bursts detected:", n_bursts)

in_burst_mask_full = np.zeros(n_bins, dtype=bool)  # one flag per 1 ms bin over the WHOLE trace
if n_bursts:
    ranges = np.asarray(burst_ranges, dtype=np.int64).reshape(-1, 2)

    # Build mask of photons inside any burst (for micro-time hist of bursts)
    mask_burst = np.zeros(len(tttr), dtype=bool)
    for start, stop in ranges:
        mask_burst[start:stop + 1] = True

        # --- Convert burst [start, stop] INDICES -> BIN INDICES via searchsorted on EDGES ---
        # Use photon times t[start], t[stop] to locate covered bins (no float comparisons on centers).
        b0 = np.searchsorted(edges, t[start], side="left")
        b1 = np.searchsorted(edges, t[stop],  side="right") - 1
        b0 = max(0, min(b0, n_bins - 1))
        b1 = max(0, min(b1, n_bins - 1))
        if b1 >= b0:
            in_burst_mask_full[b0:b1 + 1] = True

    burst_idx = np.nonzero(mask_burst)[0].astype(np.int64)
    counts_burst = np.bincount(micro[burst_idx], minlength=tick_max + 1)

    first_start, first_stop = ranges[0]
    print(f"First burst: [{first_start}, {first_stop}] "
          f"({first_stop - first_start + 1} photons)")
    print(f"In-burst photons: {len(burst_idx)} of {len(tttr)} "
          f"({len(burst_idx)/len(tttr):.1%})")

    fig2, ax2 = plt.subplots(figsize=(5.6, 3.2))
    ax2.step(times_ns, counts_burst, where="mid", label="BURST photons")
    ax2.step(times_ns, counts_all,   where="mid", label="ALL photons")
    ax2.set_yscale("log")
    ax2.set_xlabel("Micro time (ns)")
    ax2.set_ylabel("Counts (log scale)")
    ax2.set_title("Micro-time histogram: bursts vs all (native ticks)")
    ax2.legend()
    fig2.tight_layout()

# ------------------------------------------------------------
# 5) Channel-resolved (green/red) intensity traces (first 3 s)
#    Fill the area under the curves WHERE the per-bin burst mask is True.
# ------------------------------------------------------------
used_channels = np.unique(np.asarray(tttr.routing_channels, dtype=np.int32))
print(f"Used routing channels in this file: {used_channels.tolist()}")

# Simple default detector assignment
if used_channels.size >= 2:
    detectors = {"green": {"chs": [int(used_channels[0])]},
                 "red":   {"chs": [int(used_channels[1])]} }
elif used_channels.size == 1:
    detectors = {"green": {"chs": [int(used_channels[0])]},
                 "red":   {"chs": []}}
else:
    detectors = {"green": {"chs": []}, "red": {"chs": []}}

green_ch = detectors["green"]["chs"]
red_ch   = detectors["red"]["chs"]
print(f"Green channels: {green_ch}")
print(f"Red channels: {red_ch}")

# Select photons by routing channel
green_indices = np.asarray(tttr.get_selection_by_channel(green_ch), dtype=np.int64) if len(green_ch) else np.array([], dtype=np.int64)
red_indices   = np.asarray(tttr.get_selection_by_channel(red_ch),   dtype=np.int64) if len(red_ch)   else np.array([], dtype=np.int64)

print(f"Green photons: {len(green_indices):,}")
print(f"Red photons: {len(red_indices):,}")

# Create channel-specific TTTR objects
tttr_green = tttr[green_indices] if green_indices.size else tttr[:0]
tttr_red   = tttr[red_indices]   if red_indices.size   else tttr[:0]

# Intensity traces (1 ms bins) from library
bin_width_ms = 1.0
bin_width_s  = bin_width_ms / 1000.0
trace_green = tttr_green.get_intensity_trace(bin_width_s)
trace_red   = tttr_red.get_intensity_trace(bin_width_s)

# Time axis in ms (0, 1, 2, ...) matching 1 ms bins
trace_len = len(trace_green) if len(trace_green) else len(trace_red)
time_axis_ms_full = np.arange(trace_len, dtype=float) * bin_width_ms

# We want to show FIRST 3 SECONDS → first 3000 bins
show_bins = min(500, time_axis_ms_full.size)
time_axis_ms = time_axis_ms_full[:show_bins]
trace_green = trace_green[:show_bins]
trace_red   = trace_red[:show_bins]

# Align the burst mask with the first 3 s as well:
# Our per-bin burst mask was built over ALL bins using the SAME 1 ms edges.
# The first 'show_bins' entries correspond to the first 3 s.
in_burst_mask = in_burst_mask_full[:show_bins] if in_burst_mask_full.size else np.zeros(show_bins, dtype=bool)

print(f"Displaying first {show_bins} ms ({show_bins/1000:.1f} s)")

# Plot: green (top), red (middle, inverted), combined (bottom)
fig3, axes = plt.subplots(3, 1, figsize=(12, 7), sharex=True)

# Green trace + filled area under curve where bursts are present
axes[0].plot(time_axis_ms, trace_green, "g-", linewidth=0.6, label="Green")
if in_burst_mask.any():
    axes[0].fill_between(time_axis_ms, 0, trace_green, where=in_burst_mask, alpha=0.25, label="Burst region")
axes[0].set_ylabel("Intensity (counts/ms)", fontsize=10)
axes[0].set_title(f"Green/Red Intensity Traces — {hdf_path.name} (1 ms bins, first 3 s)", fontsize=12)
axes[0].legend(loc="upper right")
axes[0].grid(True, alpha=0.3)

# Red trace (inverted y) + filled area where bursts are present
axes[1].plot(time_axis_ms, trace_red, "r-", linewidth=0.6, label="Red")
if in_burst_mask.any():
    axes[1].fill_between(time_axis_ms, 0, trace_red, where=in_burst_mask, alpha=0.25, label="Burst region")
axes[1].invert_yaxis()
axes[1].set_ylabel("Intensity (counts/ms)", fontsize=10)
axes[1].legend(loc="upper right")
axes[1].grid(True, alpha=0.3)

# Combined trace + filled area for both (shade under the higher of the two)
axes[2].plot(time_axis_ms, trace_green, "g-", linewidth=0.6, alpha=0.8, label="Green")
axes[2].plot(time_axis_ms, trace_red,   "r-", linewidth=0.6, alpha=0.8, label="Red")
if in_burst_mask.any():
    combined_top = np.maximum(trace_green, trace_red)
    axes[2].fill_between(time_axis_ms, 0, combined_top, where=in_burst_mask, alpha=0.20, label="Burst region")
axes[2].set_xlabel("Time (ms)", fontsize=10)
axes[2].set_ylabel("Intensity (counts/ms)", fontsize=10)
axes[2].legend(loc="upper right")
axes[2].grid(True, alpha=0.3)

plt.tight_layout()
plt.show()

# ------------------------------------------------------------
# Recap
# ------------------------------------------------------------
# - LOW/HIGH count-rate selections were obtained with tttrlib's count-rate filter.
# - Micro-time histograms were plotted at native tick resolution (log-y).
# - Sliding-window burst search returned start/stop INDICES; these were mapped to
#   1 ms BIN INDICES via np.searchsorted(edges, ...), producing an accurate per-bin
#   burst mask for shading the intensity traces.
# - The first 3 seconds of green/red intensity traces are shown with filled areas
#   under the curves where bursts are present.
