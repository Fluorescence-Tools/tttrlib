// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_store.h"
%}

%include "std_string.i"
%include "std_vector.i"

%include "io_store.h"

// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block needs the guard.
#ifdef SWIGPYTHON
%pythoncode %{
def load_store(filename, columns=None):
    """Read a native ``.dstore`` file back into a DataStore.

    The counterpart of :func:`save_store`, and the fast way to put a table on
    disk and get it back: one read per column, straight into the column's own
    buffer, with no chunking, no compression pipeline and no type conversion in
    between. Everything comes back as it went in -- column order, dtypes,
    dictionary-encoded text, validity masks, the row selection, labels, and the
    whole group tree.

    Use HDF5 instead (:func:`read_hdf5`) when the file has to be read by
    something that is not tttrlib.

    :param columns: read only these columns, if given. The file's directory
        says where each one is, so the rest are never touched -- two columns
        out of a four-gigabyte store costs two seeks. The group tree is rebuilt
        whole either way, being the directory.
    :raises RuntimeError: if the file is missing, not a store file, written by
        a newer version, or corrupt. A reader that quietly returned an empty
        table could not be told from one that read an empty table.
    """
    store = DataStore()
    # Filled in place: returning a store by value would have SWIG copy the
    # whole tree at the moment it is largest.
    if columns is None:
        read_store_into(store, filename)
    else:
        read_store_into(store, filename, VectorString(list(columns)))
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
