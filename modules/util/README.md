# `util` — Core Plumbing Utilities

The `util` module provides foundational infrastructure used across `tttrlib`: logging, progress reporting, byte-order conversion, bit manipulation, CPU feature detection, and parallel-loop helpers. It does not depend on any higher-level module and contains no numerical kernels — those live in the `math` module.

## Contents

- **`Verbose.h` / `Verbose.cpp`**: Global verbosity level management and debug output formatting.
- **`ProgressMonitor.h` / `ProgressMonitor.cpp`**: Progress callback interfaces for long-running operations.
- **`ProgressTicker.h` / `ProgressTicker.cpp`**: Lightweight ticker for command-line and UI progress updates.
- **`info.h`**: CPU feature detection (AVX / FMA / NEON) and runtime dispatch macros.
- **`BitOps.h`**: Bit manipulation helpers (`ctz64`, `popcount64`) for packed bitsets.
- **`ByteOrder.h`**: Byte-order conversion (big-endian / little-endian swap).
- **`ParallelFor.h`**: Parallel for-loop abstraction (OpenMP wrapper).
- **`bimap.h`**: Bidirectional map container.
- **`string_encoding.h`**: String encoding helpers (native <-> UTF-8), for
  vendor headers written in a code page.
- **`Sha256.h`**: SHA-256 of a buffer or a file (`sha256_file_hex`) -- the
  checksum a `.pto` records for the measurement it was derived from, and what
  `registry("plugin")` reports for a loaded plugin.

## Dependencies

- None (base utility layer).
