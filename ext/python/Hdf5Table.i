// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_hdf5_table.h"
%}

%include "std_string.i"
%include "std_vector.i"

%include "io_hdf5_table.h"

// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block now needs the guard. The JavaScript
// equivalents of these conveniences live in ext/js/pkg/index.js.
#ifdef SWIGPYTHON
%pythoncode %{
def read_hdf5(filename, group="/"):
    """Read a columnar HDF5 table into a DataStore.

    One 1-D dataset per column, which is what a DataStore already is -- so the
    file loads with no conversion, no transpose and no intermediate copy. Types
    are kept: a float32 column comes back float32, an integer column stays an
    integer, and a text column becomes a dictionary-encoded string column.

    Two optional conventions the writer here produces and this reads:

    * a ``columns`` attribute on the group, giving the order; without it the
      columns come back in whatever order HDF5 lists them
    * a ``<name>__mask`` dataset, one byte per row and zero where the value is
      missing, which becomes the column's validity mask. A float column can say
      that with a NaN; an integer column has nothing to spare, so this is the
      only way it can.

    :param group: the group holding the columns; "/" for the file root
    """
    store = DataStore()
    # Filled in place: returning a store by value would have SWIG copy the whole
    # table at the moment it is largest.
    read_hdf5_table_into(store, filename, group)
    return store


def write_hdf5(filename, store, group="/", compression=4):
    """Write a DataStore as a columnar HDF5 table.

    Only the SELECTED rows are written when the store has a selection, so
    exporting a gated subset needs no intermediate table.

    :param compression: 0 for none, 1-9 for gzip
    """
    return write_hdf5_table(filename, store, group, int(compression))
%}
#endif  // SWIGPYTHON
