// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_CSV_H
#define TTTRLIB_IO_CSV_H
// Validation: A/B-TESTED 2026-08-17 -- values and inferred types equal pyarrow.csv on a tricky and a
//   large file (quoted delimiters, doubled quotes, missing values, booleans; threads and block size
//   change nothing). test/python/test_csv_reader.py.
//   Register: okf/testing/algorithm-validation.md

/*!
 * \file io_csv.h
 * \brief A threaded CSV reader that fills a \ref tttrlib::data::DataStore directly.
 *
 * Written after studying Apache Arrow's CSV reader (cpp/src/arrow/csv), whose
 * design this follows: split the file into blocks on record boundaries, parse
 * them in parallel, and infer each column's type by a loosening ladder. The
 * implementation is independent -- Arrow is Apache-2.0 and tttrlib is
 * BSD-3-Clause, and copying source would encumber the license of a library that
 * does not otherwise carry that obligation.
 *
 * \section csv_direct Why it builds the store itself
 *
 * The point is to have no intermediate representation. A reader that produced
 * arrays which were then copied into columns would touch every value twice and
 * hold both copies at the peak -- and for the data this is for, where a table
 * can be most of the memory in the process, the peak is what decides whether it
 * opens at all. Each block parses straight into the typed buffers that become
 * the columns.
 *
 * \section csv_scope What it does not do
 *
 * Quoting per RFC 4180, `\n` and `\r\n`, and UTF-8/ASCII. Not: other encodings,
 * or escape characters outside doubled quotes. A file needing those should go
 * through pandas -- the goal is to be fast on the files that actually get
 * opened, not to be a general CSV library, and the difference is what keeps
 * this finishable and correct.
 *
 * Comment lines were on that list and are now half off it: \ref
 * CsvOptions::comment skips a LEADING and a TRAILING block, which is what
 * `write_csv`'s metadata block is and what a file annotated by hand usually
 * has. A comment between two data rows is still not supported, and the reason
 * is the same one that put comments on the list: recognising them anywhere
 * costs a test per record in a reader whose whole point is how few of those it
 * does.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "DataStore.h"

namespace tttrlib {
namespace io {

/// Which C++ type a column was inferred to hold.
enum class CsvColumnKind { Null, Integer, Boolean, Real, Text };

struct CsvOptions {
    char delimiter = ',';
    char quote = '"';
    bool has_header = true;

    /*!
     * Whether a quoted value may contain a raw newline.
     *
     * False -- the default -- lets block boundaries be found by looking for
     * newlines, which is a memchr and is most of why this is fast. True forces
     * a sequential quote-aware scan of the file to find boundaries, which is
     * correct for such files and materially slower. The reader detects a file
     * with no quote characters at all and takes the fast path regardless.
     */
    bool newlines_in_values = false;

    /// Store inferred floating-point columns as float32. Half the memory, and
    /// more than the precision a plot axis can show.
    bool use_float32 = false;

    /// 0 to decide, 1 to force serial, or an explicit count.
    int threads = 0;

    /// Target bytes per parallel block. Boundaries move to the next record end.
    std::size_t block_size = 16u << 20;

    /// Values read as "missing": the row is marked invalid in that column.
    std::vector<std::string> na_values = {"", "NA", "N/A", "na", "null", "NULL",
                                          "NaN", "nan", "None", "-"};

    /// Explicit types by column name, bypassing inference for those columns.
    std::vector<std::string> force_text_columns = {};

    /*!
     * \brief Lines beginning with this are not data. `\0` (the default) is off.
     *
     * Recognised as a **leading block and a trailing block**, not line by line
     * anywhere in the file. That is what \ref write_csv writes with
     * `metadata`, and it is what keeps the parser's hot loop as it was: the
     * alternative is a test per record in a reader whose whole point is how few
     * of those it does. A comment between two data rows is not supported and
     * will be parsed as a row.
     *
     * A line that parses as one of this library's metadata objects restores
     * what it carries -- the store's label, a column's description. One that
     * does not is simply skipped, so a file commented by hand still reads.
     */
    char comment = '\0';
};

/*!
 * \brief Read `filename` into a DataStore.
 *
 * Column names come from the header row when `has_header`, and are `f0`, `f1`,
 * ... otherwise. Types are inferred per column; a column that is not numeric or
 * boolean becomes a dictionary-encoded text column, which is both the
 * compression and what makes it histogrammable.
 *
 * \throws std::runtime_error if the file cannot be read or is not rectangular.
 */
data::DataStore read_csv(const std::string& filename, const CsvOptions& options = CsvOptions());

/*!
 * \brief Read into an existing store.
 *
 * For language bindings. Returning by value makes SWIG copy-construct the
 * result, and for a table that is most of the memory in the process that is a
 * second copy of the whole thing at the moment of peak usage -- exactly what
 * building the store directly was meant to avoid.
 */
void read_csv_into(data::DataStore& out, const std::string& filename,
                   const CsvOptions& options = CsvOptions());

/*!
 * \brief What the reader would infer, without building the store.
 *
 * For a caller that wants to show the user a type per column before committing
 * to loading a large file.
 */
std::vector<CsvColumnKind> infer_csv_columns(const std::string& filename,
                                             const CsvOptions& options = CsvOptions());

/// Column names, from the header row or generated. Cheap: reads one line.
std::vector<std::string> read_csv_column_names(const std::string& filename,
                                               const CsvOptions& options = CsvOptions());

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_CSV_H
