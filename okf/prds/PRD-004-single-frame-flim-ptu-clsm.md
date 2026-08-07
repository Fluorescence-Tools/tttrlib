# PRD-004 — Single-frame FLIM PTU CLSM reconstruction

> **PRD #:** 004 · **Status:** Done · **Created:** 2026-07-03 · **Closed:** 2026-08-05 · **Owner:** tpeulen

## Summary

Reconstruct CLSM images from PicoQuant PTU FLIM files that contain **no frame
marker** (a single continuous frame) — currently they reconstruct to 0 frames.

## Problem / motivation

Some PicoHarp/SymPhoTime PTU FLIM acquisitions store line markers but **no frame
marker** (the whole acquisition is one frame). `CLSMImage` reads a frame-start
marker from the header (`ImgHdr_Frame`) and, finding no matching marker events in
the stream, produces **0 frames** — the image is empty. Observed with the
PTU_Reader example `Example_PTU_PicoHarp.ptu`: tttrlib reads all 722,915 photons
but reconstructs `0 × 256 × 256`. Framed PTU/HT3 files work correctly.

This affects the Python API and, downstream, the ImageJ plugin (which shows "no
image" for such files).

## Goals

- When frame markers are configured but **none are present** in the stream, fall
  back to a single frame spanning the acquisition (using line markers +
  `ImgHdr_PixY` lines per frame).
- Keep framed-file behaviour bit-identical.
- Fix in C++ so all bindings (Python/R/Java + ImageJ) benefit.

## Non-goals

- Bidirectional-scan / sinusoidal-correction handling beyond what already exists.
- New file formats.

## Proposed approach

1. In the CLSM header/marker configuration (already ported to C++ in
   `src/CLSMImage.cpp`), detect the "frame marker set but zero found" case during
   reconstruction.
2. Fall back to framing by line count: `n_frames = ceil(total_lines / n_lines)`
   (from `ImgHdr_PixY`), or a single frame if line count ≤ `n_lines`.
3. Add a Python CLSM test on a single-frame PTU with pinned intensity sum; add the
   file to the reference data set.

## Implementation (done)

Root cause: `create_frames` **does** synthesize one frame from the line markers
(n_frames = 1), but `remove_incomplete_frames` then discarded it because the frame
had fewer lines than the header-declared `n_lines` (256). Fix in
`src/CLSMImage.cpp::remove_incomplete_frames`: when *every* frame is incomplete
(so the image would be empty), **salvage the frame(s) with the most lines and
adopt that count as `n_lines`**. A secondary guard in the constructor handles the
"zero frame edges" case by creating one full-span frame.

**Verified (Java build, `Example_PTU_PicoHarp.ptu`):** the photons are on routing
channel 1 → the image now reconstructs `1 × 652 × 256` with **713,854 photons**
instead of an empty `0 × 256 × 256` stack. The 652 lines are the file's actual
line-marker count (this SymPhoTime export has more scan lines than the nominal
`ImgHdr_PixY = 256`); salvaging all detected lines is the safe "reconstruct
everything" behaviour. Normal multi-frame CLSM is unaffected (regression: the HT3
reference image is still `40 × 256 × 256`, sum 3,364,714).

## Milestones

- **M1 — done.** Reproduced; expected dims/sum pinned (1×652×256, 713854).
- **M2 — done.** C++ salvage fix; verified via the Java binding; no regression.
- **M3 — done.** The file is in the reference set as
  `imaging/pq/PicoHarp_SymPhoTime/Example_PTU_PicoHarp.ptu` (hash + the
  `clsm_single_frame_ptu_filename` key in `test/settings.json`).
  `test/python/clsm/test_CLSM_single_frame_ptu.py` pins 722,915 events →
  `1 × 652 × 256`, sum 713,854 on channel 1, and asserts `n_lines` is adopted
  from the salvaged frame rather than the header's 256. The ImageJ plugin is
  covered by `OpenClsmImageTest::singleFramePtuOpensInsteadOfShowingNoImage`,
  which drives the real `OpenClsmImage` command and pins the same numbers on the
  resulting `Dataset`. Both pass (Python 4/4; ImageJ 4/4, the HT3 reference
  unchanged at 40 × 256 × 256 / 3,364,714).

  Note that the C++ fix moved with the module refactor and now lives in
  `modules/imaging/src/CLSMImage.cpp` (salvage in `remove_incomplete_frames`,
  full-span guard in the constructor).

### Remaining operational step

The PTU came from `junk/PTU_Reader/example_data/` (GPL-3.0 project) and has been
copied into the local `tttr-data` checkout, but it is **not yet on the download
mirror** (`https://www.peulen.xyz/downloads/tttr-data/`) or in the `tttr-data`
GitLab repository. Until it is uploaded and pushed, both new tests skip on a
fresh checkout. Once uploaded, add the path to `required_files` in
`test/settings.json` so `download_test_data.py` fetches it.

## Risks

- Distinguishing "single-frame (no frame marker)" from "misconfigured markers"
  must not silently mask genuine header/config errors — gate on line markers
  being present and dimensions being known.

## References

- `src/CLSMImage.cpp` (marker/header configuration), `include/CLSMImage.h`
- `junk/PTU_Reader/example_data/PicoHarp_SymPhoTime/Example_PTU_PicoHarp.ptu`
- Noted as a limitation in [docs/imagej-plugin.md](../docs/imagej-plugin.md).
