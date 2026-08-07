// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_hdf5_table.h"
%}

%include "std_string.i"
%include "std_vector.i"

// The group listing as a real list, for the same reason DataStore's
// group_names is one: a SWIG VectorString has no __eq__, so comparing the
// answer to ['/results', '/meta'] is False however right it is. Must precede
// the %include -- a pythonappend declared after it is silently ignored.
#ifdef SWIGPYTHON
%feature("pythonappend") tttrlib::io::hdf5_table_groups %{
    val = list(val)
%}
%feature("pythonappend") tttrlib::io::read_hdf5_table_columns %{
    val = list(val)
%}
#endif  // SWIGPYTHON

%include "io_hdf5_table.h"

// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block now needs the guard. The JavaScript
// equivalents of these conveniences live in ext/js/pkg/index.js.
#ifdef SWIGPYTHON
%pythoncode %{
def read_hdf5(filename, group="/", with_groups=True, columns=None,
              first_row=0, n_rows=0):
    """Read a columnar HDF5 table, and the tree under it, into a DataStore.

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

    Sub-groups become groups of the store, recursively, so a file written from
    a tree comes back as that tree -- names, order, nesting, column order,
    dtypes and validity masks. A group with nothing table-shaped anywhere
    inside it is skipped rather than turned into an empty node, so a
    Photon-HDF5 or pandas file read at the root gives back whichever parts are
    tables and ignores the rest.

    ``columns`` and the row range are native, not a slice of a whole read: one
    dataset per column is already the layout, so a column not asked for is
    never opened, and a row range becomes a hyperslab HDF5 resolves to the
    chunks it falls in. ``tttrlib.hdf5_bytes_read()`` is how that is checked
    rather than asserted -- take a difference around one call and compare it to
    a difference around another.

    :param group: the group to read; "/" for the file root
    :param with_groups: False to read only that group's own columns
    :param columns: read only these columns, if given. Matched per node, so a
        name in one group and not another leaves that other group with fewer
        columns rather than making the read an error.
    :param first_row: skip this many rows of every table read.
    :param n_rows: how many rows, or 0 for all of them onwards. Each table is
        clamped to its own length, so a group shorter than ``first_row`` comes
        back empty rather than raising.
    """
    store = DataStore()
    # Filled in place: returning a store by value would have SWIG copy the whole
    # table at the moment it is largest.
    if columns is None and not first_row and not n_rows:
        read_hdf5_table_into(store, filename, group, with_groups)
    else:
        names = VectorString(list(columns) if columns is not None else [])
        read_hdf5_table_into(store, filename, group, with_groups, names,
                             first_row, n_rows)
    return store


def write_hdf5(filename, store, group="/", compression=0, mode=None):
    """Write a DataStore as a columnar HDF5 table.

    Only the SELECTED rows are written when the store has a selection, so
    exporting a gated subset needs no intermediate table.

    Writing a group replaces that group and everything under it; under the
    default ``Hdf5WriteMode_Update`` every other group in the file is left
    alone, so a file can be built one group at a time. Writing ``"/"`` replaces
    the file's whole content, the root being a group like any other.

    The write never half-happens -- it goes to a temporary and is moved into
    place -- so a failure leaves what was there before readable and unchanged.
    A file that exists and is not HDF5 is refused rather than replaced; pass
    ``mode=tttrlib.Hdf5WriteMode_Truncate`` if replacing it is what you meant.

    Note that HDF5 never reclaims freed space, so repeatedly replacing one
    group of a multi-group file grows it; ``h5repack`` is the answer. Replacing
    the root does not, because that writes a new file and renames it over.

    :param compression: 0 for none, 1-9 for gzip. None by default: level 4
        costs roughly thirty times the write to save eight percent of the size,
        on files written once and read repeatedly.
    :param mode: ``Hdf5WriteMode_Update`` (default) or ``Hdf5WriteMode_Truncate``
    """
    if mode is None:
        mode = Hdf5WriteMode_Update
    return write_hdf5_table(filename, store, group, int(compression), mode)
%}
#endif  // SWIGPYTHON
