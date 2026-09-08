// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_PTO_H
#define TTTRLIB_IO_PTO_H

/*!
 * \file io_pto.h
 * \brief PTO -- the PhoTon cOntainer -- as tttrlib uses it.
 *
 * The container itself lives in **ptolib** (`thirdparty/ptolib/ptolib.h`,
 * https://github.com/tpeulen/ptolib), the header tttrlib shares with IMP.bff:
 * the EBML framing, objects, tags, annotations, cues, the two-index atomic
 * commit, in-place update, compaction, the writer lock, and embedding a
 * DataStore as a `dstore` payload. This header re-exports all of that under
 * `tttrlib::io`, where every caller and binding has always found it, and adds
 * what only a photon library can: recognising a photon file by its bytes,
 * indexing a record stream with cues, reading a range of events out of an
 * object into a `TTTR`, streaming an acquisition into a container, and the
 * registration that makes `TTTR("run.pto")` work.
 *
 * `PtoFile` is `pto::File` with those additions; a `PtoFile` is usable
 * wherever a `pto::File` is. See `doc/formats/pto.rst` (now maintained in
 * ptolib as `docs/pto.rst`) for the normative specification.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef SWIG
#include <functional>
#endif

#include "ptolib/ptolib.h"
#include "DataStore.h"
#include "TTTRStreamWriter.h"

/// Photon data comes back as one of these. Declared rather than included: the
/// container knows nothing about photons, and only \ref pto_read_events does.
class TTTR;
/// Where a photon stream's clocks come from. \see PtoPhotonStream::create
class TTTRHeader;

namespace tttrlib {
namespace io {

// -- the container, from ptolib -------------------------------------------------

using pto::PtoType;
using pto::PtoTag;
using pto::PtoAnnotation;
using pto::kPtoSidecarTag;
using pto::PtoExtent;
using pto::PtoObject;
using pto::PtoFileType;
using pto::PtoCue;
using pto::Element;
using pto::Problem;
using pto::kDefaultBanner;
using pto::element_name;
using pto::classify_by_extension;
using pto::pto_add_store;
using pto::pto_update_store;
using pto::pto_mark_sidecar;
using pto::pto_read_store;
using pto::pto_store_region;
using pto::pto_store_columns;
using pto::pto_store_groups;
using pto::pto_bundle_files;
using pto::is_pto_file;

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

/// What \ref PtoFile::create writes into the banner: ptolib's decoding note,
/// plus where to get this library.
extern const char* const kTttrlibBanner;

/*!
 * \brief A PTO file, open for reading or for writing, with tttrlib's photon knowledge.
 *
 * Everything a container does is inherited from `pto::File`; see that class
 * for the contract (commit, update in place, compact, tags, cues, the lock).
 * This class adds the members that need a photon library behind them, and
 * overrides the two hooks the container leaves open: \ref classify, so that
 * \ref attach and \ref pto_bundle_files recognise a photon file by its bytes,
 * and \ref external_payload_path, so that an object recorded as a sidecar
 * reference (`_mmfdb_artifact.file_path`) streams from the file beside the
 * container.
 */
class PtoFile : public pto::File {
public:
    PtoFile();
    ~PtoFile() override;

    /*!
     * \brief Create an empty container, replacing anything already at `filename`.
     *
     * `pto::File::create` with tttrlib's banner: the decoding note, and where
     * a reader for photon containers is found.
     */
    bool create(const std::string& filename, const std::string& title = "");

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
     * \brief Add an object that references an external sidecar file via link / relative path.
     *
     * Creates an object entry without copying raw binary data into the container,
     * attaching tags for "_mmfdb_artifact.file_path" and "_mmfdb_artifact.is_sidecar".
     */
    std::uint64_t add_sidecar_file(const std::string& kind, const std::string& encoding,
                                   const std::string& name, const std::string& file_path);

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

    /// \ref pto_classify_path: the sniffers first, then the extension table.
    PtoFileType classify(const std::string& path) const override;

    /// The `_mmfdb_artifact.file_path` tag, resolved beside the container.
    std::string external_payload_path(std::uint64_t uid) const override;
};

/*!
 * \brief Photons into a container as they are measured. \see PtoFile::Mode::Stream
 *
 * `TTTR::write` stores a measurement that is already finished. An acquisition
 * is the other case, and it is not the same problem: the photon count is
 * unknown when the file is opened, the run may last hours, and the process may
 * be killed. That is the case this sink exists for.
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

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_PTO_H
