// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_store.h"
%}

%include "std_string.i"
%include "std_vector.i"

// io_store.h and io_pto.h are spelled in <cstdint> types throughout, and only
// the Python backend resolves `std::uint64_t` on its own. Everywhere else it
// stays an unknown type and every offset, size and UID comes out as an opaque
// SWIGTYPE proxy -- so a Java caller could open a container and then do nothing
// with what it told them. `stdint.i` would fix it and cannot be included here:
// see the note at the top of misc_types.i about int64_t on glibc. This says the
// one thing that is needed instead.
//
// Not for R, which gives std::uint64_t its own typemaps in ext/r/tttrlib.i:
// `unsigned long long` there goes through as.integer(), which is 32-bit and
// silently NA above 2^31, and a %apply here would overwrite the fix with the
// very thing it corrects.
#ifndef SWIGR
%apply unsigned long long { std::uint64_t };
#endif

// A SWIG VectorString is not a list and has no __eq__, so
// store_groups(f) == ['a', 'b'] would be False however right the answer was --
// every caller ends up writing list(...) round it. group_names/group_paths on
// DataStore already get this treatment; these are the same shape and should
// not be the exception. Must precede the %include: a pythonappend declared
// after the header it applies to is silently ignored.
#ifdef SWIGPYTHON
%feature("pythonappend") tttrlib::io::store_groups %{
    val = list(val)
%}
%feature("pythonappend") tttrlib::io::store_columns %{
    val = list(val)
%}
#endif  // SWIGPYTHON

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
