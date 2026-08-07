// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file io_table.h
 * \brief One vocabulary for a table in a file, whatever the file is.
 *
 * \section table_why Why this exists
 *
 * Three formats can hold a `DataStore` -- the native `.dstore`, HDF5, and a
 * store embedded in a PTO container -- and each was reached by a different
 * verb with a different spelling of the same argument:
 *
 * ```
 * load_store(f, columns=)        read_hdf5(f, group)      pto_store(f, uid, columns=)
 * store_groups(f)   -> results   hdf5_table_groups(f) -> /results
 * ```
 *
 * So a caller who wanted to swap one format for another rewrote their call
 * sites, and a caller reading a folder of mixed files carried a branch per
 * format. The formats were interchangeable in what they could hold and not in
 * how they were asked.
 *
 * These five functions are that one way to ask. They add no capability: every
 * one is a call to a reader that already exists, chosen from the file. What
 * they add is that the choosing happens once, here, instead of at every call
 * site in four languages.
 *
 * \section table_five Five, not six
 *
 * There is no `table_remove`. Removing a group is `read_table` ->
 * `remove_group` -> `write_table`, which is the rule this whole surface rests
 * on: **a change happens in memory, and a write is what puts it in a file.**
 * A remove verb would be the only call that changed a file without the caller
 * holding what changed, and it is the one that cannot be given the same cost
 * in all three formats -- `.dstore` would rewrite, HDF5 would unlink, PTO
 * would free an extent. Three queries and two transfers are the whole surface.
 *
 * \section table_spec The spec
 *
 * `spec` is `path` or `path|object`, the same form the TTTR readers already
 * take for `run.pto|m001.ptu`. A PTO has one addressing axis more than the
 * other two -- it holds many objects, each of which is a tree -- and the pipe
 * is where that axis goes. Everything after it is identical:
 *
 * ```
 * read_table("run.dstore",     "results", {"Tau"})
 * read_table("run.h5",         "results", {"Tau"})
 * read_table("run.pto|bursts", "results", {"Tau"})
 * ```
 */
#ifndef TTTRLIB_IO_TABLE_H
#define TTTRLIB_IO_TABLE_H

#include <cstdint>
#include <string>
#include <vector>

#include "DataStore.h"

namespace tttrlib {
namespace io {

/// Which reader a spec resolves to. \see table_format_of
enum class TableFormat {
    Unknown = 0,
    Store,      ///< the native `.dstore`
    Hdf5,       ///< a columnar HDF5 table
    Csv,        ///< one flat table, no tree
    Pto,        ///< a store embedded in a PTO container
};

/*!
 * \brief Which format a spec names, from the file rather than from its name.
 *
 * Content first and the extension only as a tie-break, because an extension is
 * a claim and the bytes are the fact. `.dstore` and PTO have magic; HDF5 has
 * its signature; CSV has none and never will, so it is the one that is decided
 * by extension -- which is worth knowing before relying on it.
 *
 * `Unknown` for a file that is none of them, including one that does not
 * exist. The callers below turn that into an error naming the spec; this
 * function does not, because a caller may be asking rather than opening.
 */
TableFormat table_format_of(const std::string& spec);

/// The reader's name for a format, for a message a human has to act on.
const char* table_format_name(TableFormat f);

/*!
 * \brief Read a table, or part of one, whatever format it is in.
 *
 * \param spec  `path`, or `path|object` for a store inside a PTO.
 * \param group read this group as the root; empty for the whole file. Leading
 *        and trailing separators are optional, and HDF5's `/results` and the
 *        native format's `results` both work on either -- the one place the two
 *        formats' own listings disagree, normalised here so a path taken from
 *        one can be handed to the other.
 * \param columns read only these; empty for all of them. Matched per node, so
 *        a name in one group and not another leaves that group with fewer
 *        columns rather than making the read an error.
 * \param first_row skip this many rows of every table read.
 * \param n_rows how many, or 0 for all of them on. Each table is clamped to its
 *        own length, so a group shorter than `first_row` comes back empty.
 *
 * \throws std::runtime_error if the spec names no readable table, naming the
 *         spec and what was looked for. It never returns an empty store to mean
 *         "could not read", because that cannot be told from an empty table.
 *
 * CSV takes neither `group` nor a row range -- it is one flat table with no
 * tree -- and asking for either throws rather than being ignored. A knob that
 * silently does nothing is worse than one that is not there.
 */
void read_table_into(data::DataStore& out, const std::string& spec,
                     const std::string& group = "",
                     const std::vector<std::string>& columns = {},
                     std::uint64_t first_row = 0, std::uint64_t n_rows = 0);

/// \see read_table_into. Returns by value, which for a large table means a
/// second copy at the peak -- prefer the in-place form from a binding.
data::DataStore read_table(const std::string& spec,
                           const std::string& group = "",
                           const std::vector<std::string>& columns = {},
                           std::uint64_t first_row = 0,
                           std::uint64_t n_rows = 0);

/*!
 * \brief Write a store as a table, in the format the spec names.
 *
 * The format comes from the **extension** here and not from the content, since
 * the file need not exist yet. That is the one asymmetry with \ref read_table
 * and it is inherent: there is nothing to sniff.
 *
 * \param group write into this group; empty for the root.
 * \param replace_file true to recreate the file, so it holds only what is
 *        written now. False -- the default -- keeps whatever else is in it and
 *        replaces the group being written, which is how a file is built one
 *        group at a time.
 *
 * `.dstore` and CSV hold one tree and one table respectively, so a partial
 * write is not something they can do: writing either replaces the file, and
 * `group` on them throws rather than being ignored.
 */
bool write_table(const std::string& spec, const data::DataStore& store,
                 const std::string& group = "",
                 bool replace_file = false);

/// Every group path in the table, depth first. Empty for a spec that names no
/// table -- a question, unlike \ref read_table, so it is silent on any input.
std::vector<std::string> table_groups(const std::string& spec);

/// The column names of one group, in order, without reading any data.
std::vector<std::string> table_columns(const std::string& spec,
                                       const std::string& group = "");

/// Whether the spec names a readable table, and that group in it. Silent on any
/// input, including files that are not tables: probing is a normal thing to do.
bool table_has(const std::string& spec, const std::string& group = "");

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_TABLE_H
