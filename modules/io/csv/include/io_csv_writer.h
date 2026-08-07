// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_CSV_WRITER_H
#define TTTRLIB_IO_CSV_WRITER_H

/*!
 * \file io_csv_writer.h
 * \brief The other direction: a \ref tttrlib::data::DataStore written as CSV.
 *
 * There are now three ways out of a store, and they are not competing:
 * \ref io_store.h is fidelity (read(write(s)) == s, and nothing else is
 * promised), \ref io_hdf5_table.h is the scientific stack (typed, still
 * columnar, h5py and pandas read it), and this is the lowest common
 * denominator -- the thing a spreadsheet, a plotting script, R, and a
 * collaborator who will not install anything can all open. A format that loses
 * types and doubles the bytes is the right answer surprisingly often, because
 * the alternative is that the numbers do not leave at all.
 *
 * \section csvw_arrow What came from Arrow, and what did not
 *
 * Arrow's writer (cpp/src/arrow/csv/writer.cc) works in two phases: each column
 * is first cast to strings and asked for its rendered length per row, the row
 * lengths are prefix-summed into byte offsets, and only then does each column
 * populate a single preallocated buffer, advancing every row's offset past the
 * value and the delimiter it just wrote. The payoff is that the inner loop is
 * one tight typed pass per column with no per-cell type dispatch at all, and
 * the output buffer is allocated exactly once, at exactly the right size.
 *
 * Two of those three reasons do not apply here.
 *
 * The exact-size pass exists because Arrow hands a finished buffer to a sink it
 * does not own. This writer owns its sink -- a `FILE*` -- so it can format
 * straight into a growable block buffer and flush it, and skipping the sizing
 * pass means every rendered byte is touched once rather than twice. What is
 * kept is the shape of the dispatch: the type switch is resolved once per
 * column per block into a cell writer, not re-entered per cell.
 *
 * The one thing done here that Arrow cannot: a text column is
 * dictionary-encoded, so its distinct values are quoted and escaped ONCE per
 * block-set and every row is then a memcpy of a prepared string. Arrow's writer
 * sees a decoded string array and escapes per row. On the columns this library
 * actually holds -- a few labels repeated over millions of rows -- that is most
 * of the work in the column, removed.
 *
 * \section csvw_fidelity What a round trip does and does not preserve
 *
 * `read_csv(write_csv(s))` gives back the values, the column names, and the
 * order. It does not give back the types exactly: the reader infers, and
 * inference is a property of the text, not of what was written. In particular
 *
 * - integer width is not preserved -- an Int8 column comes back Int64, since
 *   `3` says nothing about how many bytes it was held in;
 * - a text column whose values all look numeric comes back numeric. Quoting
 *   does not prevent this, in this reader or in Arrow's; `CsvOptions::
 *   force_text_columns` is the way to say otherwise on the way back in;
 * - a missing value and an empty string are the same eight characters of
 *   nothing, so a text column containing "" comes back masked. Set
 *   \ref CsvWriteOptions::null_string to something outside the value set if
 *   that distinction matters;
 * - a table of ONE column writes an empty LINE for a missing value, and an
 *   empty line is a blank line to every CSV reader there is -- so that row is
 *   not read back at all. Same fix: give it a `null_string`;
 * - an integral value in a real column is written "12", not "12.0", so a
 *   column whose values all happen to be integral stops looking like a real
 *   column to a reader that infers types from the text.
 *   \ref CsvWriteOptions::keep_decimal_point is the answer, and it matters
 *   most where a file is merged column-wise by position with a file some other
 *   program wrote;
 * - a NaN is written as the text "nan", not as an empty field, and comes back
 *   as a missing value because the reader's default `na_values` includes it.
 *   The asymmetry is deliberate. In a store a NaN is a VALUE and the mask is
 *   how "missing" is said, so the writer has nothing to translate; a data
 *   frame has only the NaN and writes an empty field for it. A caller
 *   converting frame-shaped data should mask the NaNs at this boundary and
 *   only at this boundary -- an HDF5 or `.dstore` write keeps them, where they
 *   are values again. Infinities are never touched by such a pass: a diverging
 *   fit is a result, and blanking it hides one;
 * - the validity mask survives (as `null_string`), the row selection does not
 *   -- unselected rows are not written at all.
 *
 * Every one of those is inherent to CSV rather than to this implementation.
 * When identity is the requirement, `.dstore` is the format that promises it.
 *
 * \section csvw_speed Threads
 *
 * Rows are cut into blocks, blocks are formatted in parallel into their own
 * buffers, and the buffers are written in order. A block is a range of row
 * INDICES, not of selected rows, so a gated store needs no index list built
 * first -- the mask is read where the rows are.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "DataStore.h"

namespace tttrlib {
namespace io {

/// When a value is wrapped in quotes. \see CsvWriteOptions::quoting
enum class CsvQuoting {
    Needed,   ///< only values holding a delimiter, a quote, or a newline
    All,      ///< every value, including numbers
    // Never rather than None: SWIG has to escape a Python keyword, and
    // CsvQuoting__None with its two underscores is not an API to ship.
    Never,    ///< fails rather than write a value that needs quoting
};

struct CsvWriteOptions {
    char delimiter = ',';
    char quote = '"';

    /// Write the column names as the first row.
    bool has_header = true;

    CsvQuoting quoting = CsvQuoting::Needed;

    /// "\n", or "\r\n" for the strict RFC 4180 reading. Both read back.
    std::string eol = "\n";

    /// What an invalid (missing) value is written as. Empty is what the
    /// reader's default `na_values` takes back as missing.
    std::string null_string = "";

    /*!
     * \brief What a float `NaN` is written as.
     *
     * A different question from \ref null_string, and the store keeps the two
     * apart on purpose: a masked cell says *not measured*, a `NaN` says *the
     * number is not a number* -- a fit that diverged, a ratio with no
     * denominator. CSV has one blank field for both, so the writer is the place
     * a caller has to be able to choose.
     *
     * `"nan"` is the default and is what this always wrote, so no existing file
     * changes. `""` is what a data frame's writer produces, and is what a caller
     * feeding a program written against one needs.
     *
     * Without it the only way to get the empty field was to mask every
     * non-finite value before writing -- and the mask is part of the table, so
     * doing that in place means *writing a table changes it*. The alternative
     * was a whole-table copy per write, to express one formatting choice.
     *
     * ±infinity is NOT covered: it is a value with an exact text that reads
     * back as itself, and a frame writes it as `inf` too.
     */
    std::string nan_string = "nan";

    // A Bool column written as 1/0 would be inferred as an integer on the way
    // back in, which is why these are words and not digits.
    std::string true_string = "true";
    std::string false_string = "false";

    /*!
     * Significant digits for real columns, or 0 for the shortest text that
     * reads back as the same double.
     *
     * Shortest-round-trip is the default because it is both smaller and more
     * correct than a fixed precision: `%.17g` writes 0.1 as
     * 0.10000000000000001, and `%.6g` writes a different number.
     */
    int float_precision = 0;

    /*!
     * Digits after the decimal point, as printf's `%.<n>f`, or -1 to leave the
     * choice to \ref float_precision.
     *
     * The two count different things, and a caller writing a file whose layout
     * another program fixed almost always means this one. The burst companion
     * formats are specified as `%.6f` -- six DECIMALS. `float_precision = 6`
     * gives six SIGNIFICANT digits, so a small value comes out `1.23457e-05`
     * where the format calls for `0.000012`, and a large one loses digits
     * before the point.
     *
     * Fixed-point does not round-trip and is not meant to: it is for matching
     * a layout, not for preserving a value.
     */
    int float_decimals = -1;

    /*!
     * Write an integral value in a real column with a trailing ".0".
     *
     * Off by default, which matches Arrow and every shortest-form writer: 12.0
     * and 12 are the same double and the shorter spelling is the one asked for.
     * Turn it on when the reader on the other side infers types from the text
     * -- pandas does -- because an all-integral float column otherwise arrives
     * as an integer column. That is a dtype lost in transit, not a value.
     *
     * Ignored when \ref float_decimals is set, where the number of decimals is
     * exactly what was asked for.
     */
    bool keep_decimal_point = false;

    /// Write only the selected rows, when the store is gated. False writes the
    /// whole table and ignores the selection.
    bool selected_only = true;

    /// Which columns, by name and in this order. Empty for all of them, in
    /// store order. A name that is not a column is an error, not a skip.
    std::vector<std::string> columns = {};

    /// 0 to decide, 1 to force serial, or an explicit count.
    int threads = 0;

    /// Rows per parallel block. Also the flush granularity.
    std::size_t block_rows = 16384;
};

/*!
 * \brief Write `store` to `filename` as CSV.
 *
 * The store's own columns only: CSV is one table, and a store is a tree. Pass
 * `store.group("path")` to write a sub-table.
 *
 * Goes to a temporary beside the target and is renamed into place, so a failed
 * write never leaves a half file where a good one was.
 *
 * \return false if the file could not be written, if a name in
 *         \ref CsvWriteOptions::columns is not a column, if the columns
 *         disagree about how many rows there are, or if
 *         \ref CsvQuoting::Never was asked for and a value needs quoting. The
 *         reason goes to stderr.
 */
bool write_csv(const std::string& filename, const data::DataStore& store,
               const CsvWriteOptions& options = CsvWriteOptions());

/*!
 * \brief \see write_csv, into a string rather than a file.
 *
 * For a caller that is about to put the text somewhere that is not a
 * filesystem -- a socket, a clipboard, a test. The whole table is in memory
 * twice at the moment it returns, which is exactly why the file path does not
 * go through here.
 *
 * \throws std::runtime_error where \ref write_csv would return false.
 */
std::string write_csv_string(const data::DataStore& store,
                             const CsvWriteOptions& options = CsvWriteOptions());

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_CSV_WRITER_H
