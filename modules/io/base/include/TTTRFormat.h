// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TTTRFORMAT_H
#define TTTRLIB_TTTRFORMAT_H

/*!
 * \file TTTRFormat.h
 * \brief What tttrlib knows about a file format, in one place.
 *
 * The same knowledge -- which container is called what, which extension it
 * uses, which record types are valid in it -- was written out six times, in six
 * shapes, in five files:
 *
 *   TTTR::initialize_container_names()      name  <-> int
 *   Registry.cpp file_container_entries()   name, label, extensions, int
 *   inferTTTRFileType()                     extension -> sniff -> int
 *   inferTTTRContainerTypeFromExtension()   extension -> int
 *   tttrContainerCanonicalExtension()       int -> extension
 *   valid_container_record_pair()           (int, int) -> bool
 *   default_record_type_for_container()     int -> int
 *
 * Nothing made them agree. Adding the Photonscore reader meant eleven edits
 * across five files, none of which the compiler would have missed. This header
 * replaces all of it with one description per format, and everything above
 * becomes a query against the table.
 *
 * That is also what makes formats extensible from outside: a plugin cannot edit
 * six switch statements inside a compiled library, but it can add a row.
 *
 * Deliberately independent of TTTR.h. A format description is data -- naming a
 * format must not drag in the photon-stream data model, its histogram, its mask
 * or its friendship with CLSMImage. This header includes <string> and <vector>
 * and nothing else, which is what lets a format module stay cheap and what lets
 * the same shape be mirrored across the C plugin ABI later.
 */

#include <string>
#include <vector>

namespace tttrlib {

/*!
 * \brief Separates a container from the object inside it: `run.pto|m001.spc`.
 *
 * A pipe, not a colon: `C:\\data\\run.pto` already has a colon in it, and
 * telling a drive letter from a selector would be guesswork on every path on
 * one platform. A pipe is not legal in a filename on Windows and is vanishingly
 * rare on anything else.
 */
extern const char kSubfileSeparator;

/// The container part of a spec -- everything before the last \ref
/// kSubfileSeparator, or the whole thing when there is none.
std::string subfile_path(const std::string& spec);

/// The object part, or empty when the spec names no particular one.
std::string subfile_selector(const std::string& spec);

/*!
 * \brief One file format tttrlib can read and/or write.
 *
 * `container_type` is the integer that has always identified this format.
 * Built-in formats own 0-999 permanently: those integers are baked into tests
 * and into user code that stored them. Anything registered from a plugin is
 * allocated 1000 and up, in load order, and is therefore session-local -- for a
 * plugin the *name* is the stable identifier, and `stable` says so.
 */
struct FileFormat {
    /// Stable identifier, e.g. "PTU". This is what a user passes and what a
    /// plugin claims; it never changes and never collides silently.
    std::string name;

    /// Historical integer identifier. See the note above about 0-999.
    int container_type = -1;

    /// Human-readable name, e.g. "PicoQuant PTU".
    std::string label;

    /// One line of description, for a UI that lists formats.
    std::string summary;

    /// Filename extensions, lowercase and without the dot, e.g. {"h5","hdf5"}.
    /// More than one format may claim the same extension -- ".spc" is four --
    /// which is exactly why sniffing exists.
    std::vector<std::string> extensions;

    /// Extension used when tttrlib writes this format and has to choose one.
    /// Not simply extensions[0]: Photon-HDF5 is listed as ".h5,.hdf5" but
    /// writes "hdf5". Defaults to extensions[0] when left empty.
    std::string canonical_extension;

    /*!
     * \brief Recognise this format from the file's contents.
     *
     * Registered by whoever owns the reader, so the format table does not have
     * to reach up a layer to call it. A null sniffer means one of two things,
     * distinguished by \ref detectable:
     *   - detectable: accept on the extension alone. Only "SM" does this, and
     *     it is what the hand-written dispatcher did -- isSMFile() exists but
     *     was never called from it.
     *   - not detectable: never identified from a file at all. SPC-600_256 and
     *     SPC-600_4096 claim ".spc" but were never candidates; the caller has
     *     to name them.
     */
    bool (*sniff)(const std::string& filename) = nullptr;

    /*!
     * \brief As \ref sniff, but carrying state.
     *
     * A plain function pointer cannot close over anything, which is fine for
     * the built-in predicates -- each one *is* the whole answer for its format.
     * A plugin's sniffer is a different function per format behind one C entry
     * point, so it needs to be told which format is asking. Preferred over
     * \ref sniff when both are set.
     */
    bool (*sniff_with_context)(void* context, const std::string& filename) = nullptr;

    /*!
     * \brief Read a file of this format into a TTTR, for a reader above core.
     *
     * A container format is read by a module that sits ON TOP of the photon
     * data model -- PTO holds a TTTR file, so io_pto depends on core and core
     * therefore cannot call it. This pointer inverts that, the same way
     * \ref sniff does for recognition: the layer that owns the reader hands it
     * down, and core calls through a pointer it was given.
     *
     * Deliberately C-shaped -- `const char*`, `void*`, `int` -- because the
     * modules are separate shared libraries and this crosses between them. A
     * `std::string` or a `TTTR&` here would tie every module to one compiler's
     * library ABI, which is the thing PRD-018 is about. `tttr` is a
     * `tttrlib::TTTR*`; `spec` is the whole thing the caller passed, selector
     * and all, because splitting it is the format's business.
     *
     * \return 1 on success, 0 on failure.
     */
    int (*read_into)(void* context, const char* spec, void* tttr) = nullptr;

    /// Passed back to \ref read_into. \see sniff_context.
    void* read_context = nullptr;

    /// Passed to \ref sniff_with_context. Owned by whoever set it.
    void* sniff_context = nullptr;

    /// Whether this format takes part in content-based detection. See \ref sniff.
    bool detectable = true;

    /*!
     * \brief Reader parameters this format needs, as a JSON Schema.
     *
     * Most formats need none: everything about the measurement is in the file,
     * so the string is empty and a caller passes nothing. A few cannot be read
     * without knowing something the file does not record -- a BrightEyes
     * ``.ttr`` is a bare word stream whose sample clock, laser rate and channel
     * count are properties of the instrument -- and those are named here.
     *
     * A schema rather than a struct, because the alternative is per-format
     * plumbing in every language binding. The registry already publishes
     * JSON Schema for fit models and burst searches, and a frontend that can
     * render one of those renders this one too; the reader receives the same
     * JSON object the schema describes. What a format requires is therefore
     * discoverable rather than documented, which matters most for the formats
     * that need it, since those are exactly the ones a caller cannot guess.
     *
     * Empty means the format takes no parameters, and passing any is an error
     * rather than a silent no-op.
     */
    std::string parameters_schema;

    /*!
     * \brief Whether this container can be read in pieces.
     *
     * True for the containers that are a header followed by fixed-width
     * records, where record `first` is a seek away, and for PTO, which indexes
     * its payloads. False everywhere else, and the false is the useful half: a
     * caller asking for a range of a Photon-HDF5 or a FLIM LABS file gets a
     * named decline rather than a silent full read.
     *
     * Declared here so it is discoverable through the registry, next to
     * \ref parameters_schema, which is where the range is asked for.
     */
    bool ranged_reads = false;

    /// Record encodings valid inside this container. Empty means "any", which
    /// is true of Photon-HDF5: it stores decoded arrays, not records.
    std::vector<int> record_types;

    /// Encoding used when transcoding into this container and the header does
    /// not carry a usable one. -1 if the format does not need one.
    int default_record_type = -1;

    bool can_read = true;
    bool can_write = false;

    /// False for plugin-provided formats, whose container_type is session-local.
    bool stable = true;

    /// True if this format accepts \p record_type.
    bool accepts_record_type(int record_type) const {
        if (record_types.empty()) return true;
        for (int r : record_types) if (r == record_type) return true;
        return false;
    }

    /// True if \p extension (lowercase, no dot) is one of ours.
    bool has_extension(const std::string& extension) const {
        for (const auto& e : extensions) if (e == extension) return true;
        return false;
    }

    /// The extension to write with; see \ref canonical_extension.
    std::string write_extension() const {
        if (!canonical_extension.empty()) return canonical_extension;
        return extensions.empty() ? std::string() : extensions.front();
    }
};

/*!
 * \brief The formats tttrlib currently knows about.
 *
 * A function-local static behind an accessor, not a namespace-scope table:
 * registration has to be able to happen from another translation unit -- an
 * io_* module, or a plugin -- and a namespace-scope table could still be
 * unconstructed when the first of those runs. The decay-fit factory and
 * TTTR::container_names() take the same approach for the same reason.
 */
class IORegistry {
public:
    /// Every known format, in ascending container_type order.
    static const std::vector<FileFormat>& formats();

    /// The format called \p name, or nullptr.
    static const FileFormat* by_name(const std::string& name);

    /// The format with this container id, or nullptr.
    static const FileFormat* by_container_type(int container_type);

    /*!
     * \brief Every format claiming \p extension, in ascending container_type.
     *
     * The order is load-bearing: it is what makes ".spc" try SPC-130 (2) before
     * SPC-QC (9), which is the order the hand-written dispatcher used and which
     * decides what an ambiguous file is detected as.
     */
    static std::vector<const FileFormat*> by_extension(const std::string& extension);

    /*!
     * \brief Add a format. Returns false if the name is already taken.
     *
     * Refused rather than shadowed, deliberately: a plugin that silently
     * replaced the built-in "PTU" reader would be a supply-chain problem, not a
     * feature.
     */
    static bool add(const FileFormat& format);

    /*!
     * \brief Remove the format called \p name. Returns false if there is none.
     *
     * Exists for one purpose: a plugin whose initialisation fails partway must
     * leave no trace, so the host rolls back each row it added. Not a general
     * "unregister" -- removing a format other code is holding a container id
     * for would be a way to make that id mean something else later.
     */
    static bool remove(const std::string& name);

    /*!
     * \brief Bumped on every add and remove.
     *
     * Anything that caches a view of this table -- the container name map is
     * one -- compares this to know its copy is stale. Without it, a table built
     * before the plugins loaded stays authoritative forever, and a plugin
     * format is registered but unnameable.
     */
    static unsigned long generation();

    /*!
     * \brief Attach a content sniffer to an already-registered format.
     *
     * The predicates that recognise a PTU or an HT3 by its bytes are public API
     * in their own right and live with the readers, a layer above this table.
     * Rather than have the table reach up for them -- which would put the
     * photon-stream data model underneath a plain description of a format --
     * that layer hands them down here on first use.
     */
    static bool set_sniffer(const std::string& name,
                            bool (*sniff)(const std::string&));

    /*!
     * \brief Attach a reader to an already-registered format. \see FileFormat::read_into.
     *
     * For a container whose reader lives above core. Returns false if there is
     * no format of that name.
     */
    static bool set_reader(const std::string& name,
                           int (*read_into)(void*, const char*, void*),
                           void* context = nullptr);

    /*!
     * \brief Identify the format of \p filename: extension, then contents.
     *
     * Candidates are tried in ascending container_type order, which is not an
     * arbitrary choice -- it is what makes ".spc" try SPC-130 (2) before
     * SPC-QC (9), reproducing the hand-written dispatcher exactly instead of by
     * coincidence. Returns the container id, or -1.
     */
    static int infer_container_type(const std::string& filename);

    /// Container id for \p filename's extension alone, without reading it. -1 if none.
    static int container_type_from_extension(const std::string& filename);

    /// Lowercased extension of \p filename without the dot, or "".
    static std::string extension_of(const std::string& filename);
};

}  // namespace tttrlib

#endif  // TTTRLIB_TTTRFORMAT_H
