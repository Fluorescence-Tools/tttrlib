// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_STORE_H
#define TTTRLIB_IO_STORE_H

/*!
 * \file io_store.h
 * \brief The native store file (``.dstore``): a DataStore, saved and reloaded.
 *
 * \section store_why Why not just use HDF5
 *
 * Because the two have different jobs.
 *
 * HDF5 is for interoperability -- h5py, pandas, PyTables, anything that is not
 * tttrlib. It earns its cost when the file has to leave. \ref io_hdf5_table.h is
 * that path and stays that path.
 *
 * This is for the job that happens far more often: save this store, load it
 * back, unchanged. For that, everything HDF5 does is overhead -- chunking, a
 * deflate pipeline, a B-tree link index, an attribute system, a type-conversion
 * layer between what is on disk and what is in memory. None of it is needed to
 * put a column of doubles somewhere and get the same column of doubles back.
 *
 * So this format does the minimum: one read per column, straight into the
 * column's own buffer. The store's buffers use a default-init allocator, so the
 * destination is sized without first being filled with zeros that are about to
 * be overwritten; the write is a single forward pass, and only the 48-byte
 * header is written twice.
 *
 * \section store_speed What that is actually worth
 *
 * Measured on a million rows of mixed numeric types, 21 MB:
 *
 * | | native | HDF5, no compression | HDF5, deflate 4 |
 * |---|---|---|---|
 * | write | 0.008 s | 0.009 s | 2.09 s |
 * | read  | 0.009 s | 0.006 s | 0.134 s |
 * | size  | 21.0 MB | 21.0 MB | 18.5 MB |
 *
 * Against **uncompressed** HDF5 it is a wash, and the honest reason is that
 * both are writing the same bytes through the same page cache -- the disk
 * decides, not the format. Against **compressed** HDF5 it is two orders of
 * magnitude, because deflate is CPU-bound and this has no deflate.
 *
 * Reading one column of four takes 0.0001 s rather than 0.009 s: the directory
 * says where each column is, so the others are never touched. There is no
 * equivalent through the HDF5 path here.
 *
 * And it has no dependency of any kind. A build with ``BUILD_PHOTON_HDF=OFF``
 * cannot persist a DataStore at all today; with this it can.
 *
 * \section store_fidelity Fidelity, not export
 *
 * The contract is `read(write(s))` == `s`: column order, dtypes, dictionary
 * encodings, validity masks, labels, the row selection, and the whole group
 * tree. In particular the writer does **not** drop unselected rows -- the HDF5
 * writer does, because its job is to export a subset, and this one's job is to
 * come back identical. The selection is saved as data, so it comes back too.
 *
 * Bool survives here, which it does not through HDF5: HDF5 has no boolean type,
 * so a bool column comes back as uint8 from that path.
 *
 * \section store_layout The layout
 *
 * \code
 * [48-byte header] [blob] [blob] ... [directory]
 *
 * header:  "TTTRSTOR" | u32 version | u32 flags | u64 dir_offset
 *          | u64 dir_bytes | u64 file_bytes | u32 dir_checksum | u32 reserved
 *
 * node:    label, n_rows, row_mask{n_bits, blob}, n_columns, [column], n_groups,
 *          [name, node]
 * column:  name, type, n, flags, data blob, mask blob, dictionary blob
 * blob:    u64 offset, u64 bytes            -- 8-byte aligned, always
 * \endcode
 *
 * Four choices worth stating, because each is a trade someone will otherwise
 * reverse:
 *
 * - **The directory is last.** The writer streams blobs forward and never seeks
 *   back to patch an offset; the reader does one seek to the tail. That is what
 *   makes a large write a single pass.
 * - **Blobs are 8-byte aligned and stored in native layout**, so a future reader
 *   can map the file and hand a column its memory without copying. That is not
 *   what happens today -- it would need a Column that can borrow a buffer --
 *   but the format is laid out so the change needs no new version.
 * - **Little-endian only.** The flags word records the byte order and a
 *   big-endian reader refuses, clearly, rather than silently mis-reading.
 *   Byte-swapping on the way in would put back exactly the conversion layer
 *   this format exists to avoid.
 * - **The directory is checksummed; the data is not.** The directory is small,
 *   so verifying it is free. Checksumming four gigabytes of payload on every
 *   open would cost more than the format saves. A truncated file is caught by
 *   the byte count in the header.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef SWIG
#include <cstdio>
#endif

#include "DataStore.h"

namespace tttrlib {
namespace io {

/// The magic at the head of a store file, and the customary extension.
extern const char* const kStoreMagic;       ///< "TTTRSTOR"
extern const char* const kStoreExtension;   ///< ".dstore"

/*!
 * \brief Write a store, and everything under it, to `filename`.
 *
 * Goes to a temporary beside the target and is renamed into place, so a failed
 * write never leaves a half file where a good one was.
 *
 * \return false if the file could not be written. The reason goes to stderr.
 */
bool write_store(const std::string& filename, const data::DataStore& store);

/*!
 * \brief Read a store file into `out`, replacing whatever it held.
 *
 * \throws std::runtime_error if the file is missing, not a store file, written
 *         by a newer version, byte-swapped, truncated, or corrupt in its
 *         directory. All of those are stated rather than guessed at: a reader
 *         that silently returns an empty table cannot be told apart from one
 *         that read an empty table.
 */
void read_store_into(data::DataStore& out, const std::string& filename);

/*!
 * \brief \see read_store_into, but only the named columns of each table.
 *
 * The directory says where every column is, so the ones not asked for are never
 * read -- two columns out of a four-gigabyte store costs two seeks. Names that
 * are not in the file are skipped silently; the group tree is rebuilt whole,
 * because it is the directory and costs nothing.
 */
void read_store_into(data::DataStore& out, const std::string& filename,
                     const std::vector<std::string>& columns);

/// \see read_store_into. Returns by value, which copies the whole tree at the
/// peak -- prefer the in-place form from a binding.
data::DataStore read_store(const std::string& filename);

/// Whether this file begins with the store magic. Silent on any input,
/// including a missing file: probing is a normal thing for a caller to do.
bool is_store_file(const std::string& filename);

#ifndef SWIG
/*!
 * \brief Write a store into an already-open file, at its current position.
 *
 * Every offset the directory records is relative to where the store starts, so
 * the result is a self-contained store file that happens to live inside
 * something bigger -- a PTO container, say. Nothing is buffered and no
 * temporary file is used, so a multi-gigabyte table costs its own bytes.
 *
 * Not exposed to the bindings: a FILE* is not something a binding can hold.
 *
 * \return bytes written, or 0 on failure.
 */
std::uint64_t write_store_at(std::FILE* f, const data::DataStore& store);
#endif

/*!
 * \brief Read a store that begins `base` bytes into `filename`.
 *
 * \param bytes the length of the region, or 0 for "to the end of the file".
 * \see write_store_at, and \ref read_store_into for the whole-file case.
 */
void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes);

/*!
 * \brief Both knobs at once: a column subset of a store embedded at `base`.
 *
 * The combination a container makes routine. A store inside a PTO is always the
 * `base`/`bytes` case, so without this a caller reading one could never ask for
 * a subset of its columns -- the single combination that matters was the single
 * one the API omitted.
 *
 * \see pto_read_store, which is the reason this exists.
 */
void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns);

/*!
 * \brief A column subset **and** a row range of an embedded store.
 *
 * A row range is a projection along the other axis from a column subset, and
 * costs the same kind of nothing: the directory records where every column's
 * blob begins and how wide its elements are, so a range is an offset and a
 * length per column. What a table viewer needs -- paging a million-row burst
 * table otherwise decodes a million rows to show fifty.
 *
 * Fixed-width columns are exact. A bit-packed column (bool, and every validity
 * mask) reads only the words its range falls in and is repacked to start at bit
 * zero. A dictionary-encoded text column reads its codes for the range and the
 * whole dictionary, which is small by construction.
 *
 * The range is applied to every table in the tree, each clamped to its own
 * length: a group with fewer rows than `first_row` comes back empty rather than
 * throwing. `n_rows` of 0 means "to the end".
 *
 * The store that comes back reports the range's length as its `n_rows`. It is a
 * window, not the file: writing it back would write the window.
 */
void read_store_into(data::DataStore& out, const std::string& filename,
                     std::uint64_t base, std::uint64_t bytes,
                     const std::vector<std::string>& columns,
                     std::uint64_t first_row, std::uint64_t n_rows);

/// The column names of the root table, in order, without reading any data.
/// Empty for a file that is not one of ours.
std::vector<std::string> store_columns(const std::string& filename,
                                       const std::string& group = "");

/// \see store_columns, for a store that begins `base` bytes into `filename`.
std::vector<std::string> store_columns(const std::string& filename,
                                       std::uint64_t base, std::uint64_t bytes,
                                       const std::string& group = "");

/// Every group path in the file, depth first, without reading any data.
/// Empty for a file that is not one of ours.
std::vector<std::string> store_groups(const std::string& filename);

/// \see store_groups, for a store that begins `base` bytes into `filename`.
std::vector<std::string> store_groups(const std::string& filename,
                                      std::uint64_t base, std::uint64_t bytes);

/*!
 * \brief How many bytes the store reader has moved since the process started.
 *
 * A counter, because the claim "the reader did not touch those bytes" is not
 * one a wall clock can make: a warm page cache measures the cache. Every read
 * the store reader does goes through one place, and this is what that place
 * counts. Test scaffolding, and cheap enough to leave in.
 *
 * Not synchronised, because nothing in this library reads a store from more
 * than one thread. Take a difference around a single call and compare it to a
 * difference around another; the absolute value means nothing.
 */
std::uint64_t store_bytes_read();

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_STORE_H
