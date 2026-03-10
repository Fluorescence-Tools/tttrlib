.. _troubleshooting:

Troubleshooting
===============

This page lists common issues and how to resolve them when working with tttrlib.

Type Inference Fails
--------------------
- **Symptom:** `tttrlib.TTTR(filename)` raises an error about unknown type.
- **Fix:** Provide the explicit type: `tttrlib.TTTR(filename, 'PTU')` (or `'HT3'`, `'SPC-130'`, `'PHOTON-HDF5'`). Check file extension and integrity.

Channel Mapping Mismatch
------------------------
- **Symptom:** Donor/acceptor channels appear swapped or empty selections.
- **Fix:** Verify acquisition channel assignment and update your channel lists, e.g., `get_selection_by_channel([0])`.

 HDF5 Routing Channels Differ From Reference
 -------------------------------------------
 - **Symptom:** Tests or analyses show different routing channels vs. an expected reference for HDF5 data.
 - **Fix:** Confirm dataset metadata (detector mapping) in the HDF5 file. Differences can be data-dependent; validate channel names/IDs and adjust selection lists accordingly.

Alternation Parameters (ALEX/PIE)
---------------------------------
- **Symptom:** E-S plot looks wrong or bursts appear in unexpected regions.
- **Fix:** Re-check `alex_period`/`pie_period`, `offset`, `D_ON`, `A_ON`. Start from instrument defaults and refine using alternation histograms.

Performance / Memory
--------------------
- **Symptom:** Slow operations or high memory usage on large datasets.
- **Fix:** Slice early, cache selections, avoid recomputing histograms, prefer type inference and sequential selections.

Writing/Converting Files
------------------------
- **Symptom:** Output file unreadable by another tool.
- **Fix:** Ensure header fields (e.g., container/record type) are compatible with the target format; use an existing header as a template when possible.
