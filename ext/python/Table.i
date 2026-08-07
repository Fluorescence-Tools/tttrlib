// SPDX-License-Identifier: BSD-3-Clause
//
// One vocabulary for a table in a file, whatever the file is. Must follow
// StoreFile.i, Hdf5Table.i, Csv.i and Pto.i: it dispatches to all four, and its
// header names their types.
%module tttrlib
%{
#include "io_table.h"
%}

%include "std_string.i"
%include "std_vector.i"

%include "io_table.h"

// The Python face. Keyword arguments and a store filled in place, matching
// load_store() and read_hdf5() -- this is the same read said one way, so it has
// to look like the ones it replaces or it is a third spelling rather than a
// single one.
//
// %pythoncode is Python-only; the other bindings call read_table_into and the
// four queries directly, which is the whole surface minus the sugar.
#ifdef SWIGPYTHON
%pythoncode %{
def read_table(spec, group="", columns=None, first_row=0, n_rows=0):
    """Read a table, or part of one, whatever format it is in.

    The format comes from the file, exactly as ``TTTR(filename)`` already
    infers a container -- so swapping ``.dstore`` for ``.h5`` is swapping a
    filename rather than rewriting the call::

        read_table("run.dstore",     group="results", columns=["Tau"])
        read_table("run.h5",         group="results", columns=["Tau"])
        read_table("run.pto|bursts", group="results", columns=["Tau"])

    :param spec: ``path``, or ``path|object`` for a store inside a PTO. A PTO
        holds many objects, each of which is a tree, and the pipe is where that
        extra addressing axis goes.
    :param group: read this group as the root; "" for the whole file. Leading
        and trailing separators are optional, and HDF5's ``/results`` and the
        native format's ``results`` both work on either -- the one place the
        two formats' own listings disagree, normalised so a path taken from one
        can be handed to the other.
    :param columns: read only these, if given. Matched per node.
    :param first_row: skip this many rows of every table read.
    :param n_rows: how many, or 0 for all of them on.
    :raises RuntimeError: if the spec names no readable table, saying what was
        looked for. It never returns an empty store to mean "could not read",
        because that cannot be told from an empty table.

    CSV takes neither ``group`` nor a row range -- it is one flat table with no
    tree -- and asking for either raises rather than being ignored.
    """
    store = DataStore()
    # Filled in place: returning a store by value would have SWIG copy the
    # whole tree at the moment it is largest.
    read_table_into(store, spec, group,
                    VectorString(list(columns) if columns is not None else []),
                    first_row, n_rows)
    return store
%}
#endif  // SWIGPYTHON
