# `io/table` — One Vocabulary for a Table in a File

Three formats can hold a [`DataStore`](../../core) — the native `.dstore`
([`io/store`](../store)), HDF5 ([`io/hdf5`](../hdf5)) and a store embedded in a
PTO container ([`io/pto`](../pto)) — and each was reached by a different verb
with a different spelling of the same argument. This module is the one
vocabulary over all of them: `read_table`, `write_table`, `table_columns`,
`table_groups`, `table_has`, `table_format_of`, `table_format_name`.

## Contents

- **`include/io_table.h`** — the vocabulary and the format enumeration.
- **`src/io_table.cpp`** — dispatch to the three backends, format detection by
  content and extension, and the `table_format` registry category.

## Dependencies

- Depends on `io/store`, `io/hdf5` (`io_hdf5_table`), `io/csv`, `io/pto`,
  `io/base`, `core`. It sits above them all: it is the union of what they do.
