// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_csv.h"
%}

%include "std_string.i"
%include "std_vector.i"

%include "io_csv.h"

// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block now needs the guard. The JavaScript
// equivalents of these conveniences live in ext/js/pkg/index.js.
#ifdef SWIGPYTHON
%pythoncode %{
def read_csv(filename, delimiter=",", quote='"', has_header=True,
             use_float32=False, threads=0, block_size=16 << 20,
             newlines_in_values=False, na_values=None, text_columns=None):
    """Read a CSV into a DataStore, in parallel, without an intermediate copy.

    :param use_float32: store inferred real columns as float32 -- half the
        memory, and more precision than a plot axis can show
    :param threads: 0 to decide, 1 to force serial, or an explicit count
    :param newlines_in_values: a quoted value may contain a raw newline. Costs a
        sequential scan of the file to find block boundaries; leave it off
        unless the data needs it
    :param text_columns: names to keep as text regardless of what they look like

    Types are inferred per column. Anything not numeric or boolean becomes a
    dictionary-encoded text column, which is both the compression and what makes
    it directly histogrammable.
    """
    o = CsvOptions()
    o.delimiter = delimiter
    o.quote = quote
    o.has_header = has_header
    o.use_float32 = use_float32
    o.threads = int(threads)
    o.block_size = int(block_size)
    o.newlines_in_values = newlines_in_values
    if na_values is not None:
        o.na_values = VectorString(list(na_values))
    if text_columns is not None:
        o.force_text_columns = VectorString(list(text_columns))
    store = DataStore()
    # Filled in place: returning a store by value would have SWIG copy the whole
    # table at the moment it is largest.
    read_csv_into(store, filename, o)
    return store
%}
#endif  // SWIGPYTHON
