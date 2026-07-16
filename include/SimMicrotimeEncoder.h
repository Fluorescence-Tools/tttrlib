/*!
 * \file SimMicrotimeEncoder.h
 * \brief Encode abstract simulated photon records (macro-window + arrival time +
 *        channel) into hardware TCSPC records (PRD-005).
 *
 * The target record layout is selected via `SimRecordFormat`; the method names are
 * format-agnostic. Only the Becker & Hickl SPC-130/132 layout is realised here (a
 * faithful port of the legacy `data2spc_tac`, byte-identical for identical abstract
 * records — a development-time correctness gate). PicoQuant PTU/HT3 output is
 * produced by the existing `TTTR::write` instead. Additive; does not modify any
 * existing tttrlib class.
 */
#ifndef TTTRLIB_SIMMICROTIMEENCODER_H
#define TTTRLIB_SIMMICROTIMEENCODER_H

#include <cstdint>
#include <vector>
#include "SimRandom.h"

namespace tttrlib {

/// Hardware record layout to emit. Values name real formats; the API stays generic.
enum class SimRecordFormat {
    BeckerHicklSpc,   ///< B&H SPC-130/132 4-byte records (implemented here)
    PicoQuantHt3,     ///< PicoQuant HydraHarp T3 — produced via TTTR::write, not here
    PicoQuantPtu      ///< PicoQuant PTU (T3) — produced via TTTR::write, not here
};

/// Raw encoded record bytes plus the running macro-time-overflow counter.
struct SimEncodedRecords {
    std::vector<uint8_t> bytes;
    uint64_t mt_overflow = 0;   ///< total macro-time overflows accumulated (for streaming)
};

/*!
 * \brief Configurable encoder from abstract simulated photons to TCSPC records.
 *
 * Fields mirror the legacy `data2spc_tac` parameters. The CW path (`pulsed_exc==0`)
 * derives the micro-time (TAC) deterministically from the sub-period phase and uses
 * no RNG; the pulsed path samples the TAC from an inverse-CDF (`F`/`lookup`).
 *
 * `reverse_tac` selects whether the TAC is written in B&H reverse start-stop order;
 * set it to `true` to reproduce the legacy `data2spc_tac` output byte-for-byte.
 */
class SimMicrotimeEncoder {
public:
    SimRecordFormat format = SimRecordFormat::BeckerHicklSpc;

    int pulsed_exc = 0;                     ///< 0 = CW (deterministic micro-time), 1 = pulsed (inverse lookup)
    int n_channels = 2;                     ///< number of detection channels
    double tw = 0.01;                       ///< macro-window (original time-window) length
    std::vector<uint16_t> ch_conversion;    ///< channel i -> hardware channel ch_conversion[i]
    int n_microtime_channels = 4096;        ///< number of micro-time channels
    double microtime_resolution = 0.004069; ///< ns per micro-time channel
    double laser_period = 13.596;           ///< ns
    bool reverse_tac = false;               ///< false (default) = write the physical micro-time directly. true = write reversed TAC (B&H reverse start-stop: raw ADC = n_microtime_channels-1 - micro_time); the SPC reader un-reverses, so a to_tttr round-trip needs true (SimEngine.to_tttr passes it by default).
    std::vector<double> F;                  ///< pulsed: integrated p(t) (inverse-CDF), else empty
    std::vector<int> lookup;                ///< pulsed: start-index lookup, else empty
    uint32_t macro_time_clock = 100;        ///< value written into the file header

    /*!
     * \brief Encode photon records into the configured `format`'s hardware records.
     * \param mt_overflow_in initial overflow counter (for streaming continuation).
     * \throws std::invalid_argument for formats produced via `TTTR::write` (PTU/HT3).
     */
    /// \param data_micro optional simulated micro-time channel per photon. When non-null the TAC
    ///        is taken directly from it (faithfully preserving the FLIM/lifetime axis and any
    ///        micro-time filters); when null the TAC is derived (pulsed inverse-CDF or CW phase).
    SimEncodedRecords encode(
        const uint32_t* data_T,
        const double* data_t,
        const int16_t* data_N,
        const int16_t* data_species,
        const uint16_t* data_micro,
        uint64_t n_photons,
        SimRandom& rng,
        uint64_t mt_overflow_in = 0) const;

    /// Prepend the configured format's file header to a raw record byte stream.
    std::vector<uint8_t> file_bytes(const std::vector<uint8_t>& records) const;

private:
    SimEncodedRecords encode_becker_hickl(
        const uint32_t* data_T, const double* data_t,
        const int16_t* data_N, const int16_t* data_species, const uint16_t* data_micro,
        uint64_t n_photons, SimRandom& rng, uint64_t mt_overflow_in) const;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMMICROTIMEENCODER_H
