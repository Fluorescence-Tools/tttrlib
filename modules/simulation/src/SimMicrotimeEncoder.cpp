/*!
 * \file SimMicrotimeEncoder.cpp
 * \brief TCSPC record encoder implementation (see SimMicrotimeEncoder.h).
 *
 * The Becker & Hickl SPC-130/132 path is a faithful port of legacy
 * `data2spc132_tac` / `write_spc132_file`: kept arithmetically identical (same
 * double expressions, integer casts, and byte layout) so the output is
 * bit-identical to the legacy encoder for identical abstract records.
 */
#include "SimMicrotimeEncoder.h"
#include <cmath>
#include <stdexcept>

namespace tttrlib {

SimEncodedRecords SimMicrotimeEncoder::encode(
    const uint32_t* data_T,
    const double* data_t,
    const int16_t* data_N,
    const int16_t* data_species,
    const uint16_t* data_micro,
    uint64_t n_photons,
    SimRandom& rng,
    uint64_t mt_overflow_in) const {

    switch (format) {
        case SimRecordFormat::BeckerHicklSpc:
            return encode_becker_hickl(data_T, data_t, data_N, data_species, data_micro,
                                       n_photons, rng, mt_overflow_in);
        case SimRecordFormat::PicoQuantHt3:
        case SimRecordFormat::PicoQuantPtu:
            throw std::invalid_argument(
                "SimMicrotimeEncoder: PTU/HT3 output is produced via TTTR::write, "
                "not this encoder.");
    }
    throw std::invalid_argument("SimMicrotimeEncoder: unknown record format");
}

SimEncodedRecords SimMicrotimeEncoder::encode_becker_hickl(
    const uint32_t* data_T,
    const double* data_t,
    const int16_t* data_N,
    const int16_t* data_species,
    const uint16_t* data_micro,
    uint64_t n_photons,
    SimRandom& rng,
    uint64_t mt_overflow_in) const {

    SimEncodedRecords out;
    out.mt_overflow = mt_overflow_in;
    if (n_photons == 0) return out;

    const double SYNC_DT = laser_period * 1.e-6;
    const int MT_MAX_N = 0xfff;
    const int BYTEMASK = 0xff;
    const double MT_MAX_T = double(MT_MAX_N + 1) * SYNC_DT;
    const double MT_CALIB = microtime_resolution * 1.e-6;
    const double TWO32 = 4294967296.0;  // 2^32 (legacy: tw * 4294967296L)

    uint64_t& MT_ov = out.mt_overflow;
    uint32_t data_T_prev = data_T[0];
    double t_offset = 0.0;

    // Align the running time offset with the incoming overflow counter.
    while (t_offset + tw * double(data_T[0]) + data_t[0]
           < (double(MT_ov) - 1.0) * MT_MAX_T) {
        t_offset += tw * TWO32;
    }

    std::vector<uint8_t>& b = out.bytes;
    b.reserve(size_t(n_photons) * 4 + 64);

    for (uint64_t n = 0; n < n_photons; ++n) {
        // Genuine 32-bit macro-window wraparound (decrease > half the range).
        if (data_T[n] < data_T_prev && (data_T_prev - data_T[n]) > 2147483648UL)
            t_offset += tw * TWO32;

        double t = t_offset + tw * double(data_T[n]) + data_t[n];
        int MT = int(std::ceil((t - double(MT_ov) * MT_MAX_T) / SYNC_DT));

        if (MT > MT_MAX_N) {
            int MT_ov_last = MT / (MT_MAX_N + 1);
            b.push_back(uint8_t(MT_ov_last & BYTEMASK));
            b.push_back(uint8_t((MT_ov_last >> 8) & BYTEMASK));
            b.push_back(uint8_t((MT_ov_last >> 16) & BYTEMASK));
            b.push_back(uint8_t(((MT_ov_last >> 24) & 0x0f) + 0xc0));
            MT_ov += uint64_t(MT_ov_last);
            MT -= MT_ov_last * (MT_MAX_N + 1);
        }

        int tac;
        if (data_micro) {
            // Faithful path: carry the already-simulated micro-time (FLIM) channel, so the
            // read-back TAC — and any micro-time filter — matches the simulation exactly.
            tac = int(data_micro[n]);
            if (reverse_tac) tac = n_microtime_channels - tac - 1;
        } else if (pulsed_exc && data_species[n] >= 0 &&
                   data_species[n] * n_channels + data_N[n] >= 0 &&
                   size_t((data_species[n] * n_channels + data_N[n] + 1) * n_microtime_channels) <= F.size()) {
            size_t i_shift = size_t((data_species[n] * n_channels + data_N[n]) * n_microtime_channels);
            double r = rng.random0i1e();
            tac = lookup[i_shift + size_t(std::floor(r * n_microtime_channels))];
            while (F[i_shift + tac] < r) ++tac;
            // tac now holds the physical micro-time channel (delay after the pulse).
            if (reverse_tac) tac = n_microtime_channels - tac - 1;
        } else {
            double phase = std::fmod(t, SYNC_DT);  // physical sub-period delay
            tac = int(std::floor((reverse_tac ? (SYNC_DT - phase) : phase) / MT_CALIB));
        }
        if (tac < 0) tac = 0; else if (tac >= n_microtime_channels) tac = n_microtime_channels - 1;

        const int nn = int(data_N[n]);
        int N_spc = (nn >= 0 && nn < int(ch_conversion.size())) ? int(ch_conversion[nn]) : (nn & 0x3f);
        b.push_back(uint8_t(MT & BYTEMASK));
        b.push_back(uint8_t((N_spc << 4) + (MT >> 8)));
        b.push_back(uint8_t(tac & BYTEMASK));
        b.push_back(uint8_t(tac >> 8));

        data_T_prev = data_T[n];
    }
    return out;
}

std::vector<uint8_t> SimMicrotimeEncoder::file_bytes(
    const std::vector<uint8_t>& records) const {
    if (format != SimRecordFormat::BeckerHicklSpc) {
        throw std::invalid_argument(
            "SimMicrotimeEncoder::file_bytes: only the Becker & Hickl layout has a "
            "raw file header here; PTU/HT3 files are written via TTTR::write.");
    }
    std::vector<uint8_t> f;
    f.reserve(records.size() + 4);
    f.push_back(uint8_t(macro_time_clock & 0xFF));
    f.push_back(uint8_t((macro_time_clock >> 8) & 0xFF));
    f.push_back(uint8_t((macro_time_clock >> 16) & 0xFF));
    f.push_back(uint8_t(0x80));  // invalid=1, unused=0
    f.insert(f.end(), records.begin(), records.end());
    return f;
}

} // namespace tttrlib
