Performance Guide
=================

This page summarizes tips for fast and memory-efficient use of ``tttrlib`` and
tracks how performance and memory consumption evolve across releases.

Cross-version performance & memory (0.27.0 vs 0.26.2)
-----------------------------------------------------

Release 0.27.0 is a performance **and** memory release. Confocal (CLSM/FLIM)
image reconstruction moved to a lazy, per-event stream-mask fill: instead of
eagerly materializing a ``std::vector`` of photon indices per pixel, ``fill()``
stores a one-bit-per-event acceptance mask and builds per-pixel containers only
when a pixel handle is actually requested. Intensity, lifetime, phasor and
tttr-index queries run straight off the mask. The new
``CLSMImage(..., build_pixels=False)`` "virtual fill" skips per-pixel allocation
entirely for intensity-only work.

The numbers below are produced by the cross-version monitor in
``benchmarks/`` (``bench_versions.py`` + ``make_version_plots.py``), which runs
each released version in its own isolated environment and the working-tree build
in the base environment, one task per subprocess. Wall time is the best of
several repeats; **memory is the task footprint — peak process RSS minus the
post-import baseline** — measured with ``getrusage`` because the wins live in the
C++ heap, which ``tracemalloc`` cannot see. Machine: Apple M1 Pro, CPU only.

.. image:: _static/benchmarks/summary_versions.png
   :alt: Peak-memory change of tttrlib 0.27.0 vs 0.26.2 per task
   :width: 90%

.. list-table:: tttrlib 0.27.0 vs 0.26.2 (same machine, same inputs; negative Δ is better)
   :header-rows: 1
   :widths: 38 15 15 10 12 10

   * - Task
     - 0.26.2 time
     - 0.27.0 time
     - Δ time
     - Δ memory
     - Notes
   * - CLSM fill+structure (512×512 PTU)
     - 125 ms
     - 46 ms
     - **−63%**
     - **−12%**
     - lazy stream-mask fill
   * - CLSM fill, 2.6 M-pixel FLIM image (40×256×256 HT3)
     - 1484 ms
     - 279 ms
     - **−81%**
     - **−40%**
     - largest memory win
   * - Correlation / FCS (3.5 M photons)
     - 438 ms
     - 279 ms
     - −36%
     - −7%
     -
   * - TTTR file reading (3.5 M-photon PTU)
     - 31 ms
     - 30 ms
     - −4%
     - ≈0
     - I/O bound
   * - Burst search (3.5 M photons)
     - 2.7 ms
     - 2.8 ms
     - ≈0
     - ≈0
     -
   * - CLSM intensity, virtual fill (512×512 PTU)
     - —
     - 22 ms
     - new
     - new
     - ``build_pixels=False``
   * - Per-pixel reconvolution-MLE map (256×256)
     - —
     - 900 ms
     - new
     - new
     - ``FitNExp.fit_map``

To reproduce or extend to a future version::

    cd benchmarks
    python bench_versions.py --versions 0.26.2 0.27.0=local
    python make_version_plots.py     # -> plots/versions/*.png + summary.md

Honest caveat: the warm mean-lifetime / phasor caches trade a little resident
memory (a few tens of bytes per pixel) for large speed-ups when the IRF,
background or modulation frequency is re-tuned on an already-built map — the
opposite direction from the fill wins above, and only paid once a cache is warm.

Selections and Slicing
----------------------
- Prefer sequential selections when possible; they are faster and more memory-friendly.
- Slice early to reduce working set size before heavy computations.

Correlation
-----------
- Use ``tttrlib.Correlator`` for efficient multi-tau correlation.
- Precompute and reuse selections for channels and time windows to avoid repeated filtering.

Lifetime/Histogramming
----------------------
- Reuse allocated arrays when calling histogram methods in hot loops.
- Use instrument-specific prompt windows for reduced data ranges.

I/O and Compression
-------------------
- Reading uses compression by default for sequential selections.
- For non-sequential selections, internal logic avoids compression to keep correctness.
- Control compression-on-read via the environment variable
  ``TTTR_COMPRESS_ON_READ``. Set it to ``0``, ``false``, or ``off`` to disable
  compression on read.

Memory
------
- Avoid unnecessary copies: use indices to reference subsets such as
  ``tttr[idx]``.
- Convert to smaller dtypes only if compatible with your device header and downstream code.

Diagnostics
-----------
- Enable verbose diagnostics with ``TTTRLIB_VERBOSE=1`` to understand hotspots
  and I/O stages.

Example Checklist
-----------------
- **[ ]** Compute selections once and reuse
- **[ ]** Use sequential selections where feasible
- **[ ]** Keep windows narrow (prompt/decay ranges)
- **[ ]** Avoid executing notebooks during CI builds
  (``nbsphinx_execute = 'never'``)
