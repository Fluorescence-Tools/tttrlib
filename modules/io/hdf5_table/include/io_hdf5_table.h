// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_HDF5_TABLE_H
#define TTTRLIB_IO_HDF5_TABLE_H

/*!
 * \file io_hdf5_table.h
 * \brief A table in HDF5: one dataset per column, straight into a DataStore.
 *
 * \ref io_hdf5.h is a specification -- Photon-HDF5, with its fixed group names
 * and mandatory metadata. This is the plain thing underneath it: an HDF5 group
 * holding one 1-D dataset per column, which is what a
 * \ref tttrlib::data::DataStore already is. The two are the same shape, so a
 * table written here loads with no conversion, no row-major-to-columnar
 * transpose, and no intermediate copy of the data.
 *
 * The alternative -- reading the table through a DataFrame and handing that over
 * -- costs a full second copy at the moment the table is largest, and loses
 * every column's type on the way: a float32 column comes back float64, and an
 * integer column with one missing value comes back float64 too. Neither is
 * recoverable afterwards, which is why this exists rather than a converter.
 *
 * \section hdf5_table_why_separate Why this is not part of io_hdf5
 *
 * Because `core` reads Photon-HDF5, so `io_hdf5` cannot depend on `core`
 * without a cycle -- and this needs `core`, since a DataStore lives there. The
 * dependency runs core -> io_hdf5 and io_hdf5_table -> core, which is a line
 * rather than a loop.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "DataStore.h"

namespace tttrlib {
namespace io {

/// True when tttrlib was built with HDF5 support (``BUILD_PHOTON_HDF``). The
/// functions below all fail cleanly when it is false.
bool hdf5_table_available();

/*!
 * \brief Read a columnar HDF5 table into `out`.
 *
 * Every 1-D dataset in `group` becomes a column, keeping its stored type:
 * float64, float32, the signed and unsigned integers, and variable-length
 * strings, which become a dictionary-encoded text column.
 *
 * Two conventions, both optional:
 *
 * - a `columns` attribute on the group listing the column names in order, so a
 *   table does not silently come back alphabetised;
 * - a `<name>__mask` dataset of one byte per row, zero where the value is
 *   missing, which becomes the column's validity mask. A NaN in a float column
 *   says the same thing without it; the mask is what lets an INTEGER column
 *   have missing values at all.
 *
 * Datasets that are not 1-D, and any whose length differs from the first
 * column's, are skipped rather than truncated: a ragged table is a mistake
 * somewhere upstream and quietly shortening it hides which.
 *
 * \throws std::runtime_error if the file or group cannot be read.
 */
void read_hdf5_table_into(data::DataStore& out, const std::string& filename,
                          const std::string& group = "/");

/// \see read_hdf5_table_into. Returns by value, which for a large table means a
/// second copy at the peak -- prefer the in-place form from a binding.
data::DataStore read_hdf5_table(const std::string& filename,
                                const std::string& group = "/");

/// The column names, in order, without reading any data.
std::vector<std::string> read_hdf5_table_columns(const std::string& filename,
                                                 const std::string& group = "/");

/// How a write treats a file that is already there.
enum class Hdf5WriteMode {
    Update,     ///< create the file if absent; replace what is written; keep the rest
    Truncate,   ///< recreate the file, so it holds only what is written now
};

/*!
 * \brief Write a store as a columnar HDF5 table.
 *
 * Each column becomes one dataset of its own type, plus a `__mask` dataset for
 * any column that has one, plus the `columns` attribute recording the order.
 * Only the SELECTED rows are written when the store has a selection, so
 * exporting a gated subset needs no intermediate table.
 *
 * \section hdf5_table_replacement What a write replaces
 *
 * **Writing a group replaces that group and everything under it.** Writing
 * `/results` holding `{a, b}` over a `/results` that held `{a, b, c}` leaves `c`
 * gone, and takes any sub-group of `/results` with it. Merging column-wise is a
 * caller's decision, never the writer's: a table that silently keeps a stale
 * column from a previous run is worse than one that lost it, because it looks
 * current.
 *
 * Under \ref Hdf5WriteMode::Update every OTHER group in the file is left alone,
 * so a file is built one group at a time. Writing the root replaces the file's
 * whole content, because the root is a group like any other.
 *
 * A write never half-happens: it goes to a temporary and is moved into place, so
 * a failure leaves what was there before readable and unchanged.
 *
 * \note HDF5 does not reclaim freed space inside a file, so repeatedly replacing
 *       a group in a multi-group file grows it. That is inherent to the format;
 *       `h5repack` is the answer. (Replacing the root does not grow anything --
 *       it writes a new file and renames it over the old one.)
 * \note HDF5 without SWMR is single-writer. Nothing here defends against a
 *       second process writing the same file at the same time.
 *
 * \param compression 0 for none, 1-9 for gzip. Chunked at 1 MB per chunk, which
 *        is what makes a column readable back without inflating the whole file.
 *        Defaults to none: level 4 costs roughly thirty times the write to save
 *        eight percent of the size, on files written once and read repeatedly.
 * \param mode \ref Hdf5WriteMode. `Update` on a file that exists and is not
 *        HDF5 returns false and leaves it untouched -- ask for `Truncate` to
 *        replace a file of unknown provenance.
 */
bool write_hdf5_table(const std::string& filename, const data::DataStore& store,
                      const std::string& group = "/", int compression = 0,
                      Hdf5WriteMode mode = Hdf5WriteMode::Update);

/*!
 * \brief What tables does this file hold?
 *
 * Full paths, in file order, `"/"` for a table at the root. A group holds a
 * table when it has at least one 1-D dataset and every 1-D dataset in it is the
 * same length; sub-groups are ignored, so a root table with a `/meta` group
 * beside it lists as both.
 *
 * Answers rather than complains: a file that is not HDF5, one written as a
 * data frame, or one with no table anywhere gives an empty vector and prints
 * nothing. Probing a foreign file is a normal thing for a caller to do.
 */
std::vector<std::string> hdf5_table_groups(const std::string& filename);

/// Whether one particular group holds a table. Agrees with
/// \ref hdf5_table_groups for every group, and is silent on any input.
bool hdf5_table_has(const std::string& filename, const std::string& group = "/");

/*!
 * \brief Drop a group and everything under it.
 *
 * False when the file is not ours, the group is not there, or it could not be
 * removed. At the root -- which cannot be unlinked -- this removes the table
 * the root holds and leaves its sub-groups, each of which is its own table.
 *
 * \note HDF5 does not shrink when something is deleted; the space stays in the
 *       file. `h5repack` reclaims it.
 */
bool hdf5_table_remove(const std::string& filename, const std::string& group);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_HDF5_TABLE_H
