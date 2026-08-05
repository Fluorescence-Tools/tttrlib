// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_BRIGHTEYES_H
#define TTTRLIB_IO_BRIGHTEYES_H

/*!
 * \file io_be.h
 * \brief BrightEyes-TTM raw time-tagging data (``.ttr``).
 *
 * The open-hardware time-tagging module from the Vicidomini lab (IIT): a
 * Xilinx Kintex-7 TDC with ~30 ps resolution, built for SPAD arrays of 25 or 49
 * elements on a scanning microscope.
 *
 * \section ttr_shape What makes this format different
 *
 * There is no header, no magic and no length -- a ``.ttr`` is a bare stream of
 * little-endian ``uint16`` words. Two consequences follow, and both are why this
 * reader does not look like the others:
 *
 * 1. **It can never be identified from its contents.** Any file is a valid
 *    sequence of 16-bit words. The caller has to say what it is.
 * 2. **The file does not contain absolute times.** The sample clock, the laser
 *    frequency and the number of detector elements are properties of the
 *    instrument, not of the file, and the TDC payload is a raw tapped-delay-line
 *    code rather than a duration. They must be supplied.
 *
 * That is what \ref TtrParams is for. Everything the format cannot tell you
 * about itself is named in one place, so it is obvious what a caller is
 * asserting rather than buried in defaults.
 *
 * \section ttr_words The word layout
 *
 * Each 16-bit word is ``valid << 15 | ID << 8 | data``:
 *
 * | ID | meaning |
 * |----|---------|
 * | ``0..n_channels-1`` | detector element; ``data`` is its raw TDC code |
 * | ``123`` | dummy |
 * | ``124`` | laser sync; ``data`` is the reference TDC code |
 * | ``125`` | step byte A (low 7 bits) + bit 7 = pixel clock |
 * | ``126`` | step byte B (mid 7 bits) + bit 7 = line clock |
 * | ``127`` | step byte C (high 7 bits) + bit 7 = frame clock -- **ends the record** |
 *
 * Records are variable length and are delimited by ``ID == 127``. The three step
 * bytes carry 7 bits each, because bit 7 of each is a scanner enable, giving a
 * 21-bit coarse counter of sample clock ticks that wraps and is unwrapped here.
 *
 * \section ttr_valid The valid bit is not decoration
 *
 * The FPGA emits detector words in aligned pairs, exactly one of which has
 * ``valid`` set; the other is zero-filled. A reader that ignores the valid bit
 * sees twice as many photons as the file contains, half of them at TDC code 0.
 * The laser word carries ``valid`` in only about half of the records.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tttrlib {
namespace io {

/*!
 * \brief What a ``.ttr`` file cannot tell you about itself.
 *
 * Defaults match the firmware v2.0 hardware and the published datasets, but a
 * default is a guess: if the instrument differs, the times are wrong rather than
 * absent, which is the failure mode worth being explicit about.
 */
struct TtrParams {
    /// Number of SPAD elements; 25 (5x5) or 49 (7x7). Words with a smaller ID
    /// are detector words, so this decides where the channel range ends.
    int n_channels = 25;

    /// Sample clock in MHz. The step counter counts these ticks.
    double sysclk_MHz = 240.0;

    /// Laser repetition rate in MHz. Only needed to turn micro times into
    /// nanoseconds; leave at 0 to skip that.
    double laser_MHz = 0.0;

    /*!
     * Picoseconds per TDC code, if a calibration is known.
     *
     * The payload is a tapped-delay-line code whose bins are not equal, so a
     * single number is an approximation -- the proper conversion is a
     * per-channel bin-width (DNL) calibration derived from the code histogram
     * over one clock period. Left at 0, micro times stay in raw codes, which is
     * honest: an uncalibrated code is not a time and pretending otherwise
     * produces a plausible-looking wrong answer.
     */
    double tdc_ps_per_code = 0.0;

    /// Drop the 0x7FFF idle word before parsing.
    bool drop_filler = true;
};

/*!
 * \brief One decoded ``.ttr`` stream.
 *
 * Laid out as tttrlib's four per-event arrays, so it drops into the same model
 * as every other container: macro time, micro time, routing channel, event type
 * (0 photon, 1 marker).
 */
struct TtrData {
    std::vector<uint64_t> macro_times;    ///< sample-clock ticks, unwrapped
    std::vector<uint16_t> micro_times;    ///< t_channel - t_laser, in TDC codes unless calibrated
    std::vector<int8_t>   routing_channels;
    std::vector<int8_t>   event_types;    ///< 0 photon, 1 marker

    /// Marker channel numbers, for the events with event_type 1.
    static constexpr int8_t MARKER_PIXEL = 1;
    static constexpr int8_t MARKER_LINE  = 2;
    static constexpr int8_t MARKER_FRAME = 3;
};

/*!
 * \brief Summary of a ``.ttr`` stream, without decoding it into events.
 *
 * Cheap enough to run over a whole file and answer the questions a caller
 * actually has before committing to a decode: how many records, which detector
 * IDs are present (so whether n_channels is plausible), how often the scanner
 * clocks fire.
 */
struct TtrStats {
    std::size_t n_words = 0;
    std::size_t n_filler = 0;          ///< 0x7FFF idle words
    std::size_t n_records = 0;         ///< words with ID == 127
    std::size_t n_photons = 0;         ///< valid detector words
    std::size_t n_pixel = 0, n_line = 0, n_frame = 0;
    int max_channel_id = -1;           ///< largest detector ID seen
    std::size_t n_laser_valid = 0;     ///< records whose laser word is valid
    uint64_t step_span = 0;            ///< unwrapped range of the coarse counter
};

/// Scan \p filename and report what is in it. Does not decode events.
TtrStats scan_ttr(const std::string& filename, const TtrParams& params = {});

/// Decode \p filename into per-event arrays.
TtrData read_ttr(const std::string& filename, const TtrParams& params = {});

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_BRIGHTEYES_H
