# `io/hdf5` — HDF5 I/O (Photon-HDF5 and columnar tables)

Two targets share this directory, because HDF5 does two different jobs here and
one target with both dependencies would be a cycle:

| Target | Sits | What it does |
|---|---|---|
| `tttrlib_io_hdf5` | below `core` | **Photon-HDF5 v0.5** — decoded photon arrays, a format module like `io/pq` |
| `tttrlib_io_hdf5_table` | above `core` | **columnar HDF5** — one dataset per `DataStore` column |

## Contents

- **`include/io_hdf5.h`, `src/io_hdf5.cpp`** — Photon-HDF5 structure, the
  `/photon_data` arrays, setup metadata, reading and writing.
- **`include/io_hdf5_table.h`, `src/io_hdf5_table.cpp`** — a `DataStore` as a
  group of datasets: `write_hdf5_table`, `read_hdf5_table`,
  `read_hdf5_table_into`, `hdf5_table_columns`, `hdf5_table_groups`,
  `hdf5_table_has`, `hdf5_table_remove`.

## Dependencies

- `io_hdf5`: `io/base`, HighFive/HDF5.
- `io_hdf5_table`: `io/base`, `core`, HighFive/HDF5.
