---
type: Handover
title: Fiji plugin — where to continue
description: Pre-existing handover note on the SciJava Fiji plugin, kept verbatim; predates this bundle.
tags: [imagej, fiji, java, handover]
status: draft
generated: { by: "human:tpeulen", at: 2026-08-04T00:00:00Z }
---

<!-- Content below is the original `okf` file, unchanged. It was a plain file at
     the repository root; creating the okf/ bundle needed that name, so it moved
     here and gained the frontmatter OKF conformance requires. Nothing else was
     edited. -->

# okf — where to continue on the Fiji plugin

Written 2026-08-04. Everything below is verified unless it says otherwise.

## State

Committed: `4a8ed328 feat(imagej): rebuild the Fiji plugin on SciJava …`
That commit is **Java only** (`ext/java/**`, `test/java` removals, `.gitignore`).

**It does not build green on its own.** The plugin's tests depend on C++ changes
that are still uncommitted in the working tree — see "Uncommitted" below. Commit
those before pushing, or CI will fail on the BH, overflow and FCS tests.

32 files staged in the index are *yours* (HMM rename, CLSMSuperRes, examples,
prototype/esrrf). I left them alone; the commit above was path-scoped.

## Uncommitted and needed by the plugin

C++ (affects Python and R too, not just Fiji):

- `src/CLSMImage.cpp`, `src/CLSMFrame.cpp`, `include/CLSMImage.h`,
  `include/CLSMFrame.h`
  - `get_tag` returns a not-found **sentinel** `{"value": -1, "name": "NONE"}`,
    not null. `read_tag_int/double` accepted it, so every missing tag read as
    -1 and `bidirectional_scan = (-1 != 0) = true` mirrored alternate scan
    lines. Fixed with `tag_is_absent()`. **Any other `get_tag` caller has the
    same trap.**
  - BH SPC imaging reconstructed to 0 frames outside Python: geometry comes from
    the `.set` sidecar but the marker layout from the `BH_SPC_ReadingRoutine`
    tag → `CLSM_BH_SPC130`. Was Python-only, now in `CLSMImageInfo::from_header`.
  - Leica SP5/SP8 marker presets moved to C++ for the same reason.
  - Geometry and markers are now gated separately: one gate meant an explicit
    reading routine got no pixel dimensions at all.
  - `get_intensity_u32` / `get_intensity_from_masks_u32` / `CLSMFrame::
    get_intensity_u32` — counters widened; the 16-bit path wrapped *during*
    accumulation, so the true value never existed.
  - `get_fcs_image` had three defects that made it unusable from every binding:
    null deref on the documented-optional `clsm_other`; a default correlation
    method (`"default"`) the correlator does not know, returning silent zeros;
    and stack-local `TTTR` selections copy-assigned into reused `shared_ptr`s,
    leaving the correlator on freed memory.
- `ext/CMakeLists.txt` — `SWIG_MODULE_DEPENDS` is **not a property UseSWIG
  reads**, so wrappers never regenerated on `.i` or header edits, for all three
  languages. Now `SWIG_MODULE_<target>_EXTRA_DEPS` + `USE_SWIG_DEPENDENCIES`.
- `ext/python/CLSM.i` — 2-D `unsigned int` typemap for `get_intensity_u32`.
- `ext/python/CLSMImage.py` — no longer duplicates the header resolution in
  Python (it had drifted from C++); SP5/SP8/BH presets removed, C++ owns them.
- `.github/workflows/ci.yml`, `doc/imagej-plugin.rst`, `CHANGELOG.md`.

Separate repo, also uncommitted — `~/dev/chisurf`:
- `chisurf/core/fluorescence/mle/setup.py` — split VV/VH on **channel-number
  parity**; must be **alternating by list position**. Also now honours
  `polarization_resolved` (it ignored it), which outranks per-detector
  `ch_p`/`ch_s`.
- `test/fluorescence/test_mle_setup.py` (new; parser tests moved out of the
  deprecated `test_mle_fit2x.py`).

## How to build and test

```bash
cmake -S . -B build-java -G Ninja -DBUILD_PYTHON_INTERFACE=OFF \
  -DBUILD_JAVA_INTERFACE=ON -DBUILD_LIBRARY=OFF -DBUILD_PHOTON_HDF=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-java --target tttrlibJava

# stage the generated proxies + native (both gitignored)
DST=ext/java/imagej/src/main/java/io/github/fluorescencetools/tttrlib
cp build-java/java-pkg/src/main/java/io/github/fluorescencetools/tttrlib/*.java $DST/
cp ext/java/pkg/src/main/java/io/github/fluorescencetools/tttrlib/NativeLoader.java $DST/
mkdir -p ext/java/imagej/src/main/resources/native/darwin-aarch64
cp build-java/java-pkg/native/libtttrlibjni.* \
   ext/java/imagej/src/main/resources/native/darwin-aarch64/libtttrlibjni.dylib

TTTRLIB_DATA=~/dev/tttr-data mvn -f ext/java/imagej test   # 27
TTTRLIB_DATA=~/dev/tttr-data mvn -f ext/java/pkg    test   # 5
```

Fiji here is `/Volumes/SD1TB/Applications/Fiji.app` (x86-64, Java 8 — so the
JAR needs the `darwin-x86-64` native, not arm64; cross-build with
`-DCMAKE_OSX_ARCHITECTURES=x86_64`). Install by copying the JAR to
`Fiji.app/plugins/tttrlib_imagej.jar`. **It is installed there now** — delete it
if you don't want it.

## Traps that already cost time

- **Tests that bypass the dialog.** `CommandService.run(cls, true, map)` skips
  the input harvester entirely, so no dialog defect can surface. That is how an
  empty-string `choices` entry shipped and crashed ICS. `CommandDialogTest`
  guards the static side; the real Swing path needs a display and is exercised
  by a script (see below), not by CI.
- **Stale classes.** Restoring a file with `mv` preserves its mtime, so Maven
  sees the source as older than the `.class` and skips recompiling — you then
  test the old code. `touch` the file, or `mvn clean`.
- **Fiji dies under script-driven GUI runs**: an NPE in its own FlatLaf progress
  bar on the EDT. Not the plugin — the identical sequence is clean headless.
  Drive end-to-end runs with `--headless`.
- Building Swing off the EDT corrupts Fiji's UI. Use `invokeAndWait`.

Useful scripts are in the scratch dir (may be gone):
`dialogs.groovy` builds every command's panel through `SwingInputHarvester`;
`e2e.groovy` runs all 14 commands headless on real data.

## Next

1. Commit the C++/Python/CI/doc changes above, then push — the Java commit is
   incomplete without them.
2. Commit the chisurf change separately.
3. **Fiji update site** is the one feature not automated. The recipe is verified
   (`ci.yml`, "Publish to the Fiji update site"): `db.xml.gz` must be generated
   by Fiji's own updater — hand-rolled checksums give a site Fiji calls corrupt.
   The upload directory must be **absolute**; a relative one resolves against
   `Fiji.app` and silently writes the site into the install.
4. Detector editor still lacks, versus chisurf's page: per-channel LUT/shift
   assignment, the G-factor calculator, the micro-time binning combo.
5. Untested by me, because I cannot click: actually driving a dialog by hand,
   `File > Open` from the GUI, and drag-and-drop.

## Caveat on my own record

I made a number of errors in this session, several of which I only caught after
claiming something was verified. Treat "verified" here as meaning there is a
named test or a recorded observation behind it — where there isn't, I have said
so. The three `test/python/hmm/test_eval.py` failures are your in-progress work
(`HmmEval.posterior_sd_analytic` does not exist yet), not from any of this.
