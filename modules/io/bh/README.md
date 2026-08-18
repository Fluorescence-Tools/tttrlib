# `io/bh` — Becker & Hickl Format I/O

This module implements reader support for Becker & Hickl SPC file formats (SPC-130, SPC-600 24/32-bit, SPC-QC) and `.set` configuration files.

## Contents

- **`io_bh.h` / `io_bh.cpp`**: SPC record stream reading and routing.
- **`io_bh_set.h` / `io_bh_set.cpp`**: `.set` parameter parsing.

## Dependencies

- Depends on `io/base`, `util`, nlohmann/json.
