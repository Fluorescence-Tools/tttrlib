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
    store.add_group("results", {"Tau": np.linspace(0.5, 5.0, 4096)})
    store.add_group("meta", {"source": np.array(["run.ptu"], dtype=object)})

    tttrlib.save_store("run.dstore", store)
    back = tttrlib.load_store("run.dstore")

    back / "results" / "Tau"                 # the column, reached by path
    np.mean(back / "results" / "Tau")

The row count of a group follows from its first column, as ``add`` already does
it. Spelling it out — ``add_group`` then a loop then ``set_n_rows`` — still
works and is what the one-call form does underneath.

.. _dstore_paths:

Reaching into the tree
~~~~~~~~~~~~~~~~~~~~~~

A store is a tree, so getting at a column used to be a four-link chain. ``/``
composes a path the way :mod:`pathlib` does — it looks nothing up until the
path is used, so a path can be built before the group exists, held, and
resolved later:

.. code-block:: python

    p = back / "results" / "Tau"     # nothing looked up yet
    p.numpy()                        # resolved here
    np.mean(p)                       # and here
    p.dtype, len(p), p.parent, p.name

    back["results/Tau"]              # the same key space, resolved at once
    back["a/b/x"]
    back["results/"]                 # a group -> DataStore

    print(back.tree())               # what is in this file?
    back.paths()                     # every column, by path
    for path, group in back.walk():  # depth first, parent before child
        ...
    back.rglob("Tau")                # every Tau anywhere, as paths

Writing works the same way, creating the groups it needs:

.. code-block:: python

    back["meta/instrument"] = np.array(["MicroTime 200"], dtype=object)

**A key without a separator is unchanged.** ``store["Tau"]`` is a column and
always was, and a name that is only a group still raises — reaching a group by
name is ``store / "results"`` or ``store.group("results")``. A column is also
looked up *before* the tree, so a column genuinely called ``Sg/Sr`` still wins
over the path ``Sg/Sr``.

Histograms take paths too, on one rule: **a histogram fills from one table.**
Two groups have different row counts and no row correspondence, so axes from
two of them are not something that can be filled, and asking raises rather than
answering.

.. code-block:: python

    (back / "results" / "Tau").histogram(bins=100)      # one column, one line
    back.histogram("results/Tau", "results/E", bins=64)
    back.profile("results/x", "results/y", sample="results/tau")

    back.histogram("results/Tau", "meta/source")        # ValueError, and says why

The contract is ``load(save(s)) == s``. Two consequences worth knowing:

* **The row selection is saved, not applied.** :func:`write_hdf5` writes only
  the selected rows, because its job is to export a subset. This one writes
  every row *and* the gate, because its job is to give the store back.
* **Bool survives.** HDF5 has no boolean type, so a bool column written that way
  comes back as ``uint8``. Here it comes back as a bool column.

.. _dstore_columns:

How several columns sit in the file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Nothing is interleaved and nothing is row-major. **Each column's values are one
contiguous run, in the column's own dtype**, and a directory at the tail of the
file says where each run begins:

.. code-block:: text

    [48-byte header] [blob] [blob] ... [directory]

    node:    label, n_rows, row_mask{n_bits, blob}, n_columns, [column],
             n_groups, [name, node]
    column:  name, type, n, flags, data blob, mask blob, dictionary blob
    blob:    u64 offset, u64 bytes          -- 8-byte aligned, always

A column is up to **three** blobs: its data, its validity mask, and — for text,
which is dictionary-encoded — its labels. So reading a subset costs one seek to
the tail for the directory, then one read per wanted blob; the rest of the file
is never touched.

Measured on a 1M-row burst table of four columns, 28.0 MB, counting bytes
rather than seconds (a wall clock on a warm page cache measures the cache):

.. list-table::
   :header-rows: 1

   * - read
     - bytes moved
   * - the whole table
     - 28 000 000
   * - ``columns=["Tau", "E"]``
     - 16 000 000
   * - ``columns=["Tau"]``
     - 8 000 000

Exactly the columns asked for, and nothing else. ``tttrlib.store_bytes_read()``
is the counter — take a difference around one call and compare it to a
difference around another.

**HDF5 is the same shape**: a group holding one 1-D dataset per column, a
sub-group per child. That is why a table written there loads with no conversion
and no transpose.

Every partial read works on both, and each is native — a shorter loop over the
directory, and a hyperslab. **Emulation was rejected**: reading a whole file and
slicing gives the right answer at the wrong cost, so a caller who swapped an
extension to get a subset read would have got the opposite.

.. list-table::
   :header-rows: 1

   * -
     - ``.dstore``
     - HDF5
     - PTO
   * - read one group
     - ``load_store(f, group=)``
     - ``read_hdf5(f, group)``
     - —
   * - read a column subset
     - ``load_store(f, columns=)``
     - ``read_hdf5(f, columns=)``
     - ``pto_store(f, uid, columns=)``
   * - read a row range
     - ``load_store(f, first_row=, n_rows=)``
     - ``read_hdf5(f, first_row=, n_rows=)``
     - ``pto_store(f, uid, first_row=, n_rows=)``
   * - ask whether a group is there
     - ``store_has(f, group)``
     - ``hdf5_table_has(f, group)``
     - —
   * - list the groups
     - ``store_groups(f)`` → ``results``
     - ``hdf5_table_groups(f)`` → ``/results``
     - ``pto_store_groups(f, uid)``
   * - bytes moved, for checking the above
     - ``store_bytes_read()``
     - ``hdf5_bytes_read()``
     - ``store_bytes_read()``
   * - remove a group
     - —
     - ``hdf5_table_remove(f, group)``
     - ``PtoFile`` methods

Measured on the same tree written both ways — four root columns of 20 000
rows, one group of one column, one small nested group:

.. list-table::
   :header-rows: 1

   * - read
     - ``.dstore``
     - HDF5
   * - the whole file
     - 800 400
     - 800 400
   * - ``columns=["Tau"]``
     - 320 000
     - 320 000
   * - ``first_row=100, n_rows=50``
     - 2 000
     - 2 000
   * - one group
     - 160 000
     - 160 000

The same request moves the same bytes through either format. That is the
property that makes them interchangeable, and it is asserted with the counters
above rather than with a wall clock — on a warm page cache a clock measures the
cache.

Note the leading slash: the string identifying a group depends on which file it
came out of, which is the value a caller passes straight back in. Both readers
accept it either way, leading and trailing separators being optional.

Two things about reading one group, because neither is guessable:

* The tree **below** the group comes back with it; the tree **above** does not.
  That is what makes the result a store in its own right rather than a view —
  it writes straight back out as a file whose root is the group asked for.
* ``columns=`` is matched **per node**. A name that is in one group and not
  another leaves that other group with fewer columns rather than making the
  read an error.

Two things about ``columns=`` that are worth knowing before you rely on them,
because neither is guessable:

* **It matches by name in EVERY table of the tree, not just the root.** A tree
  whose root, ``g1`` and ``a/b`` each hold a ``Tau`` gives you all three — each
  with its own row count, because they are different tables that happen to share
  a column name. Pass a group as well if you meant one of them.
* **The tree comes back whole either way.** The structure *is* the directory, so
  filtering columns costs nothing structural: a table with no matching column
  comes back empty but keeps its ``n_rows``, and so is still distinguishable
  from a table that genuinely has no rows.

.. code-block:: python

    part = tttrlib.load_store("run.dstore", columns=["Tau"])
    [(p, n.n_rows(), n.column_names()) for p, n in part.walk()]
    # [('',    4, ['Tau']),
    #  ('g1',  9, ['Tau']),
    #  ('a',   0, []),          <- kept, with its row count
    #  ('a/b', 2, ['Tau'])]

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

.. _column_descriptions:

What a column knows about itself
--------------------------------

A column carries a description alongside its values: a JSON object, one per
column, that both the native format and HDF5 store and give back. ``units`` is
what it was built for — a burst duration in milliseconds and a lifetime in
nanoseconds otherwise say so only in their column names, when whoever wrote
them remembered.

.. code-block:: python

    store["Tau"].set_units("nanoseconds")
    store["Tau"].set_attribute("of", "run.ptu")

    back = tttrlib.load_store("run.dstore")
    back["Tau"].units()          # 'nanoseconds'
    back["Tau"].attribute("of")  # 'run.ptu'
    back["Tau"].metadata()       # the whole object, as JSON text

There are two setters and the difference matters. ``set_attribute`` stores a
**string**, whatever it looks like; ``set_attribute_json`` takes JSON text and
stores the **value** it denotes:

.. code-block:: python

    c.set_attribute("na", "[[2,4]]")       # the seven characters
    c.set_attribute_json("na", "[[2,4]]")  # a list of ranges
    c.set_attribute_json("n", "9007199254740993")

    c.attribute("na")        # '[[2,4]]'   -- unquoted, so units needs no parse
    c.attribute_json("na")   # '[[2,4]]'   -- exact, so the round trip is exact
    c.attribute_json("of")   # '"run.ptu"' -- a string keeps its quotes

Types survive the file. The description is stored as **msgpack**, not as JSON
text, so an integer written as an integer reads back as one — the ``n`` above
is exact, which it could not be if it had gone through a JSON number and come
back a double. That is storage, not interface: ``metadata()`` still takes and
returns JSON text in every binding.

Both formats keep it, and keep it identically. In HDF5 it rides as an attribute
on the column's own dataset, next to the dictionary and for the same reason —
everything needed to read a column is on the column, so a reader that takes one
column out of a wide table gets its description too. Nothing acquires a
description by being written: a column with none costs nothing.

Rows that were never measured
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A column can say a row holds no measurement, and it can do so **without
touching the dtype** — which is the thing a data frame cannot do, because it
has to widen an ``int64`` to ``float64`` to hold a ``NaN`` and the type is not
recoverable afterwards.

``valid(i)`` is the question, and there are two ways the answer is stored. A
scattered pattern — what :meth:`mask_non_finite` produces — becomes a bit mask.
A contiguous run becomes a range in the description:

.. code-block:: python

    r = tttrlib.concat([tttrlib.load_store(f) for f in files])

    r["n"].has_mask()        # False -- no bit mask was allocated
    r["n"].has_missing()     # True  -- the rows are missing all the same
    r["n"].valid(1_000_001)  # False
    r["n"].mask_numpy()      # the bool array, whichever way it was stored

    for run in r["n"].na_ranges():
        print(run.first, run.last, run.why)
        # 1000000 2000000 absent in 'm002.hdf5'

The gap a merge leaves is one whole file's contribution, so it is one run by
construction, and a bit per row would be a million copies of one fact. Twenty
files of a million rows cost about a kilobyte of description instead of 2.5 MB
of bits.

**The better reason is that a range can say why and a bit cannot.** "These rows
are not measured because that file did not have this column" is information;
a zero bit is the absence of it — so a caller merging twenty files can report
which ones contributed what, rather than keeping the file list beside the
table.

Three things follow, and none is guessable:

* ``has_mask()`` asks about **storage**, not about meaning: it is ``False`` for
  a column whose gaps are ranges. ``has_missing()`` is almost always the one
  meant.
* **HDF5 writes both** — the ranges in the description and the mask as a
  dataset. That format exists to hand a table to something that is not
  tttrlib, and such a reader cannot be assumed to know what an ``na`` range is.
  CSV, which has nowhere to put either, writes empty cells: the answer survives
  and the reason does not.
* :meth:`take` and :meth:`compact` **keep the validity and drop the ranges**.
  A gather reorders rows, so a range naming the source's rows says nothing true
  about the result's; the rest of the description is untouched.

A range can also be written by hand, in either spelling:

.. code-block:: python

    c.add_na_range(2, 4, "detector off")
    c.set_attribute_json("na", "[[2,4]]")      # the short form, no reason

Note ``set_attribute_json`` and not ``set_attribute``: the latter would store
the seven characters ``[[2,4]]`` as a string, and the column would have no
missing rows as a result.

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
