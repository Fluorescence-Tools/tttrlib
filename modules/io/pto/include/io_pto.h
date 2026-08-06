// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_PTO_H
#define TTTRLIB_IO_PTO_H

/*!
 * \file io_pto.h
 * \brief PTO -- the PhoTon cOntainer. An EBML document, DocType "pto".
 *
 * The container binds a set of opaque payloads together in one file, gives each
 * one a UID, and lets typed metadata be attached to any of them. It does not
 * know what a photon stream is. See ``doc/formats/pto.rst`` for the normative
 * specification; this header is the API, not the format.
 *
 * Three things it is for, which no other format here does together:
 *
 * - **One file per measurement.** The photon stream, the bursts found in it, a
 *   spectrum from the same sample and the original instrument file, each with
 *   an identity that survives being rewritten.
 * - **In-place update.** Recomputing a burst table beside an 8 GiB photon
 *   stream rewrites the burst table, not the file.
 * - **Provenance a caller can express.** A tag targets a UID and may have a UID
 *   as its value. What that edge MEANS is the application's business; see
 *   \ref PtoTag.
 *
 * Payload bytes are opaque here. \ref io_store.h is the encoding to reach for
 * when the payload is a table -- see \ref pto_add_store.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "DataStore.h"

namespace tttrlib {
namespace io {

/// What a \ref PtoTag carries. Covers PicoQuant's twelve header types, plus the
/// two object references that make provenance expressible.
enum class PtoType {
    Empty = 0,  ///< the tag's presence is the information
    UInt,       ///< PtoTag::u
    Int,        ///< PtoTag::i
    Float,      ///< PtoTag::d
    Date,       ///< PtoTag::i, nanoseconds since 2001-01-01 UTC (the EBML epoch)
    Text,       ///< PtoTag::text
    Bytes,      ///< PtoTag::bytes
    UID,        ///< PtoTag::u -- a reference to an object in this file
    UIDs,       ///< PtoTag::uids
    Floats,     ///< PtoTag::floats
    Ints,       ///< PtoTag::ints
};

/*!
 * \brief One piece of typed metadata, and what it is about.
 *
 * \par What PTO guarantees, and what it does not
 * A tag names its subject with \ref target, and may name another object as its
 * value with \ref PtoType::UID. That is a labelled edge, and it is the whole of
 * what the container provides. PTO does not define `derived_from`, does not
 * check that a referenced UID exists, and does not detect cycles: applications
 * disagree about all three, and a container that picks a winner is wrong for
 * everyone else.
 */
struct PtoTag {
    std::string name;               ///< opaque to the container; `pto.` is reserved
    PtoType type = PtoType::Empty;
    std::uint64_t target = 0;       ///< the object described; 0 means the file

    /*!
     * Position within an array, or -1 for a scalar.
     *
     * A PicoQuant header repeats a tag name once per element rather than
     * storing a list, and this is what lets such a header survive verbatim.
     */
    int index = -1;

    /// The type code this tag had in the format it came from, so a PTU header
    /// can be written back bit-exact. Zero when it came from nowhere.
    std::uint32_t source_type = 0;

    std::uint64_t u = 0;
    long long i = 0;
    double d = 0.0;
    std::string text;
    std::vector<unsigned char> bytes;
    std::vector<double> floats;
    std::vector<long long> ints;
    std::vector<std::uint64_t> uids;
};

/// A note somebody wrote down. Prose for people, as against \ref PtoTag, which
/// is values for programs.
struct PtoAnnotation {
    std::uint64_t target = 0;       ///< 0 means the file
    std::uint64_t first_row = 0;
    std::uint64_t last_row = 0;     ///< one past the end; both zero means all of it
    std::string text;
    std::string author;
    long long when = 0;             ///< nanoseconds since 2001-01-01 UTC, 0 if unset
};

/// A run of free space inside the file. \see PtoFile::free_extents.
struct PtoExtent {
    std::uint64_t offset = 0;
    std::uint64_t bytes = 0;
};

/// What the container knows about one payload without decoding it.
struct PtoObject {
    std::uint64_t uid = 0;
    std::string kind;               ///< photons, table, spectrum, image, attachment
    std::string encoding;           ///< dstore, ptu, hdf5, tiff, raw, ...
    std::string name;
    std::string media_type;
    std::string description;
    std::uint64_t rows = 0;         ///< advisory; 0 if the writer did not say

    /// Where the payload is in the file, and how much room it has. `capacity`
    /// is what an in-place update has to fit inside; see \ref PtoFile::update.
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t capacity = 0;
};

class PtoFile;

// -- tables -----------------------------------------------------------------
//
// Declared before PtoFile so their default arguments are stated once: the
// friend declarations inside the class must not repeat them.

/*!
 * \brief Add a DataStore as an object, encoded as `dstore`.
 *
 * Serialised straight into the container with no intermediate copy and no
 * temporary file, so a photon stream of any size costs its own bytes and
 * nothing more.
 *
 * \param reserve extra bytes for a later in-place \ref PtoFile::update.
 * \return the new object's UID, or 0.
 */
std::uint64_t pto_add_store(PtoFile& file, const std::string& kind,
                            const std::string& name, const data::DataStore& store,
                            std::uint64_t reserve = 0);

/// Replace a `dstore` object's payload from a store, in place where it fits.
bool pto_update_store(PtoFile& file, std::uint64_t uid, const data::DataStore& store);

/// Read a `dstore` object back. \throws std::runtime_error if it is not one.
void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out);


/*!
 * \brief A PTO file, open for reading or for writing.
 *
 * Everything except payload bytes is held in memory, which is a few kilobytes
 * for any realistic file, so listing objects and reading tags costs one open.
 * Payloads are read on demand and are never held.
 *
 * Changes are not visible until \ref commit. A crash before it leaves the file
 * exactly as it was; see the specification's account of the two SeekHeads.
 */
class PtoFile {
public:
    PtoFile();
    ~PtoFile();
    PtoFile(const PtoFile&) = delete;
    PtoFile& operator=(const PtoFile&) = delete;

    /// Create an empty container, replacing anything already at `filename`.
    bool create(const std::string& filename, const std::string& title = "");

    /// Open an existing one. \param writable false opens it read-only.
    bool open(const std::string& filename, bool writable = false);

    bool is_open() const;
    void close();
    const std::string& filename() const;

    /// Why the last call returned false, or empty.
    const std::string& error() const;

    // -- what the file says about itself --------------------------------------

    std::string title() const;
    void set_title(const std::string& s);
    std::string writing_app() const;
    void set_writing_app(const std::string& s);
    /// 16 random bytes, identifying this file across copies and renames.
    std::vector<unsigned char> uuid() const;
    /// Which commit this is. Rises by one each time; 0 for a file never committed.
    std::uint64_t generation() const;

    // -- objects ---------------------------------------------------------------

    int n_objects() const;
    std::vector<PtoObject> objects() const;
    bool has(std::uint64_t uid) const;
    /// \throws std::invalid_argument if there is no such object.
    PtoObject object(std::uint64_t uid) const;
    /// The first object with this name, or 0. Names are labels, not identities.
    std::uint64_t find(const std::string& name) const;

    /*!
     * \brief Add an object, and return its UID.
     *
     * \param reserve bytes to leave after the payload so a later \ref update can
     *        grow into it without the object moving. Costs nothing but disk.
     * \return 0 on failure; see \ref error.
     */
    std::uint64_t add(const std::string& kind, const std::string& encoding,
                      const std::string& name, const unsigned char* data,
                      std::size_t n, std::uint64_t reserve = 0);

    /*!
     * \brief Replace an object's payload, keeping its UID.
     *
     * Rewrites in place when the new payload fits the room the old one had,
     * which is the point of the whole format: nothing else in the file moves,
     * whatever else is in it. When it does not fit, the object is written
     * elsewhere and the old space becomes free -- the UID survives, the offset
     * does not.
     */
    bool update(std::uint64_t uid, const unsigned char* data, std::size_t n);

    /// Drop an object. Tags targeting it are left alone, because an application
    /// may want to remember that something was there.
    bool remove(std::uint64_t uid);

    /// The payload. \throws std::runtime_error if there is no such object.
    std::vector<unsigned char> read(std::uint64_t uid) const;

    /*!
     * \brief Write an object's payload out as a file of its own.
     *
     * The way back out of the container: a measurement is saved as one `.pto`
     * holding the original instrument file and everything computed from it, and
     * this is how the instrument file becomes a `.ptu` again for something that
     * only reads those.
     *
     * Copied in blocks, so the payload is never held whole -- extracting an
     * eight-gigabyte stream costs eight gigabytes of disk and a few kilobytes
     * of memory.
     *
     * \return false if there is no such object, or the file could not be
     *         written; see \ref error.
     */
    bool extract(std::uint64_t uid, const std::string& filename) const;

    /*!
     * \brief Take the container apart: every object out into a directory.
     *
     * The way back to separate files. A measurement saved as one `.pto` holding
     * the instrument file and everything computed from it becomes a `.ptu` and
     * a table again, for tools that read only those.
     *
     * Sidecars land beside what they belong to, under their own names, which is
     * what a Becker & Hickl `.spc` needs: its reader looks for the `.set` next
     * to it on disk, and would otherwise silently read half a header.
     *
     * Each object is named by its \ref PtoObject::name, or by its UID when it has none
     * -- and when two share a name, the later ones get the UID as well, because
     * a name is a label and nothing stops two objects having the same one.
     *
     * \return the paths written, in object order. Empty if nothing could be.
     */
    std::vector<std::string> disassemble(const std::string& directory) const;

    // -- metadata ---------------------------------------------------------------

    std::vector<PtoTag> tags() const;
    /// Tags whose target is `uid`. Pass 0 for the tags describing the file.
    std::vector<PtoTag> tags_for(std::uint64_t uid) const;
    void add_tag(const PtoTag& tag);
    void set_tags(const std::vector<PtoTag>& tags);
    void clear_tags();

    std::vector<PtoAnnotation> annotations() const;
    void add_annotation(const PtoAnnotation& note);
    void clear_annotations();

    // -- making it stick ---------------------------------------------------------

    /*!
     * \brief Make every change since the last commit visible, atomically.
     *
     * Writes the index that is not currently live, with a higher generation and
     * a correct checksum, so a reader either sees all of this or none of it.
     */
    bool commit();

    /// The free space in the file. For tests, and for deciding whether a file
    /// has accumulated enough holes to be worth compacting.
    std::vector<PtoExtent> free_extents() const;

    /// Copy the live objects to a new file, dropping the free space. UIDs are
    /// preserved; offsets are not. The only way space comes back.
    bool compact(const std::string& to);

private:
    struct Impl;
    Impl* p_;

    // These three put a DataStore straight into the container, which needs the
    // layout, not the public API: a photon stream is serialised into the file
    // where it will live rather than into a buffer first.
    friend std::uint64_t pto_add_store(PtoFile&, const std::string&,
                                       const std::string&, const data::DataStore&,
                                       std::uint64_t);   // defaults: see above
    friend bool pto_update_store(PtoFile&, std::uint64_t, const data::DataStore&);
    friend void pto_read_store(const PtoFile&, std::uint64_t, data::DataStore&);
};

/// True for a file that begins with an EBML header whose DocType is "pto".
/// Silent on any input, including a missing file.
bool is_pto_file(const std::string& filename);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_PTO_H
