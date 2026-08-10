# `io/csv` — CSV Table I/O

Fast, zero-copy CSV reading and writing routines for tabular analysis data.

## Contents

- **`io_csv_reader.h` / `io_csv_reader.cpp`**: Multi-threaded CSV parser reading into `DataStore`.
- **`io_csv_writer.h` / `io_csv_writer.cpp`**: Fast CSV writer supporting JSON Lines metadata headers.

## Dependencies

- Depends on `io/store`, `util`.
