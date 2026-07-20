"""
Basic operations with TTTR files
================================

This example shows how to perform basic operations with TTTR files.

Introduction to Time-Tagged Time-Resolved (TTTR) Data with tttrlib
===================================================================

TTTR data is a format used in single-molecule fluorescence, fluorescence lifetime imaging (FLIM), and time-correlated single-photon counting (TCSPC) experiments. TTTR files record streams of photon detection events, each with precise timing and detector channel information.

Each event typically contains:
  1. Macro time (experiment time or sync pulse number)
  2. Micro time (photon delay after excitation pulse)
  3. Channel number (detector ID)
  4. Event type (photon, marker, etc.)

This rich, raw data enables advanced analyses such as burst detection, fluorescence lifetime calculation, FRET efficiency, and more.

Common Use Cases:
  - Single-molecule FRET (smFRET) burst analysis
  - Fluorescence Correlation Spectroscopy (FCS)
  - Fluorescence Lifetime Imaging (FLIM)
  - Photon counting histograms
  - Custom photon stream filtering and selection

Installation & Setup:
  You can install tttrlib using pip:
      pip install tttrlib

  Or, for the latest development version, clone the repository and install:
      git clone https://github.com/fluorescence-tools/tttrlib.git
      cd tttrlib
      pip install .

  You will need Python (>=3.7), NumPy, and optionally Jupyter for running notebooks.

The operations demonstrated in this script include:
  1. Reading a TTTR file
  2. Slicing TTTR data
  3. Joining TTTR objects
  4. Selecting events by channel
  5. Selecting events by count rate

Note: Replace the file paths with your own data.
"""

import tttrlib
import numpy as np

from examples._example_data import get_data_path

# Read a TTTR file
filename = get_data_path("bh/bh_spc132.spc")

# Use type inference (recommended):
data = tttrlib.TTTR(str(filename))

print("Number of events:", len(data))
print("First 5 macro times:", data.macro_times[0:5])
print("First 5 micro times:", data.micro_times[0:5])
print("First 5 routing channels:", data.routing_channels[0:5])

########################
# Slicing TTTR data
########################

# Select the first 100 events
first_100 = data[0:100]
print("First 100 events, macro times:", first_100.macro_times)

########################
# Joining TTTR objects
########################

# Read a second file (or use the same for demonstration)
# data2 = tttrlib.TTTR(filename)
# For the example, we'll just use a slice of the original data to avoid needing two files.
data2 = data[100:200]

# Join two TTTR objects
joined = data + data2
print("Joined event count:", len(joined))

########################
# Selecting by channel
########################

# Select events from channel 0
indices_ch0 = data.get_selection_by_channel([0])
data_ch0 = data[indices_ch0]
print("Number of events in channel 0:", len(data_ch0))

########################
# Selecting by count rate
########################

# Select events where the local count rate is below a threshold
# We use a window of 1000 macro time units and a maximum of 10 photons per window
indices_low_cr = data.get_time_window_ranges(1000, 10)
data_low_cr = data[indices_low_cr]
print("Number of events with low count rate:", len(data_low_cr))
