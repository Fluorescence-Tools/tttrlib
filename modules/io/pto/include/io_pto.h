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

#ifndef SWIG
#include <functional>
#endif

#include "DataStore.h"
#include "TTTRStreamWriter.h"

/// Photon data comes back as one of these. Declared rather than included: the
/// container knows nothing about photons, and only \ref pto_read_events does.
class TTTR;
/// Where a photon stream's clocks come from. \see PtoPhotonStream::create
class TTTRHeader;

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

/*!
 * \brief Reserved tag: the object this one accompanies, as a \ref PtoType::UID.
 *
 * A Becker & Hickl `.spc` keeps half its header in a `.set` beside it, so the
 * two have to travel together and be handed to the reader together. That makes
 * it container business rather than application business -- unlike
 * "derived from", which PTO deliberately leaves undefined -- and it is the one
 * relation the container names itself.
 */
inline constexpr const char* kPtoSidecarTag = "pto.sidecar_of";

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

/*!
 * \brief What a file on disk would become inside a container.
 *
 * The three things an object needs before its bytes: what it is for, how to
 * decode it, and what to call it when it leaves again. \see pto_classify_path.
 */
struct PtoFileType {
    std::string kind;        ///< photons, table, spectrum, image, attachment
    std::string encoding;    ///< ptu, spc-130, csv, png, raw, ...
    std::string media_type;  ///< RFC 6838 type, empty when none is standard
};

/*!
 * \brief What a path holds: its bytes where they say, its name where they do not.
 *
 * The name proposes and the bytes dispose. A file some photon format claims by
 * extension is offered to the sniffers, and which one it is comes back as that
 * format's own name -- `spc-130` rather than `spc`, which four of them claim.
 * A file no photon format claims is never sniffed at all: several of them
 * recognise a container by little more than its record size dividing evenly,
 * and would take a small `.png` for a photon stream.
 *
 * Everything else falls back to a table of extensions, and anything that table
 * does not know -- including a photon file whose contents were not recognised
 * -- is an `attachment` encoded `raw`: carried, named, and left alone.
 *
 * Costs one open of the first few kilobytes, and nothing at all for a path that
 * is not there.
 */
PtoFileType pto_classify_path(const std::string& path);

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

/// Record that `uid` accompanies `primary`. \see kPtoSidecarTag.
void pto_mark_sidecar(PtoFile& file, std::uint64_t uid, std::uint64_t primary);

/// Read a `dstore` object back. \throws std::runtime_error if it is not one.
void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out);

/*!
 * \brief \see pto_read_store, but only the named columns.
 *
 * The reason \ref read_store_into grew an overload taking both a region and a
 * column subset. An embedded store is always a region, so before that existed a
 * caller reading one had to take every column of it -- which is exactly the
 * all-or-nothing the container exists to avoid.
 *
 * Two columns out of a four-gigabyte table costs two seeks; the group tree comes
 * back whole either way, being the directory.
 */
void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out,
                    const std::vector<std::string>& columns);

/*!
 * \brief \see pto_read_store, for a window of rows as well as of columns.
 *
 * What a table viewer needs: paging a million-row burst table otherwise decodes
 * a million rows to show fifty. An empty `columns` means all of them.
 */
void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out,
                    const std::vector<std::string>& columns,
                    std::uint64_t first_row, std::uint64_t n_rows);

/*!
 * \brief Where a `dstore` object's payload lies, ready to be read.
 *
 * The seam between the container and \ref io_store.h, made namable: a caller
 * with its own reason to reach a store hands the returned `offset` and `size`
 * to any of the region-taking \ref read_store_into overloads. Everything below
 * is this plus one call.
 *
 * \throws std::runtime_error if there is no such object, or it is not a store.
 */
PtoObject pto_store_region(const PtoFile& file, std::uint64_t uid);

/// An embedded store's column names, without reading a single column.
/// Empty if the object is not a `dstore`.
std::vector<std::string> pto_store_columns(const PtoFile& file, std::uint64_t uid,
                                           const std::string& group = "");

/// An embedded store's group paths, without reading any data.
std::vector<std::string> pto_store_groups(const PtoFile& file, std::uint64_t uid);

/*!
 * \brief One entry of a cue table: where an event ordinal sits in a payload.
 *
 * Advisory, always. A cue that is wrong must cost a slower decode and never a
 * wrong answer, which is why a reader seeks to the nearest cue *at or before*
 * what it wants and decodes forward from there.
 */
struct PtoCue {
    std::uint64_t event = 0;        ///< event ordinal within the payload
    std::uint64_t offset = 0;       ///< byte offset into the payload
    std::uint64_t time = 0;         ///< macro time at that event, 0 if unrecorded
};


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

    /*!
     * \brief Create an empty container, replacing anything already at `filename`.
     *
     * Takes the writer lock (see \ref open) *before* truncating, so being
     * refused cannot destroy a container someone else is writing.
     */
    bool create(const std::string& filename, const std::string& title = "");

    /*!
     * \brief Open an existing one. \param writable false opens it read-only.
     *
     * \par One writer at a time
     * A writable open takes an exclusive advisory lock on the file and returns
     * false at once -- never blocking -- if another process or another
     * `PtoFile` already holds it; \ref error then says it is open for writing
     * elsewhere. Two writers each carry their own slot table, freelist and
     * generation counter and nothing is visible until \ref commit, so letting
     * both proceed means the second commit publishes an index over the first
     * writer's bytes. A read-only open never takes the lock, because a viewer
     * open during an analysis is the normal case and this format already has
     * the reader seeing the pre-commit state.
     *
     * The lock lives on the descriptor: \ref close drops it, so does a failed
     * open, and so does the process ending, however it ends.
     */
    bool open(const std::string& filename, bool writable = false);

    /*!
     * \brief How a container is open. \see open, PtoPhotonStream
     *
     * The three differ in what they lock and in when what they wrote becomes
     * readable, which is the distinction that matters during an acquisition.
     */
    enum class Mode {
        /*!
         * No lock. A viewer may open a container another process is measuring
         * into and will see the last committed state -- a consistent shorter
         * file, never a half-written one.
         */
        ReadOnly = 0,
        /*!
         * Exclusive advisory lock. Edits accumulate in memory and become
         * visible at \ref commit.
         */
        ReadWrite = 1,
        /*!
         * Exclusive advisory lock, and photons are appended and committed as
         * they arrive. Reached through \ref PtoPhotonStream rather than by
         * opening directly, because a stream owns the object it is filling.
         */
        Stream = 2,
    };

    /// How this container is currently open. \see Mode
    Mode mode() const;

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
    /*!
     * \brief Every object, **in the order they were written**.
     *
     * The order is part of the contract, not an accident of the container
     * layout: a name is a label rather than an identity, so re-running an
     * analysis with a changed setting writes a second object with the same
     * `(kind, name)` and the older one deliberately stays reachable. Write
     * order is then the only thing that says which is which, and every reader
     * needs it to mean the same thing -- see \ref find and \ref find_all.
     */
    std::vector<PtoObject> objects() const;
    bool has(std::uint64_t uid) const;
    /// \throws std::invalid_argument if there is no such object.
    PtoObject object(std::uint64_t uid) const;
    /*!
     * \brief The **most recently written** object with this name, or 0.
     *
     * Names are labels, not identities: a container may hold several objects
     * with one name, and this resolves the tie the way a caller asking for
     * "the" object almost always means -- the newest, which is the result of
     * the latest run.
     *
     * \note This returned the *oldest* match before 2026-08-11, which silently
     *       handed back the stalest analysis in the container to whoever used
     *       the most obvious call. Use \ref find_all to see every one, and
     *       \ref objects for the full write order.
     */
    std::uint64_t find(const std::string& name) const;
    /*!
     * \brief Every object with this name, oldest first, empty if none.
     *
     * What \ref find hides. A reader that wants to compare runs, or to notice
     * that there is more than one, asks here rather than re-deriving
     * "newest wins" from \ref objects -- which is how two readers come to
     * disagree about which result a container is showing.
     */
    std::vector<std::uint64_t> find_all(const std::string& name) const;

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

    /// Embed cheap inspection data (time trace, fluorescence decays, TTTR metadata) into the PTO container.
    bool add_inspection_trace(const std::vector<std::uint32_t>& counts, double dt_s);
    bool add_inspection_decay(int channel, const std::vector<std::uint32_t>& counts, double microtime_ns);
    bool add_inspection_metadata(const std::string& metadata_json);
    bool add_inspection_data(const std::vector<std::uint32_t>& trace_counts,
                            double trace_dt_s,
                            const std::vector<std::uint32_t>& decay_counts,
                            double microtime_ns,
                            const std::string& metadata_json = "");

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

    /*!
     * \brief Add an object whose payload is a file on disk.
     *
     * The mirror image of \ref extract, and the way in for something too big to
     * hold: \ref add takes a pointer and a length, so embedding a four-gigabyte
     * instrument file through it means having four gigabytes in hand first --
     * in Python, a `bytes` object the size of the file.
     *
     * This is not a second code path. \ref add already writes the header and
     * then streams the payload after it; this sizes the payload with
     * `std::filesystem::file_size` and replaces that one write with a block
     * loop. Same header, same slot bookkeeping, same bytes on disk.
     *
     * \return 0 if the path is missing or unreadable, or on a write failure;
     *         see \ref error.
     */
    std::uint64_t add_file(const std::string& kind, const std::string& encoding,
                           const std::string& name, const std::string& path,
                           std::uint64_t reserve = 0);

    /*!
     * \brief Bundle a file on disk, letting the file say what it is.
     *
     * \ref add_file with \ref pto_classify_path in front of it: the caller
     * hands over a path and gets an object whose kind, encoding and media type
     * come from the file itself, named after it. The difference is who decides
     * -- `add_file` is for a caller that knows what it embedded, this is for
     * one that has a directory of files and wants them carried.
     *
     * Every argument after the path overrides what was inferred, so a caller
     * that knows better about one field does not lose the other two.
     *
     * \param name what the object is called, and what \ref disassemble writes
     *        it back out as. Defaults to the filename without its directory.
     * \return 0 if the path is missing or unreadable; see \ref error.
     */
    std::uint64_t attach(const std::string& path, const std::string& name = "",
                         const std::string& kind = "",
                         const std::string& encoding = "",
                         const std::string& media_type = "");

    /*!
     * \brief Add an object that references an external sidecar file via link / relative path.
     *
     * Creates an object entry without copying raw binary data into the container,
     * attaching tags for "_mmfdb_artifact.file_path" and "_mmfdb_artifact.is_sidecar".
     */
    std::uint64_t add_sidecar_file(const std::string& kind, const std::string& encoding,
                                   const std::string& name, const std::string& file_path);

    /// The payload. \throws std::runtime_error if there is no such object.
    std::vector<unsigned char> read(std::uint64_t uid) const;

    /*!
     * \brief `n` bytes of a payload, starting `at` bytes into it.
     *
     * The binding-facing half of \ref read_at: a caller pages through a payload
     * at whatever granularity suits, instead of materialising all of it to look
     * at part of it. Reads short at the end of the payload rather than
     * throwing, like a file read does.
     *
     * \throws std::runtime_error if there is no such object.
     */
    std::vector<unsigned char> read(std::uint64_t uid, std::uint64_t at,
                                    std::size_t n) const;

#ifndef SWIG
    /*!
     * \brief `n` bytes of a payload into a caller's buffer, no copy in between.
     *
     * \return how many bytes were actually read -- short at the end of the
     *         payload, and 0 for an object that is not there.
     */
    std::size_t read_at(std::uint64_t uid, std::uint64_t at,
                        void* into, std::size_t n) const;

    /*!
     * \brief Hand a payload to a sink in blocks, never holding it whole.
     *
     * The way to stream an object somewhere that is not a file -- a socket, a
     * hash, a decoder. \ref extract is this with a file-writing sink, which is
     * what it already was internally.
     *
     * \param sink called with each block in order; returning false stops the
     *        copy and makes this return false.
     */
    bool stream(std::uint64_t uid,
                const std::function<bool(const void*, std::size_t)>& sink) const;
#endif

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
     * A name is a relative path and is checked against \ref pto_object_names
     * before **anything** is written: one object that would land outside
     * `directory` fails the whole call, so a caller who sees the failure does
     * not also have half a directory. This is the check that stands between a
     * container somebody else wrote and the filesystem.
     *
     * \param on_written called with each path as it is written, for a caller
     *        that wants to report progress. Optional -- and the reason this
     *        exists: the CLI used to replicate the naming and the loop to get
     *        its progress ticks, which is how it came to be missing the check
     *        above. A second implementation is a second place to fix.
     *
     * \return the paths written, in object order. Empty if nothing could be;
     *         see \ref error for why.
     */
    std::vector<std::string> disassemble(
            const std::string& directory,
            const std::function<void(const std::string&)>& on_written =
                    std::function<void(const std::string&)>()) const;

    // -- metadata ---------------------------------------------------------------

    std::vector<PtoTag> tags() const;
    /// Tags whose target is `uid`. Pass 0 for the tags describing the file.
    std::vector<PtoTag> tags_for(std::uint64_t uid) const;
    /// Append a tag. A tag identical in every field to one already present is
    /// not appended again: re-describing an object must not make the container
    /// claim the same fact twice (a parent edge re-written on every re-run
    /// once accumulated one copy per analysis).
    void add_tag(const PtoTag& tag);
    /*!
     * \brief State a fact, replacing what was stated before.
     *
     * Removes every tag with the same `(target, name, index)`, then appends
     * `tag`. This is "the row grain IS x" as against \ref add_tag's "x is
     * also true" — the difference between the two is exactly the difference
     * between a scalar tag and an edge, and callers re-running an analysis
     * want this one for everything scalar. Tags describing other objects and
     * other names are untouched.
     */
    void set_tag(const PtoTag& tag);
    void set_tags(const std::vector<PtoTag>& tags);
    void clear_tags();
    /// Remove every tag with this `target` and `name`, any index.
    void clear_tags(std::uint64_t target, const std::string& name);

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

    // -- cues -----------------------------------------------------------------

    /*!
     * \brief Index a photon payload: where every `every_n_events`-th event is.
     *
     * A pass over the payload's records, decoding nothing into memory but a
     * chunk at a time, that records the byte offset and macro time of every
     * n-th event. Written into the container's `Cues` element on the next
     * \ref commit.
     *
     * Not done on every write, and never automatically: a container with no
     * cues is fully valid and a reader that finds none decodes from the start,
     * exactly as before this existed.
     *
     * Spacing is the caller's. One cue per 10\f$^6\f$ events on a 10\f$^9\f$-event
     * stream is a thousand cues, a few tens of kilobytes, and bounds any
     * subsequent decode to 10\f$^6\f$ records.
     *
     * \return how many cues were built; 0 if the object holds no record stream
     *         this build can decode, or on failure -- see \ref error.
     */
    std::uint64_t build_cues(std::uint64_t uid, std::uint64_t every_n_events);

    /// The cue table for an object, ascending by event. Empty when it has none.
    std::vector<PtoCue> cues(std::uint64_t uid) const;

    /// Drop an object's cues. They are also dropped when the object is removed
    /// or its payload replaced, because a cue into bytes that changed is worse
    /// than no cue at all.
    void clear_cues(std::uint64_t uid);

    /// The free space in the file. For tests, and for deciding whether a file
    /// has accumulated enough holes to be worth compacting.
    std::vector<PtoExtent> free_extents() const;

    /*!
     * \brief Copy the live objects to a new file, dropping the free space.
     *
     * The only way space comes back — the same bargain HDF5 makes with
     * `h5repack`. UIDs are preserved and offsets are not, so nothing but the
     * index may hold an offset. Tags, annotations and cues come across: a cue
     * addresses a byte offset *into* a payload, and this moves payloads without
     * changing a byte inside one.
     *
     * Payloads are streamed, never held, so compacting an eight-gigabyte
     * container costs a megabyte of memory.
     *
     * The two knobs are the trade between a small file and a file that stays
     * small. Neither is right for everyone, which is why neither is the only
     * behaviour:
     *
     * \param tight drop the padding that puts each payload on an 8-byte
     *        boundary as well, so the result carries no reclaimable `Void` at
     *        all. The file is as small as the format allows and its payloads
     *        can no longer be mapped and used in place. For an archive or a
     *        copy that is about to be sent somewhere; alignment is a SHOULD, so
     *        the result is still conformant. \see \ref pto_align.
     * \param reserve room to leave after every object, as a fraction of its
     *        payload — 0.25 gives a 4 MiB table a megabyte to grow into. The
     *        opposite trade: a bigger file that absorbs the next few updates
     *        without relocating anything, which is what a container being
     *        edited wants. Default 0, which is what a container being archived
     *        wants.
     *
     * The default is neither: holes gone, payloads aligned, nothing reserved.
     */
    bool compact(const std::string& to, bool tight = false, double reserve = 0.0);

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
    // The rest of the store entry points need no friendship: they go through
    // pto_store_region and filename(), which is the whole point of it existing.
    friend PtoObject pto_store_region(const PtoFile&, std::uint64_t);
};

/*!
 * \brief Photons into a container as they are measured. \see PtoFile::Mode::Stream
 *
 * `TTTR::write` stores a measurement that is already finished. An acquisition
 * is the other case, and it is not the same problem: the photon count is
 * unknown when the file is opened, the run may last hours, and the process may
 * be killed. PRD-034 calls this the case the sink exists for.
 *
 * \par How the growth actually happens, and why it is not one object
 * A `dstore` payload writes its column blobs and *then* a directory describing
 * them, so appending rows to a column would overwrite the next column and move
 * the directory. It cannot grow in place, and pretending otherwise would mean
 * rewriting the whole payload per checkpoint -- O(total) work every second, on
 * a file that is measured for an hour.
 *
 * So a stream writes a **sequence of committed chunk objects** under one name,
 * `name/000000`, `name/000001`, … Nothing about the format changes, because
 * the reader already stacks several photons objects into one measurement in
 * name order, and the zero padding is what makes that order numeric. Each
 * chunk is complete and committed the moment it is written, which is what
 * gives the crash behaviour for free rather than by protocol:
 *
 * - **a killed writer keeps every checkpointed photon** -- an uncommitted
 *   chunk lies outside the `Segment` and is invisible, per the format's
 *   abandoned-write rule, and a later writer reclaims it as `Void`;
 * - **a reader may open the file mid-acquisition** -- a read-only open takes
 *   no lock and sees the committed prefix, a consistent shorter measurement
 *   rather than an error or a torn read.
 *
 * \par Fresh files only
 * \ref create refuses a path that exists. Appending a live stream to a
 * container somebody else assembled means writing chunk objects between their
 * objects, and the "several photons objects are one measurement" rule would
 * then silently absorb theirs into the acquisition.
 */
class PtoPhotonStream : public TTTRStreamWriter {
public:
    PtoPhotonStream();
    ~PtoPhotonStream() override;

    /// How many chunk objects have been committed. \see PtoPhotonStream
    std::uint64_t n_chunks() const;

protected:
    /*!
     * \brief Create the container. Refuses a path that exists.
     *
     * Appending a live stream to a container somebody else assembled means
     * writing chunk objects between their objects, and the "several photons
     * objects are one measurement" rule would then absorb theirs into the
     * acquisition.
     */
    bool open_target(const std::string& filename, TTTRHeader* header,
                     const std::string& name) override;

    /// One committed chunk object. Every chunk is complete on its own.
    bool write_chunk(const std::uint64_t* macro_times,
                     const std::uint16_t* micro_times,
                     const std::int8_t* routing_channels,
                     const std::int8_t* event_types,
                     std::size_t n, bool durable) override;

    bool close_target() override;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

/*!
 * \brief Bundle files and directories into an open container, one object each.
 *
 * The way a measurement scattered over a directory becomes one file: the
 * instrument file, its settings sidecar, the analysis that produced the burst
 * table, the protocol PDF and the note somebody left. Each keeps its name, so
 * \ref PtoFile::disassemble puts the directory back as it was.
 *
 * \par What it does that a loop over \ref PtoFile::attach does not
 * - **A directory means everything under it**, recursively, with each object
 *   named by its path relative to that directory -- `raw/m001.ptu`, not
 *   `m001.ptu` -- so two files of the same name in different folders stay two
 *   files. Entries are visited in sorted order, so the same directory bundles
 *   to the same object order twice running.
 * - **A `.set` is tied to the `.spc` it belongs to** with \ref
 *   kPtoSidecarTag, which is what makes the pair readable afterwards: a
 *   Becker & Hickl reader handed the `.spc` alone silently reads half a header.
 *
 * Nothing is committed. The caller decides when the container becomes visible,
 * because bundling is usually one step of building it -- see \ref
 * PtoFile::commit.
 *
 * \param link_sidecars false to bundle a `.set` as a plain object, for a caller
 *        that wants to say what accompanies what itself.
 * \return the objects made, in the order they were written. Short of `paths`
 *         if something could not be read; \ref PtoFile::error says what.
 * \throws std::runtime_error if a directory cannot be walked to the end. A walk
 *         that stopped early would bundle some of a directory and report that
 *         it bundled the directory, which is the one outcome nobody could
 *         detect afterwards.
 */
std::vector<PtoObject> pto_bundle_files(PtoFile& file,
                                        const std::vector<std::string>& paths,
                                        bool link_sidecars = true);

/*!
 * \brief Read a range of events out of a photon object, into a TTTR.
 *
 * Uses the object's cues to seek: the nearest cue at or before `first_event`
 * says where the decode starts, and the nearest one at or after the end says
 * where it stops. Without cues this decodes the whole payload and slices it --
 * correct, and no faster than opening the whole thing, which is the right way
 * for an advisory index to be absent.
 *
 * `spec` is a container path, optionally naming an object after a `|`:
 * `run.pto|m001.ptu`. `n_events` of 0 means "to the end of the payload".
 *
 * The same range is reachable through the reader-parameter mechanism, which is
 * what a binding will normally use and what \ref PtoFile registers a schema
 * for: `TTTR::set_container_parameters(R"({"first_event": 0, "n_events": 100})")`.
 *
 * \return 1 on success, 0 on failure.
 */
int pto_read_events(const std::string& spec, std::uint64_t first_event,
                    std::uint64_t n_events, ::TTTR* out);

/// True for a file that begins with an EBML header whose DocType is "pto".
/// Silent on any input, including a missing file.
bool is_pto_file(const std::string& filename);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_PTO_H
