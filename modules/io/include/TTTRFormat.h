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
};

}  // namespace tttrlib

#endif  // TTTRLIB_TTTRFORMAT_H
