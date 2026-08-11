// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TTTRSTREAMWRITER_H
#define TTTRLIB_TTTRSTREAMWRITER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TTTRHeader;

namespace tttrlib {
namespace io {

/*!
 * \brief Photons to a file as they are measured, for any format that can take them.
 *
 * `TTTR::write` stores a measurement that is already finished, and to do that
 * it needs the whole measurement in memory. An acquisition is a different
 * problem and this is the interface for it: the photon count is unknown when
 * the file is opened, the run may last hours, the process may be killed, and
 * **the data may be larger than RAM** — which is the constraint that shapes
 * everything here. An implementation that buffers the run and writes at
 * `close()` satisfies the signatures and defeats the purpose.
 *
 * \par The contract every implementation owes
 *
 * - **Bounded memory.** What an implementation holds may grow with the
 *   checkpoint interval and must not grow with the length of the run.
 * - **A checkpoint is durable.** Everything appended before a successful
 *   \ref checkpoint is readable afterwards by this process and by any other,
 *   and survives the writer being killed. What was appended after it may be
 *   lost, and losing it must leave a file that still reads.
 *   \ref n_committed counts exactly what a reader would get.
 * - **A checkpoint with nothing buffered succeeds and writes nothing.** A
 *   caller checkpointing on a timer must not have to ask first whether any
 *   photons arrived.
 * - **Equal-length arrays are checked, not trusted.** Four arrays that
 *   disagree would attribute every photon after the short one to the wrong
 *   event, which no later stage can detect.
 * - **`close()` checkpoints first**, and so does the destructor, so a stream
 *   unwound by an exception does not discard what it was holding.
 *
 * \par What it deliberately does not promise
 * Nothing here says the file is *complete* between checkpoints, and nothing
 * says an implementation appends to one object, one record stream or one
 * anything. A container may write a sequence of committed chunks and a record
 * stream may append records; both satisfy this interface, and the difference
 * belongs to the format rather than to the caller.
 */
class TTTRStreamWriter {
public:
    virtual ~TTTRStreamWriter();

    TTTRStreamWriter(const TTTRStreamWriter&) = delete;
    TTTRStreamWriter& operator=(const TTTRStreamWriter&) = delete;

    /*!
     * \brief Open `filename` and begin a stream.
     *
     * \param header supplies the clocks. Required, and not by convention: a
     *        photon stream without them is a column of integers with no unit,
     *        so an implementation must refuse rather than invent a default.
     * \param name what the measurement is called, where the format has a place
     *        to put a name. Ignored by formats that hold one measurement.
     * \return false on failure; see \ref error.
     */
    virtual bool create(const std::string& filename, TTTRHeader* header,
                        const std::string& name = std::string()) = 0;

    /// Add events. Buffered; nothing is durable until \ref checkpoint.
    virtual bool append(const unsigned long long* macro_times, std::size_t n_macro,
                        const unsigned short* micro_times, std::size_t n_micro,
                        const signed char* routing_channels, std::size_t n_routing,
                        const signed char* event_types, std::size_t n_event) = 0;

    /// Make everything appended so far durable and readable. \see TTTRStreamWriter
    virtual bool checkpoint() = 0;

    /// Final checkpoint, then close. Also what the destructor does.
    virtual bool close() = 0;

    virtual bool is_open() const = 0;

    /// Events a reader would get from the file right now.
    virtual std::uint64_t n_committed() const = 0;
    /// Events appended but not yet durable.
    virtual std::uint64_t n_buffered() const = 0;

    /*!
     * \brief Checkpoint automatically once this many events are buffered.
     *
     * Zero (the default) never checkpoints on its own. Setting it is how a
     * caller bounds memory without running a timer, which is the usual way an
     * acquisition larger than RAM is kept honest.
     */
    void set_auto_checkpoint(std::uint64_t events) { auto_at_ = events; }
    std::uint64_t auto_checkpoint() const { return auto_at_; }

    /// Why the last call returned false.
    const std::string& error() const { return err_; }

protected:
    TTTRStreamWriter();

    /// Record a failure and return false, so a caller can `return fail(...)`.
    bool fail(const std::string& why) { err_ = why; return false; }
    void clear_error() { err_.clear(); }

    /*!
     * \brief The equal-length check, shared so no implementation forgets it.
     * \return true when all four match.
     */
    bool lengths_agree(std::size_t n_macro, std::size_t n_micro,
                       std::size_t n_routing, std::size_t n_event);

    std::uint64_t auto_at_ = 0;

private:
    std::string err_;
};

/*!
 * \brief A stream writer for `filename`, chosen by its extension, or null.
 *
 * The registry counterpart of \ref FileFormat::write_from: a format that can
 * stream registers a factory and this finds it. Null means the format holds a
 * finished measurement and cannot be streamed into, which is a real answer —
 * most vendor formats put a record count in a header they write first.
 */
std::unique_ptr<TTTRStreamWriter> make_stream_writer(const std::string& filename);

/// \overload By container type rather than by name.
std::unique_ptr<TTTRStreamWriter> make_stream_writer_for(int container_type);

/// True if anything can stream into this container type. \see make_stream_writer
bool can_stream(int container_type);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_TTTRSTREAMWRITER_H
