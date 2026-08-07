Saving a table
==============

A :class:`DataStore` is tttrlib's columnar table: named columns that keep their
own dtype, optional per-column validity masks, dictionary-encoded text, and a
row selection. There are three ways to put one on disk, and they exist for
different reasons.

.. list-table::
   :header-rows: 1
   :widths: 14 29 29 28

   * -
     - native ``.dstore``
     - HDF5
     - CSV
   * - what it is for
     - speed and exact fidelity
     - interoperability
     - the lowest common denominator
   * - use it when
     - tttrlib writes it and tttrlib reads it: caches, checkpoints,
       intermediate results
     - anything else has to read it: h5py, pandas, PyTables, MATLAB
     - a spreadsheet, R, a plotting script, or a collaborator who will not
       install anything
   * - call
     - :func:`save_store` / :func:`load_store`
     - :func:`write_hdf5` / :func:`read_hdf5`
     - :func:`write_csv` / :func:`read_csv`
   * - needs HDF5
     - no
     - yes
     - no
   * - keeps dtypes
     - exactly
     - all but bool
     - no — they are inferred back from the text

The first two round-trip the whole tree: group names, order, nesting, column
order, dtypes, dictionary columns and validity masks. CSV is one flat table and
keeps the values, not the types.

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

That works along the other axis too, and for a store that lives inside
something bigger. The directory records where every column's blob begins and
how wide its elements are, so a **row range** is an offset and a length per
column — which is what a table viewer needs, since paging a million-row burst
table otherwise decodes a million rows to show fifty:

.. code-block:: python

    # rows 500 000-500 050 of two columns, of a store embedded at `base`
    page = tttrlib.load_store_region("run.pto", base, nbytes,
                                     columns=["Tau", "n_photons"],
                                     first_row=500_000, n_rows=50)

Fixed-width columns are exact. A bit-packed column (bool, and every validity
mask) reads only the words its range falls in and is repacked to start at bit
zero. A dictionary-encoded text column reads its codes for the range and the
whole dictionary, which is small by construction. The range applies to every
table in the tree, each clamped to its own length: a group shorter than
``first_row`` comes back empty rather than raising.

A caller with a **container** rather than a raw offset uses
:func:`tttrlib.pto_store`, which is the same read with the region looked up
from an object UID. See :doc:`formats/pto`.

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

CSV
---

The format that loses the most and travels the furthest. One table, one file,
no tree:

.. code-block:: python

    tttrlib.write_csv("run.csv", store)
    tttrlib.write_csv("run.csv", store.group("results"))   # a sub-table
    text = tttrlib.write_csv(None, store)                  # ...or as a string

    back = tttrlib.read_csv("run.csv")

Only the **selected** rows are written when the store is gated, the same as
:func:`write_hdf5` and for the same reason — exporting a subset should not need
an intermediate table. Pass ``selected_only=False`` for all of them.

What a round trip keeps, and what it cannot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Values, column names, and order survive. Types do not, because the reader
infers them from the text and the text does not say how many bytes a number was
held in: an ``int8`` column comes back ``int64``, a ``float32`` comes back
``float64``.

* **A double lands on the same double.** Each one is written as the shortest
  text that reads back as itself — ``0.1`` stays ``0.1`` rather than becoming
  ``0.10000000000000001`` or, worse, a different number.
* **Missing values survive** as an empty field, which :func:`read_csv` takes
  back as missing. One exception: a table of a *single* column writes an empty
  line for a missing value, and an empty line is a blank line to every CSV
  reader there is. Give ``na_rep="NA"`` when a lone column has gaps.
* **Text that looks numeric comes back numeric.** Quoting does not prevent this,
  here or in Arrow. ``read_csv(..., text_columns=["id"])`` is how to say
  otherwise.
* **An empty string and a missing value are the same eight characters of
  nothing.** Set ``na_rep`` to something outside the value set if the difference
  matters.
* **A NaN comes back as missing** rather than as a NaN, the reader's default
  ``na_values`` including it. That is this library's own reading of a NaN, but
  it is not identity.

When identity is the requirement, ``.dstore`` is the format that promises it.

Speed
~~~~~

Rows are cut into blocks, the blocks are formatted in parallel, and the buffers
are written in order. Two things make it quick beyond that: a text column is
dictionary-encoded, so each distinct value is quoted and escaped once rather
than once per row, and doubles are formatted by an integer method rather than by
``snprintf``, which on some platforms costs 280 ns a value and takes the locale
lock.

Two million rows, four columns, against ``pyarrow.csv.write_csv`` on the same
table (higher is better for tttrlib):

.. list-table::
   :header-rows: 1

   * - table
     - arrow
     - tttrlib, one thread
     - tttrlib, default
   * - four int64
     - 0.35 s
     - 0.10 s (3.4×)
     - 0.09 s (3.7×)
   * - four float64, measured (~7 digits)
     - 0.51 s
     - 0.52 s (1.0×)
     - 0.20 s (2.6×)
   * - four float64, full precision
     - 0.78 s
     - 0.87 s (0.9×)
     - 0.42 s (1.9×)
   * - two text, one int, one float
     - 0.22 s
     - 0.21 s (1.0×)
     - 0.09 s (2.5×)

Options
~~~~~~~

``delimiter``, ``quote``, ``header``, ``eol``, ``na_rep``, ``true_string`` /
``false_string``, ``float_precision`` (0 for shortest-round-trip),
``columns`` (a subset, in that order), ``selected_only``, ``threads``, and
``quoting``:

* ``"needed"`` — quote only what RFC 4180 says must be (the default);
* ``"all"`` — quote every value, numbers included;
* ``"none"`` — never, and refuse to write a value that would need it, rather
  than produce a file that reads back as a different table.

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
