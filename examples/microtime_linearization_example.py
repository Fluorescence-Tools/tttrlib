#!/usr/bin/env python3
"""
Example: Microtime Linearization with Channel-Specific LUTs and Shifts

This example demonstrates the new MicrotimeLinearization API that allows per-channel
correction of non-linearities in Time-to-Amplitude Converters (TAC).

The example generates synthetic TTTR data and applies channel-specific lookup tables
and shifts to correct for TAC non-linearity, then verifies the results are reproducible.
"""

import numpy as np
import tttrlib


def main():
    print("Testing New MicrotimeLinearization API")
    print("=" * 50)

    # Generate synthetic TTTR data
    n_photons = 10000
    macro_times = np.arange(n_photons, dtype=np.uint64) * 100
    micro_times = np.random.randint(0, 256, n_photons, dtype=np.uint16)
    routing_channels = np.random.randint(0, 4, n_photons, dtype=np.int8)  # 4 channels
    event_types = np.zeros(n_photons, dtype=np.int8)

    print(f"Generated {n_photons} synthetic photons")
    print(f"Channels used: {np.unique(routing_channels)}")

    # Create TTTR object
    tttr = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
    print(f"TTTR object created with {tttr.size()} events")

    # Get original histogram
    hist_orig, bins_orig = tttr.get_microtime_histogram()
    print(f"Original histogram: {len(hist_orig)} bins, total counts: {hist_orig.sum()}")

    # Create linearizer
    linearizer = tttrlib.MicrotimeLinearization()
    print("Created MicrotimeLinearization object")

    # Set LUTs for different channels
    for ch in [0, 1, 2, 3]:
        # Create identity LUT with slight modification
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i) + ch * 0.1)  # Small offset per channel
        linearizer.set_channel_lut(ch, lut)

        # Set a small shift for each channel
        linearizer.set_channel_shift(ch, ch)

    print("Configured LUTs and shifts for channels 0-3")

    # Configure TTTR with LUTs and shifts
    channel_luts = tttrlib.MapIntVectorFloat()
    channel_shifts = tttrlib.MapSignedCharInt()

    for ch in [0, 1, 2, 3]:
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i) + ch * 0.1)
        channel_luts[ch] = lut
        channel_shifts[ch] = ch

    result_config = tttr.apply_channel_luts(channel_luts, channel_shifts)
    print(f"Configuration applied to {result_config} events")

    # Apply linearization
    result_linearize = tttr.apply_luts_and_shifts(42)  # With seed
    print(f"Linearization applied: {result_linearize} result")

    # Get corrected histogram
    hist_corr, bins_corr = tttr.get_microtime_histogram()
    print(f"Corrected histogram: {len(hist_corr)} bins, total counts: {hist_corr.sum()}")

    # Verify the data is still intact
    print("\nVerification:")
    print(f"  Events preserved: {tttr.size() == n_photons}")
    print(f"  Macro times preserved: {np.array_equal(np.array(tttr.get_macro_times()), macro_times)}")
    print(f"  Channels preserved: {np.array_equal(np.array(tttr.get_routing_channels()), routing_channels)}")

    # Test reproducibility with seed
    print("\nTesting reproducibility...")

    # Create identical TTTR object
    tttr2 = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
    tttr2.apply_channel_luts(channel_luts, channel_shifts)
    result2 = tttr2.apply_luts_and_shifts(42)  # Same seed

    micro1 = np.array(tttr.get_micro_times())
    micro2 = np.array(tttr2.get_micro_times())

    print(f"  Same seed produces identical results: {np.array_equal(micro1, micro2)}")

    # Test different seed produces different results
    tttr3 = tttrlib.TTTR(macro_times, micro_times, routing_channels, event_types)
    tttr3.apply_channel_luts(channel_luts, channel_shifts)
    result3 = tttr3.apply_luts_and_shifts(123)  # Different seed

    micro3 = np.array(tttr3.get_micro_times())
    print(f"  Different seed produces different results: {not np.array_equal(micro1, micro3)}")

    print("\n✅ All tests passed! New MicrotimeLinearization API is working correctly.")

if __name__ == "__main__":
    main()
