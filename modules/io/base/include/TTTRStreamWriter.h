// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TTTRSTREAMWRITER_H
#define TTTRLIB_TTTRSTREAMWRITER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "PhotonSink.h"

class TTTRHeader;

namespace tttrlib {
namespace io {

/*!
 * \brief Photons to a file as they are measured, for any format that can take them.
 *
 * `TTTR::write` stores a measurement that is already finished, and to do that
 * it needs the whole measurement in memory. An acquisition is a different
 * problem: the photon count is unknown when the file is opened, the run may
 * last hours, the process may be killed, and **the data may be larger than
 * RAM**.
 *
 * \par The producer must not wait for the disk, and must not lose a photon
 * Those two pull against each other and this class is where they are
 * reconciled, once, for every format:
 *
 * - \ref append copies into a buffer and returns. It does no file I/O, so an
 *   instrument thread is never blocked by a slow disk, an fsync, or a
 *   filesystem that stalls for a second.
 * - A **writer thread** drains that buffer to the format underneath. Disk
 *   throughput and photon arrival are therefore decoupled, which is the point:
 *   neither one has to be uniform.
 * - When the buffer reaches \ref buffer_limit, \ref append **blocks until
 *   space frees**. It does not drop, it does not truncate, and it does not
 *   silently grow without bound. That is the guarantee: *photons are never
 *   lost, and memory is never unbounded* — if the disk cannot keep up, the
 *   producer is slowed down and \ref n_stalls says so.
 *
 * \ref n_dropped exists to be asserted on rather than consulted: it is zero by
 * construction, and a test that watches it turns "we never drop" from a claim
 * into a checked property.
 *
 * \par The contract an implementation owes
 * A backend implements only the format-specific half (\ref open_target,
 * \ref write_chunk, \ref close_target) and inherits the rest, so no backend can
 * get the buffering, the backpressure or the loss accounting subtly different.
 *
 * - **A checkpoint is durable.** Everything appended before a successful
 *   \ref checkpoint is readable afterwards, by this process and by any other,
 *   and survives the writer being killed. \ref n_committed counts exactly what
 *   a reader would get.
 * - **A checkpoint with nothing buffered succeeds and writes nothing**, so a
 *   caller checkpointing on a timer need not ask whether photons arrived.
 * - **`close()` drains and checkpoints first**, and so does the destructor.
 */
class TTTRStreamWriter : public PhotonSink {
public:
    virtual ~TTTRStreamWriter();

    TTTRStreamWriter(const TTTRStreamWriter&) = delete;
    TTTRStreamWriter& operator=(const TTTRStreamWriter&) = delete;

    /*!
     * \brief Open `filename` and start the writer thread.
     *
     * \param header supplies the clocks. Required, and not by convention: a
     *        photon stream without them is a column of integers with no unit,
     *        so this refuses rather than inventing a default.
     * \param name what the measurement is called, where the format has a place
     *        to put a name. Ignored by formats that hold one measurement.
     */
    bool create(const std::string& filename, TTTRHeader* header,
                const std::string& name = std::string());

    /*!
     * \brief Hand over events. Copies and returns; does no file I/O.
     *
     * Blocks only when the buffer is full, and then only until the writer
     * thread has made room. \see buffer_limit
     */
    bool append(const unsigned long long* macro_times, std::size_t n_macro,
                const unsigned short* micro_times, std::size_t n_micro,
                const signed char* routing_channels, std::size_t n_routing,
                const signed char* event_types, std::size_t n_event);

    /// Drain everything appended so far to disk and make it durable.
    bool checkpoint();

    /// Drain, checkpoint, stop the writer thread, close. Also done by the destructor.
    bool close();

    bool is_open() const;

    /// Events a reader would get from the file right now.
    std::uint64_t n_committed() const;
    /// Events handed over but not yet on disk.
    std::uint64_t n_buffered() const;
    /*!
     * \brief Photons lost. **Always zero**, and public so a test can say so.
     *
     * There is no path that discards an event: a full buffer blocks the
     * producer instead. A non-zero value here is a bug in this class, not a
     * capacity setting somebody should tune.
     */
    std::uint64_t n_dropped() const;
    /// How often \ref append had to wait for the disk. The backpressure signal.
    std::uint64_t n_stalls() const;
    /// Nanoseconds \ref append has spent waiting, in total.
    std::uint64_t stall_nanoseconds() const;

    /*!
     * \brief How many events may sit in memory before \ref append blocks.
     *
     * This is the memory bound and the loss guarantee in one number: the
     * writer holds at most this many events plus whatever the backend needs
     * for one chunk, whatever the run's length or the disk's mood. Default is
     * 8 M events, about 96 MB of columns.
     */
    void set_buffer_limit(std::uint64_t events);
    std::uint64_t buffer_limit() const;

    /*!
     * \brief Write a chunk automatically once this many events are buffered.
     *
     * The unit of work the writer thread takes, and for a chunked format the
     * unit that becomes durable. Zero means "only on checkpoint or when the
     * buffer limit is reached".
     */
    void set_auto_checkpoint(std::uint64_t events);
    std::uint64_t auto_checkpoint() const;

    /// Why the last call returned false.
    const std::string& error() const;

    // --- PhotonSink: a file is a consumer like any other -------------------
    //
    // What makes "write it AND correlate it AND burst-search it" one
    // acquisition rather than three passes: the writer attaches to a
    // PhotonStreamHub beside the analyses. It is also the sink that most needs
    // the buffering above, since it is the only one waiting on a disk.

    bool submit(const std::uint64_t* macro_times,
                const std::uint16_t* micro_times,
                const std::int8_t* routing_channels,
                const std::int8_t* event_types,
                std::size_t n) override;
    bool flush() override;
    std::string sink_name() const override { return "file"; }

protected:
    TTTRStreamWriter();

    /// Record a failure and return false, so a caller can `return fail(...)`.
    bool fail(const std::string& why);
    void clear_error();

    // --- what a backend implements ------------------------------------------
    //
    // Called only from the writer thread, one at a time, so an implementation
    // needs no locking of its own.

    /// Open the file and write whatever must precede the events.
    virtual bool open_target(const std::string& filename, TTTRHeader* header,
                             const std::string& name) = 0;

    /*!
     * \brief Put `n` events on disk.
     *
     * \param durable true when this chunk must be readable and crash-safe
     *        before returning -- a checkpoint. False lets a backend buffer at
     *        the OS level and postpone the expensive part.
     */
    virtual bool write_chunk(const std::uint64_t* macro_times,
                             const std::uint16_t* micro_times,
                             const std::int8_t* routing_channels,
                             const std::int8_t* event_types,
                             std::size_t n, bool durable) = 0;

    /// Finish the file: patch counts, flush, close.
    virtual bool close_target() = 0;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

/*!
 * \brief A stream writer for `filename`, chosen by its extension, or null.
 *
 * The registry counterpart of \ref FileFormat::write_from. Null means the
 * format cannot be streamed into, which is a real answer rather than a gap.
 */
std::unique_ptr<TTTRStreamWriter> make_stream_writer(const std::string& filename);

/// \overload By container type rather than by name.
std::unique_ptr<TTTRStreamWriter> make_stream_writer_for(int container_type);

/// True if anything can stream into this container type. \see make_stream_writer
bool can_stream(int container_type);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_TTTRSTREAMWRITER_H
