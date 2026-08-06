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
 *
 * \section ttr_micro Two kinds of micro time
 *
 * By default a photon's micro time is its raw TDC code, differenced against the
 * laser code of its own record when there is one. That is what the file
 * contains and nothing more, and it is not a duration: the delay line's taps
 * are not equally wide.
 *
 * Supply a \ref TtrCalibration -- or ask for one to be measured, see
 * \ref calibrate_ttr -- and micro times become picoseconds after the exciting
 * pulse. Three things change, and each of them is a decision the raw path
 * deliberately does not make:
 *
 * 1. Codes are converted through a measured per-channel code-density table
 *    rather than a scale factor.
 * 2. A photon is referenced to the **next** valid laser word, which is usually
 *    not the one in its own record: in the published 80 MHz sample only 27 % of
 *    photons have a laser word alongside them.
 * 3. The TDC counts backwards -- it measures how long until the next clock edge
 *    -- so the arrival time is the laser period minus that interval. Without
 *    the flip the decay comes out mirrored.
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

    /// Laser repetition rate in MHz. Used once the TDC is calibrated, as the
    /// period arrival times fold into; left at 0 they fold into one sample
    /// clock period instead, which is correct but is not what a decay is
    /// usually plotted against. Ignored entirely on the raw-code path.
    double laser_MHz = 0.0;

    /*!
     * Picoseconds per TDC code: the vendor's crude fallback, as a single scale.
     *
     * The payload is a tapped-delay-line code whose bins are not equal, so one
     * number cannot describe it; \ref calibrate_ttr measures the widths
     * instead. Set, this behaves exactly like a measured calibration in every
     * other respect -- the laser pairing and the direction of time are
     * properties of the hardware, not of how the codes were scaled -- so it is
     * an approximate calibration rather than a different kind of answer.
     *
     * Left at 0 with no \ref TtrCalibration supplied, micro times stay in raw
     * codes, which is honest: an uncalibrated code is not a time, and
     * pretending otherwise produces a plausible-looking wrong answer.
     */
    double tdc_ps_per_code = 0.0;

    /*!
     * Measure the delay line from the data before decoding.
     *
     * An estimation step, not parsing, which is why it is opt-in and named: the
     * bin widths are read off the code histogram of the file itself, so the
     * same file read over different subranges gives slightly different times.
     * Equivalent to calling \ref calibrate_ttr and passing the result to
     * \ref read_ttr.
     */
    bool auto_calibrate_tdc = false;

    /// Drop the 0x7FFF idle word before parsing.
    bool drop_filler = true;
};

/*!
 * \brief Read \p json (an object) into a \ref TtrParams.
 *
 * The properties are those declared by the ``BRIGHTEYES-TTR`` entry of the
 * ``file_container`` registry category, which is how a caller in any language
 * discovers them. An empty or blank string leaves every default in place.
 *
 * \throws std::invalid_argument if \p json is not an object, if a property is
 *         not one this format has, or if a value is of the wrong type or out of
 *         range. A misspelled parameter that silently did nothing would give a
 *         wrong answer that looks like a right one.
 */
TtrParams ttr_params_from_json(const std::string& json);

/*!
 * \brief A measured conversion from TDC codes to picoseconds.
 *
 * The delay line's taps are not equally wide, and the widths differ per
 * channel, so the conversion is a table rather than a factor. It is recovered
 * by code density: over one sample-clock period the arrival phase is uniform,
 * so the number of counts falling in a tap is proportional to that tap's width.
 * Integrating the normalised widths gives the time at the centre of each code.
 *
 * This is an estimate from the data, with all that implies. Rare channels get
 * noisy tables, and a channel whose codes are not uniformly illuminated -- a
 * strongly pulsed source at a harmonic of the sample clock -- gets a biased
 * one. It is nonetheless the conversion the instrument's own toolchain uses.
 */
struct TtrCalibration {
    /// ``channel_code_ps[channel][code]`` -- picoseconds into the sample-clock
    /// period. Channels with no photons get an empty row.
    std::vector<std::vector<double>> channel_code_ps;

    /// ``laser_code_ps[code]`` -- the same, for the laser reference (ID 124).
    std::vector<double> laser_code_ps;

    /// Sample-clock period the table integrates to.
    double sysclk_ps = 0.0;

    /// Laser period, or 0 if unknown. Arrival times fold into this.
    double laser_ps = 0.0;

    /// Width of one micro time bin: the nominal TDC least-significant bit,
    /// i.e. the sample-clock period over the 8-bit code space.
    double micro_time_bin_ps = 0.0;

    /// Number of micro time bins spanning one laser period (one sample-clock
    /// period when the laser rate is unknown).
    int n_micro_time_channels = 0;

    bool empty() const { return laser_code_ps.empty(); }
};

/*!
 * \brief Measure \p filename's delay line, per channel.
 *
 * A full pass over the file that decodes nothing: it only histograms TDC codes.
 * Pass the result to \ref read_ttr, or set
 * \ref TtrParams::auto_calibrate_tdc to have the reader do both.
 *
 * \p params supplies ``sysclk_MHz`` -- what the code space integrates to -- and
 * ``laser_MHz``, which sets the period arrival times fold into. Without the
 * latter they fold into one sample-clock period instead, which is correct but
 * not what a decay is usually plotted against.
 */
TtrCalibration calibrate_ttr(const std::string& filename, const TtrParams& params = {});

/*!
 * \brief One decoded ``.ttr`` stream.
 *
 * Laid out as tttrlib's four per-event arrays, so it drops into the same model
 * as every other container: macro time, micro time, routing channel, event type
 * (0 photon, 1 marker).
 */
struct TtrData {
    std::vector<uint64_t> macro_times;    ///< sample-clock ticks, unwrapped
    std::vector<uint16_t> micro_times;    ///< raw TDC codes, or bins of \ref micro_time_bin_ps once calibrated
    std::vector<int8_t>   routing_channels;
    std::vector<int8_t>   event_types;    ///< 0 photon, 1 marker

    /// Whether the micro times are times at all. False means raw delay-line
    /// codes, which are not proportional to a duration; see \ref ttr_micro.
    bool micro_times_calibrated = false;

    /// Width of one micro time bin, once calibrated. 0 otherwise.
    double micro_time_bin_ps = 0.0;

    /// Span of the micro time axis, in bins. 256 for raw codes.
    int n_micro_time_channels = 256;

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

/*!
 * \brief Decode \p filename into per-event arrays.
 *
 * With no calibration -- neither \p calibration, nor
 * \ref TtrParams::auto_calibrate_tdc, nor \ref TtrParams::tdc_ps_per_code --
 * micro times come out as raw TDC codes and \ref TtrData::micro_times_calibrated
 * is false. See \ref ttr_micro for what changes when one is supplied.
 */
TtrData read_ttr(const std::string& filename, const TtrParams& params = {},
                 const TtrCalibration* calibration = nullptr);

/*!
 * \brief Encode per-event arrays as a ``.ttr`` stream and write it.
 *
 * \section ttr_write_exact What survives, exactly
 *
 * Macro times, to the tick, including the absolute offset. The counter in the
 * file is 16 bits and the reader unwraps it by counting decreases, so the
 * writer emits empty records -- records the hardware itself emits whenever a
 * sample clock ticks and nothing happens -- to carry the counter across every
 * gap larger than one wrap. A file that begins 2.9e9 ticks in costs about 44000
 * of them, which is six words each.
 *
 * Routing channels, and the number of events.
 *
 * \section ttr_write_lossy What does not
 *
 * Micro times are saturated to 8 bits, because that is the width of the TDC
 * payload. Saturation rather than truncation, matching the other narrow
 * containers: a photon that arrived late reads as late, instead of aliasing
 * back to early.
 *
 * Order within a single macro tick is normalised to photons, then pixel, line
 * and frame markers -- the order a record is decoded in. Events at the same
 * tick have no defined order in the format, so there is nothing to preserve.
 *
 * \section ttr_write_refuse What it refuses
 *
 * A photon on a channel the device does not have (outside
 * ``0..n_channels-1``), or a marker that is not the pixel, line or frame clock.
 * There is no field for either, and writing a file that quietly lacks them is
 * worse than not writing one.
 *
 * \throws std::runtime_error if the file cannot be written, or if the events
 *         cannot be represented.
 */
void write_ttr(const std::string& filename,
               const uint64_t* macro_times,
               const uint16_t* micro_times,
               const int8_t* routing_channels,
               const int8_t* event_types,
               std::size_t n_events,
               const TtrParams& params = {});

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_BRIGHTEYES_H
