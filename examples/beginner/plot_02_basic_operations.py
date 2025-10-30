"""
Basic Example 2: Basic operations on TTTR objects (focused illustration)
------------------------------------------------------------------------
This example uses only a small slice (first ~300 events) so that the effect of
joining/appending is visible in the plots.

Make sure the demo PTU file is available (see ``test/test_settings.py``).
Set the environment variable ``TTTRLIB_DATA`` so that the file
``pq/ptu/pq_ptu_hh_t3.ptu`` can be found.

What this example covers:
1) Inspect a few header fields
2) Slice, join, and append a small subset
3) Visualize macro times **before** and **after** appending on a tiny window
4) Plot a micro-time histogram using the built-in helper
"""

# sphinx_gallery_thumbnail_number = 1

import numpy as np
import matplotlib.pyplot as plt
import tttrlib
from examples._example_data import get_data_path

# ------------------------------------------------------------
# Load the demo TTTR file
# ------------------------------------------------------------
ptu_path = get_data_path("pq/ptu/pq_ptu_hh_t3.ptu")
tttr = tttrlib.TTTR(str(ptu_path), "PTU")

n = len(tttr)
print("Number of events:", n)
if n == 0:
    raise SystemExit("The selected TTTR file contains no events.")

# ------------------------------------------------------------
# Header peek (keep it short)
# ------------------------------------------------------------
header = tttr.header
print("Header info:")
print("  Macro-time resolution (s):", header.macro_time_resolution)
print("  Micro-time resolution (s):", header.micro_time_resolution)

# ------------------------------------------------------------
# Work on a tiny subset so differences are visible
# ------------------------------------------------------------
base = tttr[:300]          # small "main" segment
first_100 = base[:100]     # small piece we will append/join

print("Base length:", len(base))
print("First_100 length:", len(first_100))

# Convenience arrays
macro_base = np.asarray(base.macro_times, dtype=np.int64)

# 1) Join with "+" (library handles details internally)
joined_plus = base + first_100
macro_joined_plus = np.asarray(joined_plus.macro_times, dtype=np.int64)
print("Joined with '+':", len(joined_plus))

# 2) Append with explicit control over macro-time shifting
#    a) SHIFTED (continuous timeline)
joined_shifted = tttrlib.TTTR(base)  # copy
joined_shifted.append(first_100, shift_macro_time=True)
macro_joined_shifted = np.asarray(joined_shifted.macro_times, dtype=np.int64)
print("Appended (shift=True):", len(joined_shifted))

#    b) UN-SHIFTED (shows a clear "jump/reset" at the join)
joined_unshifted = tttrlib.TTTR(base)  # copy
joined_unshifted.append(first_100, shift_macro_time=False)
macro_joined_unshifted = np.asarray(joined_unshifted.macro_times, dtype=np.int64)
print("Appended (shift=False):", len(joined_unshifted))

# ------------------------------------------------------------
# Plot A: Macro times on small sequences so effects are obvious
# ------------------------------------------------------------
# Entire small sequences for direct comparison
figA, axA = plt.subplots(figsize=(4, 2.8))
axA.plot(np.arange(len(macro_base)), macro_base, label="base (300)")
axA.plot(np.arange(len(macro_joined_shifted)), macro_joined_shifted, label="appended (shift=True)")
axA.plot(np.arange(len(macro_joined_unshifted)), macro_joined_unshifted, label="appended (shift=False)")
axA.set_xlabel("Event index")
axA.set_ylabel("Macro time (periods)")
axA.set_title("Macro times on small slices (visible differences)")
axA.legend()
figA.tight_layout()

# ------------------------------------------------------------
# Plot B: Zoom around the join index (base end vs appended tail)
# ------------------------------------------------------------
# The join happens at index len(base). We visualize a narrow window around it.
join_idx = len(base)
window = 40  # show +/- 40 events

# Build x/y for each series within the window if available
def window_slice(x, start, end):
    start = max(start, 0)
    end = min(end, len(x))
    return np.arange(start, end), x[start:end]

x_b, y_b = window_slice(macro_base, join_idx - window, join_idx)
x_s, y_s = window_slice(macro_joined_shifted, join_idx - window, join_idx + window)
x_u, y_u = window_slice(macro_joined_unshifted, join_idx - window, join_idx + window)

figB, axB = plt.subplots(figsize=(4, 2.8))
axB.plot(x_b, y_b, label="base (end)")
axB.plot(x_s, y_s, label="appended shift=True (around join)")
axB.plot(x_u, y_u, label="appended shift=False (around join)")
axB.axvline(join_idx, ls="--", lw=1.0)
axB.set_xlabel("Event index")
axB.set_ylabel("Macro time (periods)")
axB.set_title("Zoom near join index (shift vs no shift)")
axB.legend()
figB.tight_layout()

# ------------------------------------------------------------
# Plot C: Library micro-time histogram (small data still fine)
# ------------------------------------------------------------
counts, times_s = base.get_microtime_histogram(64)  # on the small "base" slice
times_ns = times_s * 1e9

figC, axC = plt.subplots(figsize=(4, 2.6))
axC.step(times_ns, counts, where="mid")
axC.set_xlabel("Micro time (ns)")
axC.set_ylabel("Counts")
axC.set_title("Micro-time distribution (on base slice)")
figC.tight_layout()

# Notes:
# - Using tiny slices makes the effect of appending immediately visible.
# - shift=True continues the macro-time timeline; shift=False shows a discontinuity.
# - Example 1 covers routing/per-channel plots; we avoid repeating them here.
