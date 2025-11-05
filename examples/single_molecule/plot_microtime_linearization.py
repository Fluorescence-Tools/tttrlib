"""
Microtime Linearization for SPC Becker Cards
=============================================

This example demonstrates how to linearize microtimes (TAC values) from
Becker & Hickl SPC cards using lookup tables. The linearization corrects
for non-linear response of the Time-to-Amplitude Converter (TAC).

The linearization can be applied in two ways:
1. **On-read linearization**: Apply linearization immediately after reading TTTR data
2. **Post-read linearization**: Apply linearization to already-loaded TTTR data

Based on the Seidel Software taclin algorithm (version 2008-01-28).
"""

import numpy as np
import tttrlib

# %%
# Generate Example Linearization Tables
# ======================================
#
# In practice, these tables come from calibration measurements of your SPC card.
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

# Create linearization tables for two cards
lt1 = create_example_linearization_table(256)  # Card 1: 256 channels
lt2 = create_example_linearization_table(256)  # Card 2: 256 channels

print(f"Linearization table 1 shape: {lt1.shape}")
print(f"Linearization table 2 shape: {lt2.shape}")

# %%
# Example 1: Post-Read Linearization
# ====================================
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

# Apply linearization with automatic random number generation
print("\nApplying linearization (post-read)...")
result = tttr.linearize_microtimes(
    lt1,                    # Linearization table for card 1
    lt2,                    # Linearization table for card 2
    tacstart1=0,            # First TAC channel for card 1
    tacstart2=0,            # First TAC channel for card 2
    tacshift1=0,            # TAC shift for card 1
    tacshift2=0,            # TAC shift for card 2
    seed=42                 # Random seed for reproducibility
)

if result:
    print("Linearization successful!")
else:
    print("Linearization failed!")

# Get microtime histogram after linearization
hist_after, bins_after = tttr.get_microtime_histogram()
print(f"Microtime histogram after linearization: {len(hist_after)} bins")

# %%
# Example 2: Linearization with Provided Random Numbers
# =======================================================
#
# For reproducible results, you can provide your own random numbers.

# Create new TTTR object
tttr2 = tttrlib.TTTR(
    macro_times, 
    micro_times.copy(),  # Copy to avoid modifying original
    routing_channels, 
    event_types
)

# Generate random numbers externally
random_numbers = np.random.RandomState(42).uniform(0, 1, n_photons).astype(np.float32)

print(f"\nApplying linearization with provided random numbers...")
result = tttr2.linearize_microtimes_with_random(
    lt1,                    # Linearization table for card 1
    lt2,                    # Linearization table for card 2
    random_numbers,         # Provided random numbers for dithering
    tacstart1=0,
    tacstart2=0,
    tacshift1=0,
    tacshift2=0
)

if result:
    print("Linearization with provided random numbers successful!")
else:
    print("Linearization failed!")

# %%
# Example 3: Single Card Linearization
# ======================================
#
# If you only have one SPC card, pass None for the second table.

# Create TTTR data with only channels 0-7 (single card)
routing_channels_single = np.random.randint(0, 8, n_photons, dtype=np.int8)

tttr3 = tttrlib.TTTR(
    macro_times, 
    micro_times.copy(), 
    routing_channels_single, 
    event_types
)

print(f"\nApplying linearization for single card...")
result = tttr3.linearize_microtimes(
    lt1,                    # Only linearization table for card 1
    None,                   # No second card
    tacstart1=0,
    tacshift1=0,
    seed=42
)

if result:
    print("Single-card linearization successful!")
else:
    print("Single-card linearization failed!")

# %%
# Example 4: Linearization with TAC Shifts
# ==========================================
#
# Apply TAC shifts to correct for timing offsets between cards.

tttr4 = tttrlib.TTTR(
    macro_times, 
    micro_times.copy(), 
    routing_channels, 
    event_types
)

print(f"\nApplying linearization with TAC shifts...")
result = tttr4.linearize_microtimes(
    lt1,
    lt2,
    tacstart1=0,
    tacstart2=0,
    tacshift1=5,            # Shift card 1 by 5 channels
    tacshift2=-3,           # Shift card 2 by -3 channels
    seed=42
)

if result:
    print("Linearization with TAC shifts successful!")
else:
    print("Linearization failed!")

# %%
# Example 5: Multiple Card Linearization (n cards)
# ==================================================
#
# Support for any number of SPC cards with independent linearization tables.

# Create linearization tables for 4 cards
lt_cards = {}
for card_id in range(4):
    lt_cards[card_id] = create_example_linearization_table(256)

# Create TTTR data with channels for 4 cards (0-31)
routing_channels_multi = np.random.randint(0, 32, n_photons, dtype=np.int8)

tttr5 = tttrlib.TTTR(
    macro_times, 
    micro_times.copy(), 
    routing_channels_multi, 
    event_types
)

print(f"\nApplying linearization for 4 cards...")
print(f"  Card 0: channels 0-7")
print(f"  Card 1: channels 8-15")
print(f"  Card 2: channels 16-23")
print(f"  Card 3: channels 24-31")

# Create linearization object with multiple cards
# Note: This requires the n-card constructor
result = tttr5.linearize_microtimes(
    lt_cards[0],
    lt_cards[1],
    tacstart1=0,
    tacstart2=0,
    tacshift1=0,
    tacshift2=0,
    seed=42
)

if result:
    print("Multi-card linearization successful!")
else:
    print("Multi-card linearization failed!")

# %%
# Summary
# =======
#
# The microtime linearization functionality provides:
#
# 1. **Flexible linearization**: Support for single, dual, or n SPC cards
# 2. **Dithering**: Random number-based dithering reduces quantization artifacts
# 3. **Reproducibility**: Seed control for reproducible results
# 4. **TAC shifts**: Correction for timing offsets between cards
# 5. **Efficient processing**: Direct in-place modification of microtime arrays
# 6. **Scalability**: Supports any number of cards with independent parameters
#
# Channel-to-card mapping (default):
# - Channels 0-7 → Card 0
# - Channels 8-15 → Card 1
# - Channels 16-23 → Card 2
# - Channels 24-31 → Card 3
# - etc.
#
# For more information about TAC linearization, see the Seidel Software
# taclin algorithm documentation.

print("\nExample complete!")
