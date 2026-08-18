# `io/csv` — CSV Table I/O

Threaded CSV reading and writing for tabular analysis data, both directions
between a delimited file and a [`DataStore`](../../core).

## Contents

- **`include/io_csv.h`, `src/io_csv.cpp`** — the reader: delimiter/quote
  handling, type inference per column (`infer_csv_columns`), a JSON-Lines
  metadata header, and a multi-threaded parse into a `DataStore`.
- **`include/io_csv_writer.h`, `src/io_csv_writer.cpp`** — the writer:
  `write_csv` / `write_csv_string`, column selection, and the same metadata
  header the reader understands.
- **`include/decimal_exact.h`** — exact shortest round-trip decimal formatting
  of doubles (Clinger / `__int128` verification), shared by reader and writer.
  macOS cannot use `std::to_chars(double)`, which is why this is here.

## Dependencies

- Depends on `io/base`, `core` (the `DataStore` a table is read into).
