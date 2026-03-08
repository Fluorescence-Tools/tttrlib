# Logic Analyzer Correlation Examples

This directory contains examples for computing correlation functions from Logic Analyzer data using the SALEAE_LOGIC2 integration in tttrlib.

## Examples

### 1. Simple Correlation Example (`simple_correlation_example.py`)
A minimal example to get started with correlation analysis.

**Features:**
- Synthetic data generation with known correlation
- Basic correlation algorithm
- Real Logic Analyzer file support
- Simple visualization

**Usage:**
```bash
# Use synthetic data
python simple_correlation_example.py --synthetic

# Try to use real Logic Analyzer files
python simple_correlation_example.py
```

### 2. Logic Analyzer Correlation (`logic_analyzer_correlation.py`)
Comprehensive example with multiple correlation methods.

**Features:**
- Single-file multi-channel correlation
- Multi-file correlation (Version 0 files)
- Synthetic data with known correlation patterns
- Performance comparison

**Usage:**
```bash
python logic_analyzer_correlation.py
```

### 3. Advanced Correlation (`logic_analyzer_correlation_advanced.py`)
High-performance correlation with optimization.

**Features:**
- tttrlib Correlator class integration
- Optimized numpy implementation
- Multi-channel pairwise correlations
- Performance benchmarking

**Usage:**
```bash
python logic_analyzer_correlation_advanced.py
```

## Data Requirements

### Logic Analyzer Files
The examples support both Saleae Logic 2 binary formats:

- **Version 0**: Single channel per file
  - `digital_0.bin`, `digital_1.bin`, etc.
  
- **Version 1**: Multiple channels in single file
  - `digital_multi.bin`

### File Locations
Default paths (can be modified in the scripts):
```
Q:/tttr-data/saleae/Logic2binary/
├── digital_0.bin
├── digital_1.bin
└── digital_multi.bin
```

## Correlation Methods

### 1. Basic Algorithm
Simple O(n²) algorithm suitable for small datasets:
```python
for t1 in times1:
    for t2 in times2:
        dt = t2 - t1
        if 0 <= dt <= max_lag:
            bin_idx = int(dt / bin_width)
            correlation[bin_idx] += 1
```

### 2. Optimized Numpy
Vectorized implementation using numpy operations:
```python
for t1 in times1[::sample_rate]:
    dt = times2 - t1
    mask = (dt >= 0) & (dt <= max_lag)
    bin_indices = np.round(dt[mask] / bin_width).astype(int)
    np.add.at(correlation, bin_indices, 1)
```

### 3. tttrlib Correlator
High-performance C++ implementation:
```python
correlator = tttrlib.Correlator()
correlator.set_tttr1(tttr1)
correlator.set_tttr2(tttr2)
correlator.compute_correlation()
```

## Parameters

### Correlation Parameters
- `max_time_lag`: Maximum time lag to compute (nanoseconds)
- `bin_width`: Histogram bin width (nanoseconds)
- `sample_rate`: Downsampling factor for large datasets

### Typical Values
```python
max_time_lag = 1e6    # 1 millisecond
bin_width = 1000      # 1 microsecond
sample_rate = 10      # Use every 10th event
```

## Output

### Correlation Curve
The correlation function G(τ) is computed as:
```
G(τ) = <I₁(t) I₂(t+τ)> / (<I₁> <I₂>)
```

Where:
- I₁, I₂ are intensity time series
- τ is the time lag
- <...> denotes time averaging

### Visualization
All examples generate plots showing:
- Time lag (μs) on x-axis
- Correlation rate (Hz) on y-axis
- Peak correlation with annotation

## Performance

### Dataset Sizes
- Small: < 10,000 events per channel
- Medium: 10,000 - 100,000 events
- Large: > 100,000 events

### Benchmarks
Typical computation times (for 50,000 events):
- Basic algorithm: ~10 seconds
- Optimized numpy: ~2 seconds
- tttrlib Correlator: ~0.1 seconds

## Integration with SALEAE_LOGIC2

### Reading Logic Analyzer Files
```python
# Single file (Version 1)
tttr = tttrlib.TTTR("digital_multi.bin", "SALEAE_LOGIC2")

# Multiple files (Version 0)
tttr_ch0 = tttrlib.TTTR("digital_0.bin", "SALEAE_LOGIC2")
tttr_ch1 = tttrlib.TTTR("digital_1.bin", "SALEAE_LOGIC2")
```

### Multi-Channel Processing
```python
# Split multi-channel TTTR
channels = tttr.get_routing_channels()
unique_channels = np.unique(channels)

channel_tttrs = {}
for ch in unique_channels:
    mask = channels == ch
    macro_times = tttr.get_macro_times()[mask]
    routing_ch = np.full(len(macro_times), ch, dtype=np.int8)
    channel_tttrs[ch] = tttrlib.TTTR(macro_times, None, routing_ch, None, None, None)
```

### Merging Channels
```python
# Merge multiple channels for analysis
tttr_merged = tttrlib.TTTR(channel_tttrs[0])
for ch in list(channel_tttrs.keys())[1:]:
    tttr_merged.merge(channel_tttrs[ch], 0, ch, 1)  # Interleave merge
```

## Troubleshooting

### Common Issues

1. **ImportError: tttrlib not available**
   - Solution: Rebuild Python bindings
   - Check CMAKE build with BUILD_LIBRARY=ON

2. **Memory errors with large files**
   - Solution: Use sample_rate parameter
   - Process in chunks

3. **No correlation peak**
   - Check channel assignments
   - Verify time scale (nanoseconds)
   - Increase max_time_lag

### Debug Tips
```python
# Check data
print(f"Events: {tttr.get_n_valid_events()}")
print(f"Channels: {np.unique(tttr.get_routing_channels())}")
print(f"Time range: {tttr.get_macro_times().min()} to {tttr.get_macro_times().max()}")

# Check correlation
print(f"Peak at: {time_lags[peak_idx]/1000:.1f} μs")
print(f"Peak value: {correlation[peak_idx]:.2e} Hz")
```

## Requirements

- Python 3.8+
- tttrlib with SALEAE_LOGIC2 support
- numpy
- matplotlib
- (Optional) scipy for advanced fitting

## Installation

Make sure tttrlib is compiled with SALEAE_LOGIC2 support:
```bash
cd e:\dev\tttrlib
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRARY=ON
cmake --build build-release --config Release
```

## References

- Saleae Logic 2 documentation
- tttrlib correlation module
- Fluorescence correlation spectroscopy theory
