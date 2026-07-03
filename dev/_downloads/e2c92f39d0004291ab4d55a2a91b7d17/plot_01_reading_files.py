"""
Basic Example 1: Reading TTTR data
----------------------------------
This example mirrors the unit-test data resolution logic and expects the
demo PTU files to be available. Set ``TTTRLIB_DATA`` to the directory containing
the reference data (see ``test/test_settings.py``) before building the docs. It
shows how to open a TTTR object, inspect its contents, and visualize a few
basic properties.

Notes
-----
- This script is Sphinx-Gallery friendly: it does not call ``plt.show()`` and
  will render figures with a non-interactive backend during doc builds.
- If the demo file cannot be located, a clear error is raised with a helpful hint.
"""

# sphinx_gallery_thumbnail_number = 1

from __future__ import annotations

import numpy as np
import matplotlib.pyplot as plt
import tttrlib
from examples._example_data import get_data_path

# %%
# Open a TTTR data set ---------------------------------------------------------
# The helper below returns the path to the demo PTU file. In real experiments
# you would read a file instead, e.g. ``tttrlib.TTTR('measurement.ptu')``.
try:
    data_path = get_data_path("pq/ptu/pq_ptu_hh_t3.ptu")
except FileNotFoundError as e:
    raise SystemExit(
        "Demo data not found. Set the environment variable TTTRLIB_DATA to the "
        "directory with the reference data or adjust test/settings.json."
    ) from e

tttr = tttrlib.TTTR(str(data_path), "PTU")

# %%
# Inspect the TTTR data --------------------------------------------------------
# Macro times give the coarse arrival time (laser repetition periods).
# Micro times resolve the fine delay within the current laser period.
# Routing channels record which detector/input registered the event.

num_events = len(tttr)
macro = np.asarray(tttr.macro_times, dtype=np.int64)   # shape: (N,)
micro = np.asarray(tttr.micro_times, dtype=np.int32)   # shape: (N,)
routing = np.asarray(tttr.routing_channels, dtype=np.int32)  # shape: (N,)

print(f"Number of events: {num_events}")
print(f"First 10 macro times: {macro[:10]}")
print(f"First 10 channels: {routing[:10]}")

if num_events == 0:
    raise SystemExit("The selected TTTR file contains no events.")

# %%
# Routing-channel histogram ----------------------------------------------------
# Ensure the example produces plot output so Sphinx-Gallery keeps it.
ch_min = int(routing.min())
ch_max = int(routing.max())
# Centered integer bins: [..., k-0.5, k+0.5, ...]
bin_edges = np.arange(ch_min - 0.5, ch_max + 1.5, 1.0)

fig, ax = plt.subplots(figsize=(4, 2.6))
ax.hist(routing, bins=bin_edges)
ax.set_xlabel("Routing channel")
ax.set_ylabel("Counts")
ax.set_title("TTTR routing channels")
ax.set_xticks(np.arange(ch_min, ch_max + 1))
fig.tight_layout()

# %%
# First 100 micro-times (illustrative) -----------------------------------------
# The macro time typically increases ~linearly with the event index. For readability,
# plot a short slice of the micro-time sequence.
sample_size = min(100, micro.size)
fig_sample, ax_sample = plt.subplots(figsize=(4, 2.6))
ax_sample.plot(np.arange(sample_size), micro[:sample_size], marker="o", linestyle="-")
ax_sample.set_xlabel("Event index")
ax_sample.set_ylabel("Micro time (clock ticks)")
ax_sample.set_title(f"First {sample_size} micro times")
fig_sample.tight_layout()

# %%
# Micro-time distributions per routing channel --------------------------------
# This reveals fluorescence decay characteristics recorded by each detector channel.
unique_channels = np.unique(routing)
mt_min = int(micro.min())
mt_max = int(micro.max())
# Use fixed-width bins over the observed micro-time span (inclusive on the right)
bins = np.linspace(mt_min, mt_max, 51) if mt_max > mt_min else np.array([mt_min - 0.5, mt_max + 0.5])

fig2, axes = plt.subplots(
    nrows=unique_channels.size,
    ncols=1,
    figsize=(4, 2.0 + 1.4 * max(unique_channels.size - 1, 0)),
    sharex=True,
)
axes = np.atleast_1d(axes)

for ax_i, ch in zip(axes, unique_channels):
    mask = (routing == ch)
    ax_i.hist(micro[mask], bins=bins, alpha=0.85)
    ax_i.set_ylabel(f"ch {ch}")

axes[-1].set_xlabel("Micro time (clock ticks)")
axes[0].set_title("Micro-time distributions per channel")
fig2.tight_layout()
