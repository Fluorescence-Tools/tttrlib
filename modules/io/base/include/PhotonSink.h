// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PHOTONSINK_H
#define TTTRLIB_PHOTONSINK_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TTTRHeader;

namespace tttrlib {
namespace io {

/*!
 * \brief Anything that ingests photons as they arrive.
 *
 * One source, one buffer, many consumers: a live acquisition is written to a
 * file *and* correlated *and* burst-searched *and* drawn, from the same
 * photons, without anybody copying the stream or reading it back. This is the
 * one interface all of those implement, so a source knows nothing about what
 * is downstream of it and a consumer knows nothing about where the photons
 * came from.
 *
 * \par Events, not records
 * A sink is handed decoded events — the four columns — rather than a vendor
 * record stream. Decoding once at the source is what lets an FCS correlator
 * and a `.spc` writer sit side by side on the same acquisition.
 *
 * \par What an implementation owes
 * - \ref submit **must not block for long.** Every sink on a hub is fed from
 *   the same call, so a slow one delays the others and, eventually, the
 *   instrument. A consumer that needs to do real work should buffer and do it
 *   on its own thread — which is what \ref TTTRStreamWriter does.
 * - Returning false means *this sink has failed*, not that the photons were
 *   bad. A hub keeps feeding the others and reports which one broke.
 * - \ref set_header is called once, before any events. A consumer that needs
 *   the clocks (a correlator's lag axis, a decay histogram's bin width) takes
 *   them there.
 */
class PhotonSink {
public:
    virtual ~PhotonSink();

    /*!
     * \brief Take `n` events. The four arrays are parallel and equal length.
     *
     * The pointers are borrowed and valid only for the duration of the call: a
     * sink that needs to keep the events copies them. Borrowing is deliberate
     * — with several sinks on one hub, handing each its own copy would
     * multiply the largest allocation in the process by the number of
     * consumers.
     */
    virtual bool submit(const std::uint64_t* macro_times,
                        const std::uint16_t* micro_times,
                        const std::int8_t* routing_channels,
                        const std::int8_t* event_types,
                        std::size_t n) = 0;

    /// The clocks and the instrument header. Called once, before any events.
    virtual void set_header(TTTRHeader* header) { (void) header; }

    /// Finish whatever is pending. A hub calls this on every sink at the end.
    virtual bool flush() { return true; }

    /// What to call this sink when something goes wrong with it.
    virtual std::string sink_name() const { return "sink"; }

    /*!
     * \brief \ref submit with a length per array, for callers that pass arrays.
     *
     * Not virtual and not a second path: it checks the four lengths agree and
     * forwards. It exists because a binding hands over four arrays that each
     * know their own size, and the check that they match belongs here rather
     * than in every language's wrapper.
     *
     * \return false, without submitting anything, if the lengths disagree.
     */
    bool submit_events(const unsigned long long* macro_times, std::size_t n_macro,
                       const unsigned short* micro_times, std::size_t n_micro,
                       const signed char* routing_channels, std::size_t n_routing,
                       const signed char* event_types, std::size_t n_event);
};

/*!
 * \brief One photon stream, many consumers.
 *
 * A hub is itself a \ref PhotonSink, so it attaches wherever a single consumer
 * would and can be nested. Submitting to it submits to every attached sink, in
 * attachment order, from the caller's thread.
 *
 * \par Why the fan-out does not buffer, and why that is not a shortcut
 * The obvious design gives every sink its own queue and its own thread. This
 * one does not, because the buffering belongs to whichever consumer actually
 * needs it: a file writer must decouple from the disk and owns a queue for
 * exactly that (\ref TTTRStreamWriter), while a correlator or a burst search
 * is arithmetic on the events and is *faster* without a hand-off. Giving
 * everything a queue would add a copy and a thread hop per consumer to pay for
 * a problem only one of them has.
 *
 * \par No sink is dropped, and no photon is
 * A slow consumer slows the hub, and the hub slows the source. That is a
 * deliberate choice against the other option — discarding events for whoever
 * cannot keep up — which turns a performance problem into missing data that
 * nothing downstream can detect. If an acquisition cannot afford to be slowed,
 * the answer is a consumer that buffers, not a hub that forgets.
 */
class PhotonStreamHub : public PhotonSink {
public:
    PhotonStreamHub();
    ~PhotonStreamHub() override;

    PhotonStreamHub(const PhotonStreamHub&) = delete;
    PhotonStreamHub& operator=(const PhotonStreamHub&) = delete;

    /*!
     * \brief Attach a consumer. Not owned; it must outlive the hub.
     *
     * Borrowed rather than owned so a caller can keep a typed handle to its
     * correlator and read the result out of it — which is the whole point of
     * attaching one. \see add_owned_sink
     */
    void add_sink(PhotonSink* sink);

    /// Attach a consumer and take ownership of it.
    void add_owned_sink(std::unique_ptr<PhotonSink> sink);

    /// Detach a consumer. Safe to call for one that was never attached.
    void remove_sink(PhotonSink* sink);

    std::size_t n_sinks() const;

    /// Events handed to this hub.
    std::uint64_t n_events() const;

    /*!
     * \brief Feed every attached sink.
     *
     * Every sink is offered the batch even if an earlier one failed: a broken
     * file writer must not silently stop the correlator that is still working.
     * The return is false if any sink failed, and \ref error names the first.
     */
    bool submit(const std::uint64_t* macro_times,
                const std::uint16_t* micro_times,
                const std::int8_t* routing_channels,
                const std::int8_t* event_types,
                std::size_t n) override;

    void set_header(TTTRHeader* header) override;
    bool flush() override;
    std::string sink_name() const override { return "hub"; }

    /// Why the last submit or flush returned false.
    const std::string& error() const;

    /// Sinks that have failed, by name, in the order they failed.
    std::vector<std::string> failed_sinks() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_PHOTONSINK_H
