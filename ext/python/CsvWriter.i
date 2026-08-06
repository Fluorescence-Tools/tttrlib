// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "io_csv_writer.h"
%}

%include "std_string.i"
%include "std_vector.i"

%include "io_csv_writer.h"

// %pythoncode is a Python-only directive. The other bindings (R, Java) never
// reached it because they wrap a subset; the JavaScript module wraps the whole
// Python surface, so every such block now needs the guard. The JavaScript
// equivalents of these conveniences live in ext/js/pkg/index.js.
#ifdef SWIGPYTHON
%pythoncode %{
# The generated wrapper, kept before the name below is rebound. The Python
# function is a scripting convenience over it, not a second implementation.
_write_csv_native = write_csv

def write_csv(filename, store, delimiter=",", quote='"', header=True,
              quoting="needed", eol="\n", na_rep="", true_string="true",
              false_string="false", float_precision=0, float_decimals=-1,
              keep_decimal_point=False, selected_only=True, columns=None,
              threads=0, block_rows=16384):
    """Write a DataStore as CSV, in parallel.

    :param filename: where to write it, or None to return the text instead
    :param quoting: "needed" (RFC 4180), "all", or "none" -- "none" fails
        rather than write a value that would need quoting
    :param na_rep: what an invalid (missing) value is written as. The default,
        an empty field, is what read_csv() takes back as missing
    :param float_precision: significant digits, or 0 for the shortest text that
        reads back as the same double
    :param float_decimals: digits after the point, as "%.<n>f", or -1 to leave
        the choice to float_precision. Different things: the burst companion
        formats are "%.6f", six DECIMALS, where float_precision=6 would give
        six significant digits and write 1.23457e-05 for 0.000012
    :param keep_decimal_point: write an integral value as "12.0" rather than
        "12", so an all-integral column still reads back as a float from a
        reader that infers types from the text (pandas does)
    :param selected_only: write only the selected rows when the store is gated
    :param columns: which columns, by name and in this order

    Only this store's own columns are written -- CSV is one table and a store
    is a tree, so pass store.group("path") for a sub-table.

    A round trip through read_csv() gives back the values, not the exact types:
    integer width is not recoverable from the text, and text that looks numeric
    reads back numeric unless read_csv(text_columns=...) says otherwise.
    """
    o = CsvWriteOptions()
    o.delimiter = delimiter
    o.quote = quote
    o.has_header = header
    o.quoting = {
        "needed": CsvQuoting_Needed,
        "all": CsvQuoting_All,
        "none": CsvQuoting_Never,
    }[quoting] if isinstance(quoting, str) else quoting
    o.eol = eol
    o.null_string = na_rep
    o.true_string = true_string
    o.false_string = false_string
    o.float_precision = int(float_precision)
    o.float_decimals = int(float_decimals)
    o.keep_decimal_point = keep_decimal_point
    o.selected_only = selected_only
    o.threads = int(threads)
    o.block_rows = int(block_rows)
    if columns is not None:
        o.columns = VectorString(list(columns))
    if filename is None:
        return write_csv_string(store, o)
    if not _write_csv_native(filename, store, o):
        raise IOError("write_csv: could not write %s" % filename)
    return True
%}
#endif  // SWIGPYTHON
