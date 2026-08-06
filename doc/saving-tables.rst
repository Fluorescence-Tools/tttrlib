Saving a table
==============

A :class:`DataStore` is tttrlib's columnar table: named columns that keep their
own dtype, optional per-column validity masks, dictionary-encoded text, and a
row selection. There are two ways to put one on disk, and they exist for
different reasons.

.. list-table::
   :header-rows: 1
   :widths: 18 41 41

   * -
     - native ``.dstore``
     - HDF5
   * - what it is for
     - speed and exact fidelity
     - interoperability
   * - use it when
     - tttrlib writes it and tttrlib reads it: caches, checkpoints,
       intermediate results
     - anything else has to read it: h5py, pandas, PyTables, MATLAB
   * - call
     - :func:`save_store` / :func:`load_store`
     - :func:`write_hdf5` / :func:`read_hdf5`
   * - needs HDF5
     - no
     - yes

Both round-trip the whole tree: group names, order, nesting, column order,
dtypes, dictionary columns and validity masks.

The native file
---------------

.. code-block:: python

    import tttrlib, numpy as np

    store = tttrlib.DataStore("acquisition")
    results = store.add_group("results")
    results.set_n_rows(4096)
    results.add("Tau", np.linspace(0.5, 5.0, 4096))
    meta = store.add_group("meta")
    meta.set_n_rows(1)
    meta.add("source", np.array(["run.ptu"], dtype=object))

    tttrlib.save_store("run.dstore", store)
    back = tttrlib.load_store("run.dstore")

The contract is ``load(save(s)) == s``. Two consequences worth knowing:

* **The row selection is saved, not applied.** :func:`write_hdf5` writes only
  the selected rows, because its job is to export a subset. This one writes
  every row *and* the gate, because its job is to give the store back.
* **Bool survives.** HDF5 has no boolean type, so a bool column written that way
  comes back as ``uint8``. Here it comes back as a bool column.

Reading part of a file costs only that part — the file carries a directory
saying where each column lives:

.. code-block:: python

    tttrlib.load_store("run.dstore", columns=["Tau"])

What it is actually faster at
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Measured on a million rows of mixed numeric types, 21 MB:

.. list-table::
   :header-rows: 1

   * -
     - native
     - HDF5, no compression
     - HDF5, deflate 4
   * - write
     - 0.008 s
     - 0.009 s
     - 2.09 s
   * - read
     - 0.009 s
     - 0.006 s
     - 0.134 s
   * - size
     - 21.0 MB
     - 21.0 MB
     - 18.5 MB

Against **uncompressed** HDF5 it is a wash, and the honest reason is that both
are writing the same bytes through the same page cache — the disk decides, not
the format. Against **compressed** HDF5 it is two orders of magnitude, because
deflate is CPU-bound and this has no deflate. Reading one column of four takes
0.0002 s rather than 0.009 s.

So the reasons to reach for it are: it is much faster than *compressed* HDF5, it
reads a single column without touching the others, it preserves things HDF5
cannot, and it works in a build without HDF5 at all.

The HDF5 file
-------------

One 1-D dataset per column, which is the shape a DataStore already has, so a
table loads with no conversion and no intermediate copy. Two conventions carry
what HDF5 has no place for: a ``columns`` attribute recording the order, and a
``<name>__mask`` dataset for a column's validity mask — the only way an integer
column can have missing values at all. Group order is recorded the same way, in
a ``groups`` attribute.

A file is built one group at a time:

.. code-block:: python

    tttrlib.write_hdf5("run.h5", results, group="/results")
    tttrlib.write_hdf5("run.h5", meta, group="/meta")     # /results survives

    tttrlib.hdf5_table_groups("run.h5")     # ['/results', '/meta']
    tttrlib.hdf5_table_has("run.h5", "/meta")
    tttrlib.hdf5_table_remove("run.h5", "/meta")

Rules worth stating once:

* **Writing a group replaces that group and everything under it.** Writing
  ``/results`` with ``{a, b}`` over one that held ``{a, b, c}`` leaves ``c``
  gone. A table that silently keeps a stale column from a previous run is worse
  than one that lost it, because it looks current.
* Every *other* group is left alone. Writing ``"/"`` replaces the file's whole
  content, the root being a group like any other. Pass
  ``mode=tttrlib.Hdf5WriteMode_Truncate`` to recreate the file instead.
* A file that exists and is **not** HDF5 is refused rather than replaced. Ask
  for ``Truncate`` if replacing it is what you meant.
* A write never half-happens: it goes to a temporary and is moved into place.
* Compression defaults to none. Level 4 costs roughly thirty times the write to
  save eight percent of the size, on files written once and read repeatedly.
* HDF5 never reclaims freed space, so repeatedly replacing one group of a
  multi-group file grows it. ``h5repack`` is the answer. Replacing the root does
  not, because that writes a new file and renames it over.

Two things a DataStore can express and an HDF5 file cannot:

* **A column and a group of the same name.** HDF5 has one link namespace per
  group; the writer refuses, before touching the file, and says which name.
* **A label.** There is nowhere to put it. It round-trips through ``.dstore``.

Reading foreign files
~~~~~~~~~~~~~~~~~~~~~

:func:`read_hdf5` descends into the groups the writer recorded, plus any other
group with a table somewhere inside it. A group with nothing table-shaped under
it is skipped, so a Photon-HDF5 or pandas file read at the root gives back
whichever parts are tables and ignores the rest.

A group that holds **no** table raises, rather than returning an empty store:
zero rows is an answer, zero columns is a refusal, and the two were previously
indistinguishable. :func:`read_hdf5_table_columns` is the cheap predicate that
still answers with an empty list, for a caller deciding whether to open anything
at all.
