#!/usr/bin/env python
"""
Example: CLSM Image Memory Usage Reporting

This example demonstrates how to use the memory usage reporting methods
to analyze and optimize memory consumption in CLSM images.
"""

import tttrlib
import numpy as np


def format_bytes(bytes_val):
    """Format bytes into human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.2f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.2f} TB"


def analyze_clsm_memory(clsm_image):
    """Analyze and report memory usage of a CLSM image."""
    
    # Get total memory usage
    total_memory = clsm_image.get_memory_usage_bytes()
    
    # Get detailed breakdown
    overhead, indices, ranges = clsm_image.get_memory_usage_detailed()
    
    # Calculate statistics
    n_frames = clsm_image.get_n_frames()
    n_lines = clsm_image.get_n_lines()
    n_pixels = clsm_image.get_n_pixel()
    total_pixels = n_frames * n_lines * n_pixels
    
    # Count total photons
    total_photons = 0
    for frame_idx in range(n_frames):
        frame = clsm_image[frame_idx]
        for line in frame.get_lines():
            for pixel in line.get_pixels():
                total_photons += pixel.get_index_count()
    
    # Print report
    print("=" * 70)
    print("CLSM Image Memory Usage Report")
    print("=" * 70)
    print(f"\nImage Dimensions:")
    print(f"  Frames:       {n_frames:,}")
    print(f"  Lines/frame:  {n_lines:,}")
    print(f"  Pixels/line:  {n_pixels:,}")
    print(f"  Total pixels: {total_pixels:,}")
    print(f"  Total photons: {total_photons:,}")
    
    if total_pixels > 0:
        print(f"  Avg photons/pixel: {total_photons / total_pixels:.1f}")
    
    print(f"\nMemory Usage:")
    print(f"  Total:        {format_bytes(total_memory)}")
    print(f"  Overhead:     {format_bytes(overhead)} ({100 * overhead / total_memory:.1f}%)")
    print(f"  Indices:      {format_bytes(indices)} ({100 * indices / total_memory:.1f}%)")
    print(f"  Ranges:       {format_bytes(ranges)} ({100 * ranges / total_memory:.1f}%)")
    
    print(f"\nMemory per Element:")
    if total_pixels > 0:
        print(f"  Per pixel:    {total_memory / total_pixels:.1f} bytes")
    if total_photons > 0:
        print(f"  Per photon:   {indices / total_photons:.1f} bytes")
    
    print(f"\nEfficiency:")
    if total_photons > 0:
        # Theoretical minimum: 4 bytes per photon index
        theoretical_min = total_photons * 4
        efficiency = 100 * theoretical_min / total_memory
        print(f"  Storage efficiency: {efficiency:.1f}%")
        print(f"  Overhead factor:    {total_memory / theoretical_min:.2f}x")
    
    print("=" * 70)
    
    return {
        'total': total_memory,
        'overhead': overhead,
        'indices': indices,
        'ranges': ranges,
        'n_pixels': total_pixels,
        'n_photons': total_photons
    }


def compare_memory_before_after_fill(tttr_data, settings):
    """Compare memory usage before and after filling pixels."""
    
    print("\n" + "=" * 70)
    print("Memory Comparison: Before vs After Fill")
    print("=" * 70)
    
    # Create image without filling
    print("\n1. Creating CLSM image structure (no photons)...")
    clsm_empty = tttrlib.CLSMImage(
        tttr_data,
        settings,
        fill=False
    )
    
    empty_memory = clsm_empty.get_memory_usage_bytes()
    print(f"   Memory (structure only): {format_bytes(empty_memory)}")
    
    # Fill with photons
    print("\n2. Filling pixels with photons...")
    clsm_filled = tttrlib.CLSMImage(
        tttr_data,
        settings,
        fill=True
    )
    
    filled_memory = clsm_filled.get_memory_usage_bytes()
    print(f"   Memory (with photons):   {format_bytes(filled_memory)}")
    
    # Calculate difference
    photon_memory = filled_memory - empty_memory
    print(f"\n3. Memory used by photon indices: {format_bytes(photon_memory)}")
    print(f"   Increase: {100 * photon_memory / empty_memory:.1f}%")
    
    return clsm_filled


def demonstrate_frame_stacking_impact(clsm_image):
    """Demonstrate memory impact of frame stacking."""
    
    print("\n" + "=" * 70)
    print("Memory Impact of Frame Stacking")
    print("=" * 70)
    
    # Memory before stacking
    n_frames_before = clsm_image.get_n_frames()
    memory_before = clsm_image.get_memory_usage_bytes()
    
    print(f"\nBefore stacking:")
    print(f"  Frames: {n_frames_before}")
    print(f"  Memory: {format_bytes(memory_before)}")
    
    # Stack frames (modifies in place)
    print("\nStacking frames...")
    clsm_image.stack_frames()
    
    # Memory after stacking
    n_frames_after = clsm_image.get_n_frames()
    memory_after = clsm_image.get_memory_usage_bytes()
    
    print(f"\nAfter stacking:")
    print(f"  Frames: {n_frames_after}")
    print(f"  Memory: {format_bytes(memory_after)}")
    
    # Calculate savings
    memory_saved = memory_before - memory_after
    print(f"\nMemory saved: {format_bytes(memory_saved)} ({100 * memory_saved / memory_before:.1f}%)")


def main():
    """Main example function."""
    
    # Load TTTR data
    print("Loading TTTR data...")
    tttr_data = tttrlib.TTTR('path/to/your/data.ptu')
    
    # Configure CLSM settings
    settings = tttrlib.CLSMSettings(
        marker_frame_start=[4],
        marker_line_start=1,
        marker_line_stop=2,
        n_pixel_per_line=256
    )
    
    # Example 1: Analyze memory usage
    print("\n" + "=" * 70)
    print("Example 1: Memory Usage Analysis")
    print("=" * 70)
    
    clsm = tttrlib.CLSMImage(tttr_data, settings, fill=True)
    stats = analyze_clsm_memory(clsm)
    
    # Example 2: Compare before/after fill
    print("\n" + "=" * 70)
    print("Example 2: Memory Impact of Filling")
    print("=" * 70)
    
    clsm_filled = compare_memory_before_after_fill(tttr_data, settings)
    
    # Example 3: Frame stacking impact
    if clsm_filled.get_n_frames() > 1:
        print("\n" + "=" * 70)
        print("Example 3: Frame Stacking Impact")
        print("=" * 70)
        
        # Make a copy for stacking
        clsm_copy = tttrlib.CLSMImage(clsm_filled, fill=True)
        demonstrate_frame_stacking_impact(clsm_copy)
    
    # Example 4: Per-frame analysis
    print("\n" + "=" * 70)
    print("Example 4: Per-Frame Memory Analysis")
    print("=" * 70)
    
    print(f"\nAnalyzing first 5 frames...")
    for i in range(min(5, clsm.get_n_frames())):
        frame = clsm[i]
        frame_memory = frame.get_memory_usage_bytes()
        print(f"  Frame {i}: {format_bytes(frame_memory)}")
    
    print("\n" + "=" * 70)
    print("Analysis complete!")
    print("=" * 70)


if __name__ == '__main__':
    # Note: This example requires a valid TTTR file
    # Modify the file path in main() to point to your data
    
    print("""
CLSM Memory Usage Example
=========================

This example demonstrates:
1. Total memory usage reporting
2. Detailed memory breakdown (overhead, indices, ranges)
3. Memory comparison before/after filling
4. Frame stacking impact on memory
5. Per-frame memory analysis

To run this example:
1. Update the file path in main() to point to your TTTR data
2. Adjust CLSMSettings parameters for your data format
3. Run: python clsm_memory_usage_example.py

For more information, see:
- doc/CLSM_MEMORY_OPTIMIZATION.md
- doc/MEMORY_OPTIMIZATION.md
""")
    
    # Uncomment to run the example:
    # main()
