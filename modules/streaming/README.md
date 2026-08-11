# `streaming` — Real-Time Photon Stream Analysis

Online analysis consumers that accept photons one at a time (or in small batches) and maintain incremental state. Designed for live environments where photons arrive continuously — e.g., a microscope acquisition, a hardware correlator FIFO, or a network stream.

## Contents

- **`StreamingCLSMImage`**: Scanned-image reconstruction from a live stream, with two modes and a switch between them that works **while events are arriving**: `LIVE` shows the frame being scanned right now, filling in as the beam sweeps; `INTEGRATING` shows the sum over every completed frame, which equals what the batch `CLSMImage` produces from the same events. Switching discards nothing — the sum accumulates in either mode — so "watch it scan, then show me the total, then go back" costs one enum write. The three views are also reachable individually: `get_current_frame()`, `get_last_frame()`, `get_integrated()`.
- **`StreamingCorrelator`**: Schätzel-style online multi-tau FCS correlator. Maintains a cascade of linear-tau blocks that coarsen by 2× per level, updated sample-by-sample. Provides raw and normalized correlation curves at any time, on the same lag axis and with the same normalization as the batch `Correlator` — see "Agreement with the batch correlator" below. Autocorrelation via `push_photon(mt, w)`; cross-correlation via `push_photon(mt, w, channel)`, with the lag running channel 0 → channel 1.
- **`StreamingBurstDetector`**: Sliding-window burst search. Tracks the last m photons in a ring buffer (memory is O(m), not O(N)) and detects burst boundaries as photons arrive. It implements the *same* criterion as `TTTR::burst_search_sliding_window` — a window of m photons spanning no more than T — and returns the same boundaries on the same photons, asserted index for index in `test/python/streaming/test_streaming_burst_detector.py`. The comparison is on the window's span, not on a count rate: `m / span` needs a special case at span zero, and the obvious guard reports m coincident photons (the highest rate there is) as no burst.
- **`StreamingDecayHistogram`**: Incremental per-channel fluorescence decay histogram. Accumulates microtimes into bins, supporting multiple routing channels (parallel/perpendicular). Query the histogram at any time for online model fitting.
- **`StreamingPhasor`**: Incremental phasor (g, s) computation for FLIM. Accumulates cos/sin of the modulated microtime signal per photon. Enables real-time phasor plots.
- **`StreamingIntensityTrace`**: Incremental MCS / intensity trace — the streaming twin of the batch free function `compute_intensity_trace`, on the same macro-time-0-aligned grid, so the two agree bin for bin. `set_max_bins(m)` keeps only the newest `m` bins (memory O(1) in run length) while `first_bin_index()` keeps the retained window's absolute place on the time axis. This is what a live acquisition displays; re-running the batch function each refresh bins *every photon of the run* to show its tail, which is O(N) per refresh and O(N²) over a measurement.

## Design Principles

1. **One photon at a time — but one *chunk* per language boundary crossing.**
   Every class has a `push_photon(...)` method and a `push_photons(ptr, n)`
   that loops in C++. From Python use `push_np(array)`, which is one call into
   C++ for the whole chunk. Looping in Python over `push_photon` measures
   **1.13 µs/photon** — 30× a numpy histogram of the same photons, enough to
   eat half a core on a 100 kHz acquisition — and until 2026-08-11 that is what
   `push_np` did, because no numpy typemap reached the array overloads.
2. **Query at any time**: All results are available incrementally — call `get_correlation()`, `get_bursts()`, etc. whenever you want.
3. **Header-only, std-only C++17**: No external dependencies, no compilation step.
4. **Stateful but resettable**: Each consumer maintains internal state. Call `clear()` to reset between acquisitions.

## Usage (Python)

```python
import tttrlib

# Live FCS correlation — one call per chunk, not one per photon
corr = tttrlib.StreamingCorrelator(16, 25, 1.0)   # n_bins, n_casc, resolution
for chunk in photon_chunks:
    corr.push_np(chunk)                           # cross-correlate: push_np(mt, w, ch)
corr.flush()                                      # emit the last partial bin
x = corr.get_x_axis()                             # n_casc * n_bins + 1 lags
g = corr.get_correlation_normalized()             # same length

# Live burst detection
detector = tttrlib.StreamingBurstDetector(window_photons=10, window_time=5e-6)
for t in photon_times:
    detector.push_photon(t)
bursts = detector.bursts  # [s0, e0, s1, e1, ...]

# Live decay histogram
hist = tttrlib.StreamingDecayHistogram(n_microtime_bins=4096, n_channels=2)
for mt, ch in zip(microtimes, channels):
    hist.push_photon(mt, ch)
decay = hist.histogram  # (n_channels, n_bins) NumPy array

# Live FLIM phasor
phasor = tttrlib.StreamingPhasor(frequency_MHz=80, n_microtime_bins=4096, microtime_resolution=1e-9)
phasor.push_np(microtimes)
g, s, n = phasor.phasor

# Live MCS trace showing the last second at 1 ms resolution
mcs = tttrlib.StreamingIntensityTrace(1e-3, 50e-9)   # bin width, macro-time clock
mcs.set_max_bins(1000)
for chunk in photon_chunks:
    mcs.push_np(chunk)
x, y = mcs.x, mcs.y                                  # bin starts [s], counts
```

## Batch APIs

For batch (non-streaming) analysis, use the existing consumers:
- `Correlator` (FCS) — Wahl/Felekyan/Laurence multi-tau, full SIMD acceleration
- `BurstFilter` — FRETBursts-style burst search with filtering pipeline
- `DecayPhasor` — FLIM phasor from photon lists

## Dependencies

- None (header-only, std-only C++17).

## Agreement with the batch correlator

`StreamingCorrelator` is not an approximation of `Correlator`: given the same
photons it returns the same numbers, and
`test/python/streaming/test_streaming_correlator.py` asserts that per cascade.
Two things make it hold, and both are easy to get wrong.

**The bins are aligned to macro time 0.** A level-*b* bin is exactly the batch
correlator's `t >> b` bin, which is what lets the two be compared sample for
sample rather than statistically.

**The first lag of a level is not `n_bins / 2`.** Block *b* of the multi-tau
axis holds coarse lags `offset(b)+1 .. offset(b)+n_bins`, where
`offset(b) = x[b * n_bins] / 2^b` — for `n_bins = 16` that is 0, 8, 12, 14, 15,
15, ..., converging to `n_bins - 1`. Only level 1 equals `n_bins / 2`. Reading
every level at a fixed `n_bins / 2` (which this class did until 2026-08-10)
returns the correlation at a shorter lag than the axis claims, which for a
decaying G(tau) reads as an inflated G: 1.19–2.39× too high from cascade 2 up.
Each level therefore carries its own first lag.

Photons must arrive in non-decreasing macro time, across both channels. `flush()`
emits the final partial bin and is idempotent; pushing after it raises.

## Cost

The correlator's natural cost is one cascade step per macro-time bin, and a real
acquisition has hundreds to thousands of *empty* bins between photons — a 100 s
run at 10 ns resolution is 10^10 bins. Empty runs are therefore skipped in
closed form rather than stepped through: level *b* emits `n0 / 2^b` times, so
the number of emissions a run of `k` empty samples covers is a difference of two
divisions, and only the first of them can be non-zero (it carries the
accumulator left over from before the run). 80k photons cost 0.063 s over a
4.0 M-bin span and 0.069 s over a 200 M-bin span — the cost does not grow with
the length of the acquisition. Numbers in [`PERF.md`](../../PERF.md).

## StreamingCLSMImage delegates rather than reimplements

Where a frame begins is not a one-line predicate. It depends on the reading
routine (the marker is in the routing channel for SP5, in the micro time for
SP8, in the event type plus routing channel by default), on a walk-back over
markers that share one macro-time tick, and on an instrument-specific
correction for the first B&H SPC frame. That logic exists in `CLSMImage`, is
tested, and is what every other consumer of this library agrees with.

So `StreamingCLSMImage` buffers exactly one frame of events and hands that
buffer to the ordinary `CLSMImage` constructor. The part that genuinely has to
change for a stream — memory proportional to a frame rather than to the
acquisition — changes; the pixel assignment is the batch's *by construction*
rather than by resemblance. `test_streaming_clsm_image.py` asserts frame-for-
frame equality with the batch, and equality of the integrated image with the
batch's frames summed.

The partially scanned frame is reconstructed **per query, not per photon**. A
viewer redraws at display rate, thousands of times slower than events arrive,
so a reconstruction per redraw is cheap — and it is what makes reusing the
batch path possible at all. Two consequences worth knowing:

- Before the first frame closes there is no settled geometry, and the frame in
  progress supplies its own — a half-scanned frame reconstructs with as many
  lines as have been scanned. Once a frame has completed, that geometry is
  imposed instead and the partial frame is drawn into the full canvas with the
  unscanned part still black. Letting an *incomplete* frame settle the geometry
  is a live-viewer bug that pins the image at whatever the first redraw saw.
- A `TTTR` assembled with `append_events` must have its used-routing-channel
  list refreshed, or `CLSMImage` fills its pixels from an empty channel list
  and returns an image of the right shape containing no photons, with no error
  anywhere. `append_events` now refreshes it (this was a real defect in core,
  found here).

`cmc` carries an equivalent decoder (`analysis/image/clsm_decoder.hpp`, with
`accumulating` / `latest` / `integrated` frames and `set_integrating`). It
predates this class and had to reimplement the marker handling because tttrlib
offered nothing to stream against; the same shape is now available here.
