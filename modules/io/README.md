# `io` — reading and writing files

Everything under this directory is *a thing that reads or writes a file*. Nothing
here knows what a photon stream is used for: no correlation, no imaging, no
fitting. That is the whole boundary, and it is enforced by the build — a module
only gets the include directories of itself and its declared `DEPENDS`, so a
`.cpp` here that reaches for `CLSMImage.h` fails to compile rather than quietly
creating a cycle.

## Layout

| Directory | Target | What it is |
|---|---|---|
| [`base/`](base) | `tttrlib_io` | Format-agnostic half: the registry, the header/tag model, raw file access |
| [`pq/`](pq) | `tttrlib_io_pq` | PicoQuant — PTU, HT3, PT3, PT2, HHT3, HHT2 |
| [`bh/`](bh) | `tttrlib_io_bh` | Becker & Hickl — SPC-130, SPC-600 (24/32-bit), SPC-QC, `.set` sidecar |
| [`cz/`](cz) | `tttrlib_io_cz` | Carl Zeiss — ConfoCor3 `.raw` |
| [`sm/`](sm) | `tttrlib_io_sm` | Single-molecule `.sm` |
| [`ps/`](ps) | `tttrlib_io_ps` | Photonscore LINCam `.photons` (D7) |
| [`be/`](be) | `tttrlib_io_be` | BrightEyes-TTM `.ttr` |
| [`hdf5/`](hdf5) | `tttrlib_io_hdf5` | Photon-HDF5 v0.5 — decoded arrays, not a record encoding |
| [`image/`](image) | `tttrlib_io_image` | TIFF 2D/3D arrays — file I/O, but not a TTTR container |

`hdf5` is where HighFive and `<hdf5.h>` stop. Before it existed,
`TTTRHeader.h` forward-declared `HighFive::Group` for a single private method,
so imaging, correlation and fitting all needed an HDF5 toolchain on their
include path to compile a header that has nothing to do with HDF5.
`BUILD_PHOTON_HDF` stays project-wide — core reads it to decide whether to offer
the container — but it is carried by `tttrlib::build_config` as a capability
flag, and only this module links `tttrlib::highfive`.

Every vendor module depends on `base` and on nothing else. They do not depend on
each other, and none of them depends on `core`: the dependency arrow runs
`core → io_* → io`, so a format can be added, changed or dropped without
recompiling the photon-stream data model. `base` must therefore be declared
first; the rest are independent and their order in
[`CMakeLists.txt`](CMakeLists.txt) is arbitrary.

The names are deliberately short. A vendor module is referred to constantly — in
`DEPENDS` lines, in link errors, in the module list printed at configure time —
and `io_pq` reads better in all three places than `io_picoquant`.

## What `base` provides

* **`TTTRFormat.h`** — `FileFormat` (name, container type, extensions, record
  types, `can_read`/`can_write`, `stable`, a `sniff` function) and `IORegistry`,
  which answers "what is this file?" by extension first and by content second.
  This is the *only* place that knows the set of formats; adding one is a table
  entry plus a module directory, not a switch statement edited in five files.
* **`TTTRHeaderTypes.h`**, **`TTTRTags.h`** — the header/tag model shared across
  containers (PTU's tag list is the general case; the others are projections of
  it).
* **`TTTRRecordTypes.h`** — the record-type enumeration.
* **`FileIO.h`** — raw reads that do not care what the bytes mean.

## Adding a format

1. `modules/io/<xx>/` with `include/io_<xx>.h`, `src/io_<xx>.cpp`, and a
   `CMakeLists.txt` declaring `NAME io_<xx>` and `DEPENDS io`.
2. `add_subdirectory(<xx>)` in [`CMakeLists.txt`](CMakeLists.txt).
3. A `FileFormat` entry in `base/src/IORegistry.cpp`, with a `sniff` function if
   the format can be recognised from its content.
4. `DEPENDS ... io_<xx>` in `modules/core/CMakeLists.txt`, so the dispatch in
   `TTTR.cpp` can reach it.
5. A page under [`doc/formats/`](../../doc/formats) and a row in the
   compatibility matrix in the top-level `README.md`.

The configure step fails if a source under `modules/` is claimed by two modules
or by none, so a file added here without a home is a build error and not a
surprise later.

## Read-only formats are allowed

`can_write` is a property of the format, not an aspiration. BrightEyes `.ttr` is
read-only because it has no header to write anything into and the instrument
parameters a reader needs are not in the file, so there is nothing a writer could
round-trip. The write-matrix test skips such containers by asking the registry
rather than by carrying its own list.
