# `io/base` — Base File I/O Framework

`io/base` provides the format-agnostic infrastructure for reading and writing TTTR files.

## Contents

- **`TTTRFormat.h` / `IORegistry.cpp`**: Registry and detection of supported TTTR file formats.
- **`FileIO.h` / `FileIO.cpp`**: Low-level platform file access utilities.
- **`TTTRTags.h` / `TTTRTags.cpp`**: Metadata tag structures and container attribute mappings.

## Dependencies

- Depends on `util`.
