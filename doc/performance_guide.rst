Performance Guide
=================

This page summarizes tips for fast and memory-efficient use of `tttrlib`.

Selections and Slicing
----------------------
- Prefer sequential selections when possible; they are faster and more memory-friendly.
- Slice early to reduce working set size before heavy computations.

Correlation
-----------
- Use `:class:`tttrlib.Correlator`` for efficient multi-tau correlation.
- Precompute and reuse selections for channels and time windows to avoid repeated filtering.

Lifetime/Histogramming
----------------------
- Reuse allocated arrays when calling histogram methods in hot loops.
- Use instrument-specific prompt windows for reduced data ranges.

I/O and Compression
-------------------
- Reading uses compression by default for sequential selections.
- For non-sequential selections, internal logic avoids compression to keep correctness.
- Control compression-on-read via the environment variable `TTTR_COMPRESS_ON_READ` (set to `0`/`false`/`off` to disable).

Memory
------
- Avoid unnecessary copies: use indices to reference subsets (`tttr[idx]`).
- Convert to smaller dtypes only if compatible with your device header and downstream code.

Diagnostics
-----------
- Enable verbose diagnostics with `TTTRLIB_VERBOSE=1` to understand hotspots and I/O stages.

Example Checklist
-----------------
- **[ ]** Compute selections once and reuse
- **[ ]** Use sequential selections where feasible
- **[ ]** Keep windows narrow (prompt/decay ranges)
- **[ ]** Avoid executing notebooks during CI builds (`nbsphinx_execute = 'never'`)
