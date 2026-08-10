# PRD-012 — FLIM LABS and BrightEyes-TTM native readers

> **PRD #:** 012 · **Status:** 🟢 Done · **Created:** 2026-08-04 · **Updated:** 2026-08-06 · **Closed:** 2026-08-10 · **Owner:** tpeulen
> **Related:** PRD-006 (round-trip I/O — same read/write dispatch surface), PRD-004 (CLSM marker handling), the module/registry rework in progress on `development`
> **Closed 2026-08-10 with criterion 4 waived, not met.** No FLIM LABS sample
> file is published anywhere and none is expected, so the `STT1` reader ships
> verified against the specification and synthetic fixtures only. Everything
> else is implemented and tested. If a real instrument file ever arrives, run
> `test/python/tttr/test_flimlabs.py` against it before trusting the reader —
> the three measured facts in *Implementation status* below are the parts a
> specification could not tell us, and `STT1` has no equivalent.

## Implementation status (2026-08-06, closed 2026-08-10)

| # | Acceptance criterion | State |
|---|---|---|
| 1 | BrightEyes photon count and TCSPC histogram match the vendor | ✅ 3,898,599 photons, photon-for-photon against `libttp` |
| 2 | Micro times flagged uncalibrated; calibrated decay is smooth | ✅ header tags + code-density calibration; 13 % vs 100 % comb |
| 3 | BrightEyes CLSM reconstruction is 512×512, one frame | ✅ |
| 4 | Real FLIM LABS `STT1` read correctly | ⚪ **waived 2026-08-10 — no data exists.** Synthetic fixtures only; the reader is spec-conformant, not verified against an instrument |
| 5 | `ITT1` reads correctly | ✅ (synthetic) |
| 6 | Container parameters from Python, R and Java without special cases | ✅ one JSON string on `TTTR`, declared as JSON Schema in the registry |
| 7 | `.ttr` never matches content detection | ✅ `detectable = false` |
| 8 | Registry entries + `doc/file-formats.rst` | ✅ |
| 9 | No public API removals or signature changes | ✅ additive only |

Where it landed:

- `modules/io/fl/` — new module, `STT1` + `ITT1`.
- `modules/io/be/` — `ttr_params_from_json`, `TtrCalibration`, `calibrate_ttr`,
  `read_ttr(..., calibration)`.
- `FileFormat::parameters_schema` → `params_schema` in the `file_container`
  registry category; `TTTR::set_container_parameters` and two constructors.
- `doc/formats/flim-labs-stt1.rst`, `doc/formats/brighteyes-ttm.rst`,
  `doc/file-formats.rst`.
- `test/python/tttr/test_flimlabs.py`, `flimlabs_writer.py`, and the parameter
  and calibration sections of `test_brighteyes_ttr.py`.

Three decisions differ from, or sharpen, what is proposed below; each is
explained where it is implemented:

1. **`ITT1` macro times are picoseconds, not laser pulses.** §3 recommends
   laser-pulse units for `STT1`, which is what was done — but applying it to
   `ITT1` would quantise an FCS timestamp to the laser period and throw away
   the resolution the measurement exists for.
2. **The pulse index is the floor of the macro time, not the round.** If the
   file's macro times are absolute rather than pulse counts, rounding moves a
   late photon a whole period. Which reading was true is reported in
   `FlimLabs_MacroTimeResidual_ns`.
3. **The BrightEyes calibration pairs each photon with the *next* laser word,
   and flips the time axis.** Only 27 % of photons in the published sample have
   a laser word in their own record, and the TDC counts backwards. Neither is
   visible from the specification; both were measured. The uncalibrated path is
   unchanged.

## Summary

Add native tttrlib support for two more instrument families:

1. **FLIM LABS** — the `STT1` time-tagger `.bin` (and its sibling `ITT1`), produced by
   the FLIM LABS Spectroscopy / Intensity Tracing / FCS applications.
2. **BrightEyes-TTM** — the `.ttr` raw stream from the open-source Vicidomini-lab
   time-tagging module (SPAD-array FLIM/FCS/ISM).

Both formats are fully specified below. The specifications, vendor reader sources and a
real BrightEyes sample file are already staged in the `tttr-data` repository. What is
*not* settled is where each format's peculiarities are absorbed — and both land squarely
on the generalised reader being designed right now, which is why this is a PRD and not a
patch.

## Problem / motivation

The two formats break tttrlib's current reader assumptions in **opposite** directions,
and neither is a drop-in `TTTRRecordReader`:

| | PTU / HT3 / SPC (today) | FLIM LABS `STT1` | BrightEyes `.ttr` |
|---|---|---|---|
| times in file | integer ticks | **`f64` nanoseconds** | raw TDC codes + step counter |
| calibration needed | no | no (already ns) | **yes — per channel, derived from the data** |
| metadata in file | yes | JSON header | **none at all** |
| record ordering | monotone | **unsorted** | monotone |
| markers | in-band records | in-band (`event` 70/76/80) | enable bits on step words |
| identifiable by content | magic / tags | magic `STT1` | **no magic, no header** |

FLIM LABS is *too processed*: floating-point nanoseconds, so the integer tick that
tttrlib's model requires is a choice the reader has to make, and it is not recoverable
from the file. BrightEyes is *too raw*: the payload is an uncalibrated tapped-delay-line
code, and the parameters needed to interpret it (`n_channels`, `sysclk_MHz`, `laser_MHz`)
are not in the file at all.

The second one is the important one: **the current `TTTR(filename, container_type)`
signature has nowhere to pass reader parameters.** Retrofitting that after the reader
generalisation lands would be a second migration. It should be settled while the API is
still moving.

## Goals

1. Read FLIM LABS `STT1` (17-byte) and `ITT1` (9-byte) time-tagger files into a `TTTR`
   object with correct macro times, micro times, routing channels and F/L/P markers.
2. Read BrightEyes-TTM `.ttr` (firmware v2.0) into a `TTTR` object, with an explicit,
   separately-invoked TDC calibration step.
3. A container-parameter mechanism on the reader API, sufficient for BrightEyes and
   reusable by future formats.
4. Registry entries (`file_container` category) for both, including the fact that `.ttr`
   is **not** content-sniffable.
5. CLSM reconstruction works on both without special-casing in `CLSMImage`.
6. Docs: both formats added to `doc/file-formats.rst` with their lossiness and their
   parameter requirements.

## Non-goals

- **FLIM LABS histogram and phasor formats** (`SP01`, `SPF1`, `IT02`, `FCS1`) and the
  **FLIM Imager JSON** formats (`IMF1`, `IMG1`, `IPF1`, `IPG1`). These are
  already-histogrammed decay curves and phasor coordinates, not photon records. They do
  not belong in `file_container`. (Format details are recorded in `tttr-data` anyway, in
  case a separate importer is ever wanted.)
- Writing either format. Revisit under PRD-006 once reading is trusted; an `STT1` writer
  is nearly free and is noted there as a round-trip aid, with the caveat below.
- BrightEyes-MCS `.h5` and BrightEyes-ISM reconstructed arrays — different products,
  not time-tagging.
- Reproducing `libttp`'s downstream analysis (FCS curves, ISM pixel reassignment).
  tttrlib already has correlators; the job here is to deliver photons.

## Staged material (already done, outside this repo)

Everything needed to implement without repeating the research is in `../tttr-data`:

- `../tttr-data/brighteyes/`
  - `FLIM_80MHz_512x512pixel_120FOV_pixeldwelltime200us.ttr` — **82 MB real sample**,
    from Zenodo [10.5281/zenodo.6782161](https://doi.org/10.5281/zenodo.6782161).
    80 MHz laser, 512×512 px, 200 µs dwell. This is the file the vendor's own TCSPC and
    image-reconstruction notebooks use, so their published output is a reference.
  - `FORMAT.md` — bit-level spec, including measurements taken from this file.
  - `reference_readers/ttp.py`, `ttpCython.pyx` — the vendor `libttp` source.
- `../tttr-data/flimlabs/`
  - `FORMAT.md` — all container layouts.
  - `docs/Spectroscopy-manual-v2.6.1.pdf` (ch. 9 = byte-level spec),
    `docs/Imager-manual-v2.0.1.pdf` (ch. 8), `docs/DAQ-specsheet-v1.1.0.pdf`.
  - `reference_readers/` — the vendor's own MIT-licensed reader scripts, verbatim.

⚠️ `.ttr`, `.bin` and `.pdf` are **not** in `tttr-data`'s `.gitattributes` LFS patterns.
Add them before committing the 82 MB sample. Nothing there has been committed yet.

## Format specification — FLIM LABS

Source: the manuals *and* the vendor reader scripts, which agree. No reverse engineering
was required.

### Common container

| offset | size | type | field |
|---|---|---|---|
| 0 | 4 | ASCII | magic / `file_id` |
| 4 | 4 | `uint32` LE | JSON metadata length in bytes |
| 8 | var | UTF-8 | JSON metadata |
| … | var | fixed-size records | records, no separators, to EOF |

All little-endian. JSON keys observed: `channels` (0-based list), `bin_width_micros`,
`acquisition_time_millis`, `laser_period_ns`, `tau_ns`, `harmonics`.

### Magic bytes

| magic | producer | record | in scope |
|---|---|---|---|
| `STT1` | Spectroscopy Time Tagger | 17 B: `u8` event, `f64` micro (ns), `f64` macro (ns) | **yes** |
| `ITT1` | Intensity Tracing / FCS Time Tagger | 9 B: `u8` event, `f64` time (ns) | **yes** |
| `SP01` | Spectroscopy | `f64` timestamp + 256×`u32` decay per active channel | no |
| `SPF1` | Spectroscopy phasors | `u64` ts (ns), `u32` channel, `f64` g, `f64` s | no |
| `IT02` | Intensity Tracing | `f64` timestamp + `u32` counts per channel | no |
| `FCS1` | FCS | correlation curves | no |

### `STT1` records — the details that matter

`struct` format `"<Bdd"`, 17 bytes, **packed**:

- `u8 event` — `0..N-1` = detector channel, 0-based (the GUI displays `ch{event+1}`).
  Reserved: `70` = `'F'` frame, `76` = `'L'` line, `80` = `'P'` pixel. Note this makes
  channel indices ≥ 70 ambiguous; the hardware has far fewer channels, but a reader
  should not silently mis-tag if it ever sees one.
- `f64 micro_time` — nanoseconds within the laser period, already calibrated.
- `f64 macro_time` — nanoseconds since acquisition start, already calibrated.

Consequences for the reader:

- **Do not `reinterpret_cast` the record.** 17 bytes with an `f64` at offset 1 is
  unaligned and will be padded to 24 by the compiler. Read fields explicitly.
- **Records are not globally time-ordered.** Every vendor script ends with
  `sort_values(by="Macro Time (ns)")`, which means per-channel FIFOs are interleaved out
  of order in the file. The reader must sort, which breaks the streaming assumption other
  containers enjoy — either sort after a full read, or document that this container is
  read fully into memory first.
- `ITT1` is the same reader with a 9-byte `"<Bd"` record and no micro time. Worth doing
  in the same translation unit for a handful of extra lines.

## Format specification — BrightEyes-TTM `.ttr` (firmware v2.0)

Reverse engineered from [`libttp`](https://pypi.org/project/libttp/)
(`ttpCython.pyx::timeProcessNewProtocol`, `ttp.py::readNewProtocolFileToPandas`) and
**verified against the real sample file**.

Hardware: Xilinx Kintex-7 FPGA TDC, ~30 ps resolution, SPAD array (5×5 = 25 or
7×7 = 49 elements) on a scanning microscope.

### Wire format

No file header, no magic bytes, no metadata — a bare stream of little-endian `uint16`
words:

| bits | field | meaning |
|---|---|---|
| 15 | `valid` | 1 = this word carries a real event |
| 14..8 | `ID` | 7-bit word identifier |
| 7..0 | `data` | 8-bit payload (raw TDC code, or step byte) |

`0x7FFF` is a filler/idle word, discarded before parsing (absent from the sample, but
stream-dependent — do not assume).

### ID map

| ID | meaning |
|---|---|
| `0..N-1` | detector channel TDC code (`N` = 25 or 49) |
| `123` | DUMMY |
| `124` | laser sync TDC code (`t_L`) |
| `125` | step byte A (low) + bit 7 = `pixel_enable` |
| `126` | step byte B (mid) + bit 7 = `scan_enable` (line clock) |
| `127` | step byte C (high) + bit 7 = `line_enable` (frame clock) |

**A word with `ID == 127` terminates a record.** Records are variable length — parse by
scanning for `ID == 127`.

### Step counter (coarse time)

```
step = (C & 0x7F) << 14 | (B & 0x7F) << 7 | (A & 0x7F)
```

A **21-bit** counter of `sysclk` ticks (only 7 bits per byte, because bit 7 carries the
enable flags). It wraps constantly — every ~2M ticks — and must be unwrapped into a
monotone cumulative step. This is exactly the overflow logic existing readers already
have. `libttp` calls the unwrapped value `cumulativeStep`.

### TDC codes need calibration — the crux

The 8-bit `data` payload is a **raw tapped-delay-line code, not a linear time**. Bin
widths are non-uniform and channel-specific. `libttp` recovers times by histogramming
codes over one `sysclk` period and integrating the normalised bin widths
(`autoCalibrateTAP`, `binwidth_normalized`, `calculateCalibFromH5`);
`dt_with_fixConstant` is a crude fallback with `kC4 ≈ 45–48`.

So: **a first pass over the data is required before any micro time exists**, and the user
must supply `sysclk_MHz` (240 default) and `laser_MHz`. Neither is in the file.

Photon micro time comes from the calibrated `t_ch − t_L`; macro time from
`cumulativeStep × sysclk_period` corrected by the same codes.

### Verified against the sample file

Measured over the first 40M words by an independent decode (matches `libttp` semantics):

- 8,131,157 records (`ID == 127` words); zero `0x7FFF` filler words.
- IDs present: `0..25`, plus `124,125,126,127`. `123` (DUMMY) absent.
- Every record carries exactly one `124/125/126/127` quartet → minimum record 4 words
  (8 bytes). Length histogram: 4 words (74 %), then 6, 8, 10, 12, 14 — photon words are
  appended in **pairs**.
- Channel words always appear as an aligned pair `(2k, 2k+1)`, exactly one of which has
  `valid == 1`, the other zero-filled. **Filter on the `valid` bit; do not assume the ID
  is the channel.** `libttp` does this filtering downstream, not in the unpacker.
- The laser word (`ID 124`) has `valid == 1` in only ~48 % of records; `125/126/127` are
  always valid.
- `step` increments by a constant 4103 sysclk ticks between consecutive records, wrapping
  at 2²¹ — consistent with a fixed readout cadence.
- `pixel_enable` set in 2.6 % of records, `line_enable` in 0.045 % — consistent with
  512 × 512.

### Minimal decode

```python
import numpy as np
d = np.fromfile(path, dtype='<u2')
d = d[d != 0x7FFF]
valid = (d >> 15) & 1
ID    = (d >> 8) & 0x7F
data  = d & 0xFF
ends  = np.flatnonzero(ID == 127)   # record boundaries
```

## Proposed approach

### 1. Container parameters (do this first — it is the blocker)

BrightEyes needs `n_channels` (25 vs 49), `sysclk_MHz`, `laser_MHz` at construction.
`TTTR(fn, container_type)` has nowhere to put them. Options, in preference order:

- **A JSON/option-map argument on the container constructor**, defaulted so every
  existing call site is unchanged. Fits the registry, which is already JSON-Schema based
  — the schema can declare each container's parameters, and the frontends
  (Python/R/Java) get them for free without per-language plumbing.
- A per-container parameter struct. Type-safe, but multiplies SWIG surface.
- A sidecar-file convention (like the BH `.set` handling). Rejected: invents a file
  format to work around an API gap.

Recommendation: **the option map, declared in the registry schema.** Whatever is chosen
must land with the reader generalisation, not after.

### 2. FLIM LABS reader

Small, no new concepts — a good first consumer of the generalised reader.

1. Validate magic, read `u32` JSON length, parse JSON. `nlohmann/json` is already
   vendored — keep it in the `.cpp`, it was deliberately removed from public headers.
2. Read records in blocks; decode fields explicitly (no packed-struct casting).
3. **Quantisation decision.** Set `number_of_micro_time_channels = 256` and
   `micro_time_resolution = laser_period_ns / 256` — the hardware histograms into 256
   bins, so this is lossless with respect to the instrument. For macro time there is no
   stated tick; two candidates:
   - fix a 1 ps tick and require macro times to fit `uint64` (simple; ~5 h of
     acquisition before overflow, fine in practice), or
   - store macro time in **laser-pulse units**, making `STT1` behave like a T3 file so
     `macro_time × macro_time_resolution` stays exact.

   Recommendation: **laser-pulse units (T3-like)**, since the DAQ is laser-gated and this
   keeps the arithmetic exact. Either way the choice must be written into the header,
   because it is not in the file.
4. Map `event` 70/76/80 to marker events using the existing frame/line/pixel convention
   so `CLSMImage` needs no changes. Channels pass through as `routing_channel`.
5. Sort by macro time after the full read.
6. Same code path handles `ITT1` (9-byte, no micro time).

### 3. BrightEyes reader

The decode is settled. The open decision is **where calibration lives**:

- **A. Reader auto-calibrates.** Two passes: build per-channel code histograms, then emit
  photons. Self-contained and matches `libttp`. But it puts an estimation algorithm
  inside a file reader, and the result depends on the data — the same file read over
  different subranges yields slightly different times.
- **B. Reader decodes, calibration is a separate explicit step.** `micro_time` carries
  raw TDC codes, `macro_time` the unwrapped cumulative step; a named calibration API
  converts. Honest about what the file contains, keeps the reader pure — but a naive user
  gets nonlinear micro times with no warning.
- **C. Read `libttp`'s `-raw.h5` output instead.** Cheapest, but depends on the Python
  toolchain to produce input, so not "native support".

**Recommendation: B, plus an opt-in convenience wrapper that performs A.** The reader
stays a reader; auto-calibration is one clearly-named call away and is visibly an
estimation step. The header must carry a flag marking micro times as **uncalibrated**, so
downstream fitting can refuse rather than silently fit nonlinear codes.

Also:

- Unwrap the 21-bit step counter (reuse existing overflow handling).
- Synthesise pixel/line/frame markers from **edges** on `pixel_enable` / `scan_enable` /
  `line_enable`. There is no frame marker in the stream — frames come from counting
  line-enable edges, as the vendor notebooks do.
- **No magic bytes ⇒ content sniffing is impossible.** `.ttr` must be selected by
  extension or explicitly, and must never enter any "guess the format" path — a bare
  `uint16` stream will happily mis-match another sniffer.

### 4. Tests

- BrightEyes: the 82 MB sample is the fixture. Validate the total photon count and the
  TCSPC histogram shape against the vendor notebook output. Check step unwrapping across
  a wrap boundary, and that `valid == 0` partner words are dropped.
- FLIM LABS: **see the data gap below.** Until a real file exists, tests can only be
  synthetic. A synthetic fixture validates the parser against itself — it cannot catch a
  misreading of the real writer. Mark any such fixture clearly as synthetic.

### 5. Docs

Add both to `doc/file-formats.rst`: layout, required parameters, the quantisation choice
for `STT1`, and an explicit warning that BrightEyes micro times are uncalibrated until
the calibration step runs.

## Data gap — FLIM LABS

**No FLIM LABS sample data is publicly downloadable.** Checked: all 26 repos in the
GitHub org (code and PDF docs only), the `*-docs` release assets, flimlabs.com, Zenodo,
figshare. The `.bin` files come only from the GUI apps driving real hardware, which needs
vendor drivers (obtained by emailing them).

Routes to a real file, in order:

1. Ask FLIM LABS — <info@flimlabs.com>. Their entire software stack is MIT-licensed and
   the manuals document the format openly, so a sample `STT1` export is a reasonable ask.
2. Ask a FLIM LABS customer for an export.
3. Synthesise spec-conformant files, clearly marked synthetic, and treat the reader as
   **unverified** until (1) or (2) arrives.

This does not block implementation — the spec is unambiguous — but it does block calling
the FLIM LABS reader *verified*. Do not mark it Done on synthetic data alone.

## Acceptance criteria

1. `TTTR` reads the BrightEyes sample; photon count and TCSPC histogram match the vendor
   notebook output within counting noise.
2. BrightEyes micro times are flagged uncalibrated on read; after the calibration call,
   the decay histogram is smooth (no tapped-delay-line comb artefacts).
3. BrightEyes CLSM reconstruction of the 512×512 sample produces a 512×512 image with the
   expected frame count.
4. `TTTR` reads a real FLIM LABS `STT1` file — channels, markers and both time axes
   correct, output time-ordered. (Blocked on the data gap; synthetic round-trip until
   then, and the PRD stays In Progress rather than Done.)
5. `ITT1` reads correctly (no micro time).
6. Container parameters are settable from Python, R and Java without per-language special
   cases.
7. `.ttr` never matches any content-based format detection.
8. Registry entries exist for both; `doc/file-formats.rst` updated.
9. No public API removals or signature changes (standing constraint) — new parameters
   default to existing behaviour.

## Sequencing

1. ✅ Specs + sample data staged in `../tttr-data/` (2026-08-04).
2. ✅ Container-parameter mechanism: the option map, declared in the registry schema.
3. ✅ FLIM LABS `STT1` + `ITT1` reader.
4. ⛔ **Request a real FLIM LABS file** — <info@flimlabs.com>. Still the only
   open item; the reader stays unverified until it arrives.
5. ✅ BrightEyes `.ttr` decode, option B.
6. ✅ Calibration wrapper (`auto_calibrate_tdc`) + CLSM marker synthesis.
7. ✅ Docs + registry.

## Risks / notes

- **Verification asymmetry.** BrightEyes has real data and a published reference output,
  so it can be verified properly. FLIM LABS has an unambiguous spec but no data. The
  risk is shipping a confident-looking FLIM LABS reader that has never seen a real file.
- **Calibration is an estimation step, not parsing.** Whatever is decided in §3, the
  uncalibrated state must be visible in the API. Silently returning nonlinear micro times
  that look like normal micro times is the worst outcome available here.
- **`STT1` write support** (PRD-006) would bake the quantisation choice into a file
  format tttrlib emits. Do not add it before the choice is validated against a real file.
- **Channel-index/marker collision** in `STT1` (`event` 70/76/80): benign today, but the
  reader should not silently mis-tag a channel ≥ 70.
- `libttp` is GPL-family — read it for the specification, do not copy code. The
  specification above is written independently and confirmed by measurement.

## References

- FLIM LABS GitHub org: <https://github.com/flim-labs> (readers in
  `*/export_data_scripts/`, MIT)
- Spectroscopy manual ch. 9, Imager manual ch. 8 — staged in `../tttr-data/flimlabs/docs/`
- BrightEyes-TTM: <https://github.com/VicidominiLab/BrightEyes-TTM> ·
  docs <https://brighteyes-ttm.readthedocs.io> · `libttp` <https://pypi.org/project/libttp/>
- BrightEyes-TTM datasets: [10.5281/zenodo.6782161](https://doi.org/10.5281/zenodo.6782161)
  (v2.0 raw) and [10.5281/zenodo.4912656](https://doi.org/10.5281/zenodo.4912656) (v1.0 legacy)
