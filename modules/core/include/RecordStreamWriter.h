// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_RECORDSTREAMWRITER_H
#define TTTRLIB_RECORDSTREAMWRITER_H

#include <cstdint>
#include <memory>
#include <string>

#include "TTTRStreamWriter.h"

class TTTRHeader;

namespace tttrlib {
namespace io {

/*!
 * \brief Streaming into any vendor container that is a header plus records.
 *
 * PTU, HT3, SPC-130, SPC-600, SPC-QC, Confocor3, `.sm` — the formats that
 * store a photon as a fixed-width record appended to a file. Everything they
 * have in common is here; what differs is one encoder call, chosen from the
 * record type.
 *
 * \par Why this was not simply possible before
 * The encoders (`TTTR::write_hht3v2_events` and its ten siblings) each kept
 * `MT_ov` — the cumulative macro-time overflow counter — as a **local**
 * starting at zero. That is right for writing a whole measurement and wrong
 * for writing it in pieces: a second call re-emits every overflow record from
 * the beginning, the reader accumulates those on top of the first chunk's, and
 * every macro time after the first chunk comes back too large. The count still
 * matches, which is what makes it worth stating. They now take the counter by
 * pointer and this class carries it across chunks; passing null keeps the
 * whole-file behaviour every existing caller relies on.
 *
 * \par The record count in the header
 * A vendor header states how many records follow, and it has to be written
 * before any of them. This writes a placeholder, then **patches it in place**
 * at each checkpoint, so a file abandoned mid-acquisition still names the
 * number of records actually on disk. That is the one thing a record stream
 * does worse than a container: there is no commit, so a reader has only the
 * count in the header to go on, and a writer killed *between* the last record
 * and the patch leaves a file claiming fewer records than it holds. The extra
 * records are ignored rather than misread, which is the safe direction.
 */
class RecordStreamWriter : public TTTRStreamWriter {
public:
    /*!
     * \param container_type which vendor container to write.
     * \param record_type the record encoding, or -1 for the container's
     *        canonical one. A container and a record type that do not go
     *        together are refused at \ref create rather than producing a file
     *        whose header disagrees with its records.
     */
    explicit RecordStreamWriter(int container_type, int record_type = -1);
    ~RecordStreamWriter() override;

    /// Records written so far. \see n_committed for events.
    std::uint64_t n_records() const;

protected:
    bool open_target(const std::string& filename, TTTRHeader* header,
                     const std::string& name) override;
    bool write_chunk(const std::uint64_t* macro_times,
                     const std::uint16_t* micro_times,
                     const std::int8_t* routing_channels,
                     const std::int8_t* event_types,
                     std::size_t n, bool durable) override;
    bool close_target() override;

private:
    /// \see the implementation; keeps the count honest at every checkpoint.
    bool patch_record_count();

    struct Impl;
    std::unique_ptr<Impl> p_;
};

/*!
 * \brief True if \p container_type can be streamed into as a record stream.
 *
 * False for the containers that are not a record stream at all — Photon-HDF5,
 * Photonscore `.photons`, BrightEyes `.ttr`, FLIM LABS — and for anything with
 * no encoder. A caller gets a named decline rather than a file that starts and
 * then cannot be finished.
 */
bool record_stream_supported(int container_type, int record_type = -1);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_RECORDSTREAMWRITER_H
