"""
TTTR Compression Examples

This script demonstrates the delta encoding compression feature for TTTR data,
showing how to reduce memory usage for large photon event datasets.
"""

import tttrlib
import numpy as np
import time


def print_compression_stats(stats):
    """Print compression statistics"""
    print("\n=== Compression Statistics ===")
    print(f"Events:           {stats.n_events:,}")
    print(f"Uncompressed:     {stats.uncompressed_bytes / 1024**2:.2f} MB")
    print(f"Compressed:       {stats.compressed_bytes / 1024**2:.2f} MB")
    print(f"Compression:      {stats.compression_ratio * 100:.1f}%")
    print(f"Space saved:      {(1 - stats.compression_ratio) * 100:.1f}%")


def example_basic_compression():
    """Example 1: Basic compression"""
    print("\n### Example 1: Basic Compression ###")
    
    data = tttrlib.TTTR()
    n_events = 1_000_000
    
    # Create typical photon stream data
    macro_times = np.arange(n_events, dtype=np.uint64) * 100
    micro_times = np.arange(n_events, dtype=np.uint16) % 4096
    channels = np.arange(n_events, dtype=np.int8) % 4
    types = np.zeros(n_events, dtype=np.int8)
    
    data.reserve(n_events)
    data.append_events(macro_times, micro_times, channels, types)
    
    print("Before compression:")
    print(f"Memory: {data.get_memory_usage_bytes() / 1024**2:.2f} MB")
    print(f"Compressed: {data.is_compressed()}")
    
    # Compress
    stats = data.compress_data()
    print_compression_stats(stats)
    
    print("\nAfter compression:")
    print(f"Compressed: {data.is_compressed()}")


def example_compression_patterns():
    """Example 2: Compression with different data patterns"""
    print("\n### Example 2: Compression with Different Patterns ###")
    
    n = 100_000
    
    # Pattern 1: Regular spacing (best compression)
    data1 = tttrlib.TTTR()
    macro_times = np.arange(n, dtype=np.uint64) * 100
    micro_times = np.arange(n, dtype=np.uint16) % 4096
    channels = np.zeros(n, dtype=np.int8)
    types = np.zeros(n, dtype=np.int8)
    data1.append_events(macro_times, micro_times, channels, types)
    stats1 = data1.compress_data()
    print(f"\nRegular spacing (100 units):")
    print(f"  Compression: {stats1.compression_ratio * 100:.1f}%")
    
    # Pattern 2: High count rate (small deltas, good compression)
    data2 = tttrlib.TTTR()
    macro_times = np.arange(n, dtype=np.uint64) * 10
    data2.append_events(macro_times, micro_times, channels, types)
    stats2 = data2.compress_data()
    print(f"\nHigh count rate (10 units):")
    print(f"  Compression: {stats2.compression_ratio * 100:.1f}%")
    
    # Pattern 3: Burst data (variable spacing)
    data3 = tttrlib.TTTR()
    macro_times = np.zeros(n, dtype=np.uint64)
    t = 0
    for i in range(n):
        if i % 20 < 10:
            t += 5  # Small delta in burst
        else:
            t += 1000  # Large delta between bursts
        macro_times[i] = t
    data3.append_events(macro_times, micro_times, channels, types)
    stats3 = data3.compress_data()
    print(f"\nBurst pattern (mixed deltas):")
    print(f"  Compression: {stats3.compression_ratio * 100:.1f}%")


def example_roundtrip():
    """Example 3: Compression round-trip"""
    print("\n### Example 3: Compression Round-trip ###")
    
    n_events = 10_000
    
    # Create test data
    original_macro = np.arange(n_events, dtype=np.uint64) * 100 + (np.arange(n_events) % 10)
    original_micro = np.arange(n_events, dtype=np.uint16) % 4096
    channels = np.arange(n_events, dtype=np.int8) % 4
    types = np.zeros(n_events, dtype=np.int8)
    
    data = tttrlib.TTTR()
    data.append_events(original_macro, original_micro, channels, types)
    
    print(f"Original data created: {n_events:,} events")
    
    # Compress
    stats = data.compress_data()
    print(f"Compressed to {stats.compression_ratio * 100:.1f}%")
    
    # Decompress
    data.decompress_data()
    print("Decompressed back to normal storage")
    
    # Verify data integrity
    macro_out = data.get_macro_times()
    micro_out = data.get_micro_times()
    
    if np.array_equal(macro_out, original_macro) and np.array_equal(micro_out, original_micro):
        print("✓ Data integrity verified - all events match!")
    else:
        print("✗ Data integrity check failed!")


def example_file_compression(filename):
    """Example 4: File loading with compression"""
    print("\n### Example 4: File Loading with Compression ###")
    
    try:
        # Load file
        start = time.time()
        data = tttrlib.TTTR(filename)
        load_time = time.time() - start
        
        print(f"Loaded {data.size():,} events in {load_time*1000:.1f} ms")
        print(f"Memory (uncompressed): {data.get_memory_usage_bytes() / 1024**2:.2f} MB")
        
        # Compress
        start = time.time()
        stats = data.compress_data()
        compress_time = time.time() - start
        
        print_compression_stats(stats)
        print(f"Compression time: {compress_time*1000:.1f} ms")
        
        # Decompress
        start = time.time()
        data.decompress_data()
        decompress_time = time.time() - start
        
        print(f"Decompression time: {decompress_time*1000:.1f} ms")
        
    except Exception as e:
        print(f"Error: {e}")


def example_use_cases():
    """Example 5: When to use compression"""
    print("\n### Example 5: Compression Use Cases ###")
    
    print("\n✓ GOOD use cases for compression:")
    print("  - Archival storage of large datasets")
    print("  - Memory-constrained environments")
    print("  - Datasets accessed infrequently")
    print("  - High count rate data (small deltas)")
    print("  - After filtering (before shrink_to_fit)")
    
    print("\n✗ NOT recommended for:")
    print("  - Data that needs frequent random access")
    print("  - Real-time processing pipelines")
    print("  - Very small datasets (<10K events)")
    print("  - Data being actively modified")
    
    print("\n💡 Typical workflow:")
    print("  1. Load and process data (uncompressed)")
    print("  2. Apply filters/selections")
    print("  3. Compress for storage/archival")
    print("  4. Decompress when needed for analysis")


def example_compress_after_filter():
    """Example 6: Compress after filtering"""
    print("\n### Example 6: Compress After Filtering ###")
    
    n_events = 1_000_000
    
    # Create multi-channel data
    data = tttrlib.TTTR()
    macro_times = np.arange(n_events, dtype=np.uint64) * 100
    micro_times = np.arange(n_events, dtype=np.uint16) % 4096
    channels = np.arange(n_events, dtype=np.int8) % 8
    types = np.zeros(n_events, dtype=np.int8)
    data.append_events(macro_times, micro_times, channels, types)
    
    print(f"Original: {data.size():,} events, "
          f"{data.get_memory_usage_bytes() / 1024**2:.2f} MB")
    
    # Filter to single channel
    routing_channels = data.get_routing_channel()
    selection = np.where(routing_channels == 0)[0].astype(np.int32)
    
    filtered = data.select(selection)
    print(f"Filtered: {filtered.size():,} events, "
          f"{filtered.get_memory_usage_bytes() / 1024**2:.2f} MB")
    
    # Option 1: Shrink to fit
    filtered.shrink_to_fit()
    print(f"After shrink_to_fit: "
          f"{filtered.get_memory_usage_bytes() / 1024**2:.2f} MB")
    
    # Option 2: Compress (even better for archival)
    stats = filtered.compress_data()
    print(f"After compression: "
          f"{stats.compressed_bytes / 1024**2:.2f} MB")
    
    print("\nMemory reduction:")
    print(f"  Original → Filtered: {100.0 * filtered.size() / data.size():.1f}%")
    print(f"  Original → Compressed: "
          f"{100.0 * stats.compressed_bytes / data.get_memory_usage_bytes():.1f}%")


def example_benchmark():
    """Example 7: Compression benchmark"""
    print("\n### Example 7: Compression Benchmark ###")
    
    sizes = [10_000, 100_000, 1_000_000]
    
    print("\n{:<15} {:<15} {:<15} {:<15}".format(
        "Events", "Compress (ms)", "Decompress (ms)", "Ratio"))
    print("-" * 60)
    
    for n in sizes:
        data = tttrlib.TTTR()
        macro_times = np.arange(n, dtype=np.uint64) * 100
        micro_times = np.arange(n, dtype=np.uint16) % 4096
        channels = np.zeros(n, dtype=np.int8)
        types = np.zeros(n, dtype=np.int8)
        data.append_events(macro_times, micro_times, channels, types)
        
        # Compress
        start = time.time()
        stats = data.compress_data()
        compress_time = (time.time() - start) * 1000
        
        # Decompress
        start = time.time()
        data.decompress_data()
        decompress_time = (time.time() - start) * 1000
        
        print("{:<15,} {:<15.2f} {:<15.2f} {:<15.1f}%".format(
            n, compress_time, decompress_time, stats.compression_ratio * 100))


def main():
    import sys
    
    print("TTTR Compression Examples")
    print("=========================")
    
    # Run examples
    example_basic_compression()
    example_compression_patterns()
    example_roundtrip()
    example_use_cases()
    example_compress_after_filter()
    example_benchmark()
    
    # If a filename is provided, demonstrate file compression
    if len(sys.argv) > 1:
        example_file_compression(sys.argv[1])
    else:
        print("\nTip: Run with a TTTR filename to see file compression:")
        print(f"  python {sys.argv[0]} <filename.ptu>")
    
    print("\n=== Summary ===")
    print("Delta encoding compression:")
    print("  • Typical savings: 40-60% of original size")
    print("  • Best for: High count rate, regular spacing")
    print("  • Lossless: Perfect data reconstruction")
    print("  • Fast: Compression/decompression in milliseconds")


if __name__ == "__main__":
    main()
