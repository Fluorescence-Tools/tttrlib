# `io/store` — The Native `.dstore` File

Saves and reloads a [`DataStore`](../../core) (which lives in `core`) as a
`.dstore` file: full fidelity — dtypes, dictionary-encoded strings, bit-packed
masks, attributes — with no dependency on HDF5 or anything else. HDF5 is for
interoperability; this is for keeping what you had.

## Contents

- **`include/io_store.h`** — the file layout and the API (`write_store`,
  `read_store`, `read_store_into`, `store_columns`, `store_groups`,
  `store_has`, region reads).
- **`src/io_store.cpp`** — the writer and the reader, including the partial
  and region reads a large table needs.

## Dependencies

- Depends on `io/base`, `core`.
