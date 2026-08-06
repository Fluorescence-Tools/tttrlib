// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_FLIMLABS_H
#define TTTRLIB_IO_FLIMLABS_H

/*!
 * \file io_fl.h
 * \brief FLIM LABS time-tagger data (``.bin``).
 *
 * FLIM LABS build an FPGA data-acquisition card with a SPAD detector and a
 * pulsed laser, driven by their Spectroscopy, Intensity Tracing and FCS
 * applications. All of those write ``.bin`` files behind the same envelope, and
 * only two of them contain photons:
 *
 * | magic  | contents | here |
 * |--------|----------|------|
 * | ``STT1`` | spectroscopy time tagger: one record per event, with a micro time | **yes** |
 * | ``ITT1`` | intensity tracing / FCS time tagger: one record per event, no micro time | **yes** |
 * | ``SP01`` | decay curves, 256 bins per channel per interval | no |
 * | ``SPF1`` | phasor coordinates | no |
 * | ``IT02`` | intensity traces, counts per interval | no |
 * | ``FCS1`` | correlation curves | no |
 *
 * The four excluded ones are analysis products. There are no photons left in
 * them, so there is nothing for a TTTR container to read them into -- reading
 * them would mean inventing events.
 *
 * \section fl_layout Layout
 *
 * Four ASCII magic bytes, a little-endian ``uint32`` header length, a UTF-8
 * JSON header, and then fixed-size records to the end of the file. All five
 * formats share the envelope, which is why the magic is what identifies them.
 *
 * An ``STT1`` record is 17 bytes: ``uint8`` event, ``float64`` micro time in
 * nanoseconds, ``float64`` macro time in nanoseconds. An ``ITT1`` record is the
 * same thing without the micro time, 9 bytes.
 *
 * Seventeen bytes with a ``float64`` at offset 1 is not a struct any compiler
 * will lay out that way -- it would pad to 24 -- so the fields are read
 * explicitly rather than cast.
 *
 * \section fl_events Events
 *
 * The ``event`` byte is a detector index, zero-based, except for three reserved
 * values that are the ASCII letters of what they mark: ``70`` = ``'F'`` frame,
 * ``76`` = ``'L'`` line, ``80`` = ``'P'`` pixel. Markers keep those numbers as
 * their routing channel here, rather than being renumbered to 1/2/3: the
 * hardware has channels numbered 1, 2 and 3, and giving a marker the same
 * number as a detector invites exactly one kind of bug.
 *
 * The reserved values also mean a detector index of 70 or above would be
 * indistinguishable from a marker. No such hardware exists, but the reader
 * refuses rather than guesses if a file ever declares one.
 *
 * \section fl_quantisation Times are floats, and tttrlib's are not
 *
 * This is the format's one real difficulty. Both times are ``float64``
 * nanoseconds -- already calibrated, already absolute -- while tttrlib's model
 * is an integer tick, and the file does not say what tick to use.
 *
 * For ``STT1`` the tick is **one laser period**, which the header states, so a
 * macro time is a laser pulse count and the container behaves like a T3 file.
 * That keeps ``macro_time * resolution`` exact rather than approximate, and it
 * is the unit the instrument actually works in: the hardware histograms arrival
 * times into 256 bins of the laser period, which is also where the micro time
 * resolution comes from. Nothing is lost that the instrument could measure.
 *
 * For ``ITT1`` there is no micro time and no reason to think the timestamps are
 * laser-gated -- an FCS correlation would be quantised to the laser period,
 * throwing away the resolution the measurement is for -- so the tick is one
 * picosecond. That covers 213 days before a ``uint64`` runs out.
 *
 * Either way the choice is written into the header of the resulting object,
 * because it cannot be recovered from the file.
 *
 * \section fl_order Records are not in time order
 *
 * Every reader FLIM LABS ships sorts by macro time after loading, which says
 * the per-channel FIFOs reach the file interleaved. So this one sorts too, and
 * therefore reads the whole file into memory first -- the one container here
 * that cannot stream.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tttrlib {
namespace io {

/// Which FLIM LABS format a file's magic says it is.
enum FlimLabsFlavour {
    FLIMLABS_NOT       = 0,   ///< not a FLIM LABS time tagger file
    FLIMLABS_STT1      = 1,   ///< spectroscopy time tagger, 17 byte records
    FLIMLABS_ITT1      = 2    ///< intensity tracing / FCS time tagger, 9 byte records
};

/*!
 * \brief One decoded FLIM LABS time-tagger file.
 *
 * The four per-event arrays are tttrlib's, so this drops into the same model as
 * every other container. The fields after them describe what the numbers in
 * those arrays mean, which for this format is not something a later reader of
 * the file could work out; see \ref fl_quantisation.
 */
struct FlimLabsData {
    std::vector<uint64_t> macro_times;      ///< laser pulses (STT1) or picoseconds (ITT1)
    std::vector<uint16_t> micro_times;      ///< bins of the laser period; always 0 for ITT1
    std::vector<int8_t>   routing_channels;
    std::vector<int8_t>   event_types;      ///< 0 photon, 1 marker

    FlimLabsFlavour flavour = FLIMLABS_NOT;

    /// The file's JSON header, verbatim.
    std::string metadata_json;

    /// The ``channels`` list from the header: zero-based detector indices.
    /// Empty if the header does not say.
    std::vector<int> channels;

    /// The ``laser_period_ns`` from the header.
    double laser_period_ns = 0.0;

    /// Duration of one macro time tick, in seconds.
    double macro_time_resolution_s = 0.0;

    /// Duration of one micro time bin, in seconds. Zero for ITT1.
    double micro_time_resolution_s = 0.0;

    /// Span of the micro time axis. 256 for STT1 -- the hardware's own binning
    /// -- and 1 for ITT1, which has no micro time.
    int n_micro_time_channels = 1;

    /*!
     * Largest gap between a record's macro time and the laser pulse it was
     * assigned to, in nanoseconds. It says which of two readings of the file is
     * true, and both are handled:
     *
     * - near zero: the file's macro times are already laser-pulse counts, and
     *   the conversion is exact.
     * - up to one laser period: they are absolute arrival times, so the pulse
     *   is their whole part and the micro time is the remainder.
     */
    double macro_time_residual_ns = 0.0;

    /// Number of events whose channel was not in the header's ``channels``
    /// list. Not an error -- the list may be absent -- but worth reporting.
    std::size_t n_undeclared_channel = 0;

    /// Marker channel numbers: the ASCII letters the format reserves.
    static constexpr int8_t MARKER_FRAME = 70;   // 'F'
    static constexpr int8_t MARKER_LINE  = 76;   // 'L'
    static constexpr int8_t MARKER_PIXEL = 80;   // 'P'
};

/*!
 * \brief What \p filename's magic says it is, without decoding it.
 *
 * Recognises only the two time taggers; the four analysis formats sharing the
 * envelope report \ref FLIMLABS_NOT, because a container that cannot read them
 * should not claim them. The record size is checked against the file length as
 * well, so a truncated or mislabelled file is not accepted on four bytes alone.
 */
FlimLabsFlavour flimlabs_flavour(const std::string& filename);

/*!
 * \brief Decode \p filename into per-event arrays, in time order.
 *
 * \throws std::runtime_error if the file is not a FLIM LABS time tagger, if an
 *         ``STT1`` header does not state its laser period -- without which
 *         there is no tick to quantise to, and inventing one would silently
 *         rescale every time in the file -- or if the header declares a
 *         detector index that collides with a marker code.
 */
FlimLabsData read_flimlabs(const std::string& filename);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_FLIMLABS_H
