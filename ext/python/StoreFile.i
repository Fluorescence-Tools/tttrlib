// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_store.h"
%}

// The .dstore functions are ptolib's, wrapped once by Ptolib.i; io_store.h
// only re-exports them under tttrlib::io. The Python helpers below are
// tttrlib's and stay.
%include "Ptolib.i"

%include "io_store.h"

// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block needs the guard.
#ifdef SWIGPYTHON
%pythoncode %{
def load_store(filename, columns=None, group=None, first_row=0, n_rows=0):
    """Read a native ``.dstore`` file back into a DataStore.

    The counterpart of :func:`save_store`, and the fast way to put a table on
    disk and get it back: one read per column, straight into the column's own
    buffer, with no chunking, no compression pipeline and no type conversion in
    between. Everything comes back as it went in -- column order, dtypes,
    dictionary-encoded text, validity masks, the row selection, labels, and the
    whole group tree.

    Use HDF5 instead (:func:`read_hdf5`) when the file has to be read by
    something that is not tttrlib.

    Three independent knobs, and none of them reads what it did not ask for.
    ``tttrlib.store_bytes_read()`` is how that is checked rather than asserted:
    take a difference around one call and compare it to another.

    :param columns: read only these columns, if given. The file's directory
        says where each one is, so the rest are never touched -- two columns
        out of a four-gigabyte store costs two seeks. The group tree is rebuilt
        whole either way, being the directory.
    :param group: read this group as the root, if given. The tree BELOW it
        comes back with it; the tree above it does not, which is what makes the
        result a store in its own right rather than a view -- it writes
        straight back out as a file whose root is the group asked for.
        Reaching it costs a scan of the directory and no payload at all.
        Use :func:`store_has` to ask rather than to read.
    :param first_row: skip this many rows of every table read.
    :param n_rows: how many rows, or 0 for all of them onwards. Each table is
        clamped to its own length, so a group shorter than ``first_row`` comes
        back empty rather than raising.
    :raises RuntimeError: if the file is missing, not a store file, written by
        a newer version, corrupt, or has no such group. A reader that quietly
        returned an empty table could not be told from one that read an empty
        table.
    """
    store = DataStore()
    # Filled in place: returning a store by value would have SWIG copy the
    # whole tree at the moment it is largest.
    if columns is None and group is None and not first_row and not n_rows:
        read_store_into(store, filename)
    elif group is None and not first_row and not n_rows:
        read_store_into(store, filename, VectorString(list(columns)))
    else:
        names = VectorString(list(columns) if columns is not None else [])
        read_store_into(store, filename, 0, 0, names, first_row, n_rows,
                        group if group is not None else "")
    return store


def load_store_region(filename, base, nbytes, columns=None,
                      first_row=0, n_rows=0):
    """Read a store that begins ``base`` bytes into ``filename``.

    A store written into the middle of something bigger -- a PTO container --
    is still a self-contained store, and this is how it is read where it lies.
    :func:`tttrlib.pto_store` is the same thing with the region looked up from
    an object UID, and is what a caller with a container should use.

    :param nbytes: the length of the region, or 0 for "to the end of the file".
    :param columns: read only these columns, if given.
    :param first_row: skip this many rows of every table in the tree.
    :param n_rows: how many rows to read, or 0 for all of them onwards.
    """
    store = DataStore()
    names = VectorString(list(columns) if columns is not None else [])
    if first_row or n_rows:
        read_store_into(store, filename, base, nbytes, names, first_row, n_rows)
    elif columns is not None:
        read_store_into(store, filename, base, nbytes, names)
    else:
        read_store_into(store, filename, base, nbytes)
    return store


def save_store(filename, store):
    """Write a DataStore to a native ``.dstore`` file. \\see load_store.

    Unlike :func:`write_hdf5`, this does NOT drop unselected rows: its contract
    is that the store comes back identical, so the selection is saved rather
    than applied. The write goes to a temporary and is renamed into place, so a
    failure never leaves half a file where a good one was.
    """
    return write_store(filename, store)
%}
#endif  // SWIGPYTHON
