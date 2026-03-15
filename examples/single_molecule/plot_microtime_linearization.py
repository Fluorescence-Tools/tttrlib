"""
Microtime Linearization
=======================

This example demonstrates how to linearize microtimes using lookup tables.
The linearization corrects for non-linear response of Time-to-Amplitude Converters (TAC).

The linearization can be applied in two ways:
1. **On-read linearization**: Apply linearization immediately after reading TTTR data
2. **Post-read linearization**: Apply linearization to already-loaded TTTR data

Based on the Seidel Software taclin algorithm (version 2008-01-28).
"""

import numpy as np
import tttrlib

# %%
# Generate Example Linearization Tables
# =====================================
#
# In practice, these tables come from calibration measurements of your measurement system.
# They represent the linearized TAC values at each channel.

def create_example_linearization_table(n_channels=256):
    """
    Create an example linearization table.

    In real use, these tables would come from calibration measurements.
    This example creates a slightly non-linear response to demonstrate the effect.
    """
    # Create a slightly non-linear response (quadratic distortion)
    channels = np.arange(n_channels, dtype=np.float32)

    # Non-linear response: quadratic distortion
    a = 1.0
    b = 0.0001  # Small quadratic term
    linearized = a * channels + b * channels**2

    return linearized.astype(np.float32)

# Create linearization tables for different channels
lt_ch0 = create_example_linearization_table(256)  # Channel 0: 256 channels
lt_ch8 = create_example_linearization_table(256)  # Channel 8: 256 channels

print(f"Linearization table for channel 0 shape: {lt_ch0.shape}")
print(f"Linearization table for channel 8 shape: {lt_ch8.shape}")

# %%
# Example 1: Post-Read Linearization
# ===================================
#
# Apply linearization to already-loaded TTTR data.

# Create synthetic TTTR data for demonstration
n_photons = 10000
macro_times = np.arange(n_photons, dtype=np.uint64) * 100  # 100 time units apart
micro_times = np.random.randint(0, 256, n_photons, dtype=np.uint16)
routing_channels = np.random.randint(0, 16, n_photons, dtype=np.int8)
event_types = np.zeros(n_photons, dtype=np.int8)

print(f"\nCreated synthetic TTTR data:")
print(f"  Photons: {n_photons}")
print(f"  Macro time range: {macro_times[0]} - {macro_times[-1]}")
print(f"  Micro time range: {micro_times.min()} - {micro_times.max()}")
print(f"  Routing channels: {np.unique(routing_channels)}")

# Create TTTR object from the synthetic data
tttr = tttrlib.TTTR(
    macro_times,
    micro_times,
    routing_channels,
    event_types
)

print(f"\nTTTR object created with {tttr.size()} events")

# Get microtime histogram before linearization
hist_before, bins_before = tttr.get_microtime_histogram()
print(f"Microtime histogram before linearization: {len(hist_before)} bins")

# Configure linearization for specific channels
print("\nConfiguring linearization...")

# Create linearizer
linearizer = tttrlib.MicrotimeLinearization()

# Set LUTs for specific channels
lut_ch0 = tttrlib.VectorFloat()
for val in lt_ch0:
    lut_ch0.append(val)
linearizer.set_channel_lut(0, lut_ch0)  # Channel 0

lut_ch8 = tttrlib.VectorFloat()
for val in lt_ch8:
    lut_ch8.append(val)
linearizer.set_channel_lut(8, lut_ch8)  # Channel 8

# Set shifts for channels (optional)
linearizer.set_channel_shift(0, 0)   # No shift for channel 0
linearizer.set_channel_shift(8, 5)   # Shift channel 8 by 5

# Configure the TTTR object with LUTs and shifts
channel_luts = tttrlib.MapIntVectorFloat()
channel_luts[0] = lut_ch0
channel_luts[8] = lut_ch8

channel_shifts = tttrlib.MapSignedCharInt()
channel_shifts[0] = 0
channel_shifts[8] = 5

result_config = tttr.apply_channel_luts(channel_luts, channel_shifts)
print(f"Configuration applied to {result_config} events")

# Apply linearization with specified seed
print("\nApplying linearization...")
result = tttr.apply_luts_and_shifts(42)  # Random seed for reproducibility

if result >= 0:
    print("Linearization successful!")
else:
    print("Linearization failed!")

# Get microtime histogram after linearization
hist_after, bins_after = tttr.get_microtime_histogram()
print(f"Microtime histogram after linearization: {len(hist_after)} bins")

# %%
# Example 2: Linearization with Environment Variable Seed
# ========================================================
#
# Use environment variable for reproducible results across sessions.

# Create new TTTR object
tttr2 = tttrlib.TTTR(
    macro_times,
    micro_times.copy(),  # Copy to avoid modifying original
    routing_channels,
    event_types
)

print(f"\nApplying linearization with environment variable seed...")

# Configure (same as above)
tttr2.apply_channel_luts(channel_luts, channel_shifts)
result = tttr2.apply_luts_and_shifts(-1)  # -1 means use TTTR_RND_SEED environment variable

if result >= 0:
    print("Linearization with environment seed successful!")
else:
    print("Linearization failed!")

# %%
# Example 3: Shifts Only (No LUTs)
# ================================
#
# Apply only channel shifts without LUT-based linearization.

# Create TTTR data
tttr3 = tttrlib.TTTR(
    macro_times,
    micro_times.copy(),
    routing_channels,
    event_types
)

print(f"\nApplying shifts only (no LUTs)...")

# Only configure shifts, no LUTs
shifts_only = tttrlib.MapSignedCharInt()
shifts_only[0] = 10   # Shift channel 0 by 10
shifts_only[8] = -3   # Shift channel 8 by -3

empty_luts = tttrlib.MapIntVectorFloat()  # No LUTs

tttr3.apply_channel_luts(empty_luts, shifts_only)
result = tttr3.apply_luts_and_shifts(42)

if result >= 0:
    print("Shifts-only linearization successful!")
else:
    print("Shifts-only linearization failed!")

# %%
# Example 4: Multiple Channels with Different LUTs
# =================================================
#
# Configure different LUTs for multiple channels.

tttr4 = tttrlib.TTTR(
    macro_times,
    micro_times.copy(),
    routing_channels,
    event_types
)

print(f"\nConfiguring multiple channels with different LUTs...")

# Create linearizer with multiple channels
linearizer4 = tttrlib.MicrotimeLinearization()

# Set LUTs for channels 0-15
multi_luts = tttrlib.MapIntVectorFloat()
multi_shifts = tttrlib.MapSignedCharInt()

for ch in range(16):
    # Create channel-specific LUT (with slight variations)
    lut = tttrlib.VectorFloat()
    base_lut = create_example_linearization_table(256)
    for i, val in enumerate(base_lut):
        lut.append(val + ch * 0.01)  # Small variation per channel

    linearizer4.set_channel_lut(ch, lut)
    multi_luts[ch] = lut
    multi_shifts[ch] = ch % 5  # Different shifts: 0, 1, 2, 3, 4, 0, 1, ...

print(f"  Configured {len(multi_luts)} channels with individual LUTs")

# Apply configuration and linearization
tttr4.apply_channel_luts(multi_luts, multi_shifts)
result = tttr4.apply_luts_and_shifts(123)

if result >= 0:
    print("Multi-channel linearization successful!")
else:
    print("Multi-channel linearization failed!")

# %%
# Example 5: Performance Comparison
# ==================================
#
# Compare performance of different linearization approaches.

import time

tttr_perf = tttrlib.TTTR(
    macro_times,
    micro_times.copy(),
    routing_channels,
    event_types
)

print("\nPerformance test with 10k photons...")

# Setup for performance test
perf_luts = tttrlib.MapIntVectorFloat()
perf_shifts = tttrlib.MapSignedCharInt()

for ch in range(16):
    lut = tttrlib.VectorFloat()
    for i in range(256):
        lut.append(float(i))
    perf_luts[ch] = lut
    perf_shifts[ch] = 0

tttr_perf.apply_channel_luts(perf_luts, perf_shifts)

# Time the linearization
start_time = time.time()
result = tttr_perf.apply_luts_and_shifts(42)
end_time = time.time()

if result >= 0:
    elapsed = end_time - start_time
    photons_per_sec = n_photons / elapsed
    print(f"  Processed {n_photons} photons in {elapsed:.2f} seconds")
    print(f"  Performance: {photons_per_sec:.0f} photons/second")
else:
    print("Performance test failed!")

# %%
# Summary
# =======
#
# The microtime linearization functionality provides:
#
# 1. **Per-channel configuration**: Direct LUT and shift assignment per routing channel
# 2. **Dithering**: Random number-based dithering reduces quantization artifacts
# 3. **Reproducibility**: Seed control for reproducible results
# 4. **Flexibility**: Can apply LUTs, shifts, or both
# 5. **Performance**: Optimized single-pass processing
# 6. **Scalability**: Supports any number of channels with independent parameters
#
# Channel configuration:
# - Channels 0-255 supported
# - Each channel can have its own LUT and shift
# - LUTs must all have the same size (validated automatically)
# - Shifts are applied modulo the LUT size or default range
#
# For more information about TAC linearization, see the Seidel Software
# taclin algorithm documentation.

print("\nExample complete!")
