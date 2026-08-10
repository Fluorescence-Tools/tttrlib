# `core` — Core TTTR Data Structures and Operations

This module provides the central `TTTR` photon container class, channel routing, microtime calibration, and event filters.

## Contents

- **`TTTR.h` / `TTTR.cpp`**: Core TTTR container for event records (macro time, micro time, channel routing).
- **`TTTRHeader.h` / `TTTRHeader.cpp`**: Container header structure and tag mappings.
- **`TTTRMask.h` / `TTTRMask.cpp`**: Selection mask over event indices.
- **`Channel.h`**: Detector channel definition and indexing.

## Dependencies

- Depends on `util`, `io/base`.
