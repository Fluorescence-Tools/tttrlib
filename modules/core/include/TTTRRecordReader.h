// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PQ_H
#define TTTRLIB_PQ_H

// Validation: A/B-TESTED 2026-08-17 -- HydraHarp T2/T3 record decoding vs ptufile (Gohlke), SPC-130 and HT3 vs
//   phconvert (Ingargiola) -- macro time, micro time, channel and markers
//   identical on the tttr-data files. test/python/test_ab_core_reference.py,
//   test/python/tttr/test_t2_ptufile_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <iostream>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "TTTRRecordTypes.h"
#include "TTTRHeaderTypes.h"
#include "info.h"

// ============================================================================
// OPTIMIZED TEMPLATE-BASED RECORD PROCESSORS
// These replace function pointers with compile-time dispatch for maximum performance
// All 10 TTTR record types are optimized with inlined processing
// ============================================================================

// Base template declaration
template<int RecordType>
struct RecordProcessor {
    // Default implementation - should never be called (all types are specialized)
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time_or_marker,
        int16_t& channel,
        int16_t& record_type
    ) {
        return false;
    }
};

// Specialization for PicoHarp T3
template<>
struct RecordProcessor<PQ_RECORD_TYPE_PHT3> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time_or_marker,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int T3WRAPAROUND = 65536;
        pq_ph_t3_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.channel == 0xF) && (rec.bits.dtime == 0)) {
            overflow_counter += T3WRAPAROUND;
            return false;
        }
        
        if (rec.bits.dtime == 0) {
            record_type = RECORD_MARKER;
        } else {
            record_type = RECORD_PHOTON;
        }
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.n_sync;
        micro_time_or_marker = rec.bits.dtime;
        return true;
    }
};

// Specialization for PicoHarp T2
template<>
struct RecordProcessor<PQ_RECORD_TYPE_PHT2> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int T2WRAPAROUND = 210698240;
        pq_ph_t2_record_t rec;
        rec.allbits = TTTRRecord;
        
        if (rec.bits.channel == 0xF) {
            auto markers = static_cast<int16_t>(rec.bits.time & 0xF);
            if (markers == 0) {
                overflow_counter += T2WRAPAROUND;
                return false;
            }
            true_nsync = overflow_counter + rec.bits.time;
            channel = markers;
            record_type = RECORD_MARKER;
            return true;
        }
        
        true_nsync = overflow_counter + rec.bits.time;
        channel = static_cast<int16_t>(rec.bits.channel);
        record_type = RECORD_PHOTON;
        micro_time = 0;
        return true;
    }
};

// Specialization for HydraHarp T3 v1
template<>
struct RecordProcessor<PQ_RECORD_TYPE_HHT3v1> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time_or_marker,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int64_t T3WRAPAROUND = 1024;
        pq_hh_t3_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) {
            overflow_counter += T3WRAPAROUND;
            return false;
        }
        
        if (rec.bits.special == 1) {
            record_type = RECORD_MARKER;
        } else {
            record_type = RECORD_PHOTON;
        }
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.n_sync;
        micro_time_or_marker = rec.bits.dtime;
        return true;
    }
};

// Specialization for HydraHarp T3 v2
template<>
struct RecordProcessor<PQ_RECORD_TYPE_HHT3v2> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time_or_marker,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int64_t T3WRAPAROUND = 1024;
        pq_hh_t3_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) {
            overflow_counter += T3WRAPAROUND * rec.bits.n_sync;
            return false;
        }
        
        if (rec.bits.special == 1) {
            record_type = RECORD_MARKER;
        } else {
            record_type = RECORD_PHOTON;
        }
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.n_sync;
        micro_time_or_marker = rec.bits.dtime;
        return true;
    }
};

// Specialization for HydraHarp T2 v1
template<>
struct RecordProcessor<PQ_RECORD_TYPE_HHT2v1> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int64_t T2WRAPAROUND_V1 = 33552000;
        pq_hh_t2_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.special == 1) && (rec.bits.channel == 0x3F)) {
            overflow_counter += T2WRAPAROUND_V1;
            return false;
        }
        
        if (rec.bits.special == 1) {
            // channel 0 is the sync input: a genuine photon on channel 0
            // (matches PicoQuant ptudemo/ptufile). channels 1..15 are markers;
            // the 0x3F overflow record was already handled above.
            record_type = (rec.bits.channel == 0) ? RECORD_PHOTON : RECORD_MARKER;
            channel = static_cast<int16_t>(rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        }
        
        record_type = RECORD_PHOTON;
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.timetag;
        micro_time = 0;
        return true;
    }
};

// Specialization for HydraHarp T2 v2
template<>
struct RecordProcessor<PQ_RECORD_TYPE_HHT2v2> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int64_t T2WRAPAROUND_V2 = 33554432;
        pq_hh_t2_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) {
            // Number of overflows stored in timetag (new firmware style)
            if (rec.bits.timetag == 0) {
                overflow_counter += T2WRAPAROUND_V2;  // Old style single overflow
            } else {
                overflow_counter += T2WRAPAROUND_V2 * rec.bits.timetag;  // New style multiple overflows
            }
            return false;
        }
        
        if (rec.bits.special == 1) {
            // channel 0 is the sync input: a genuine photon on channel 0
            // (matches PicoQuant ptudemo/ptufile). channels 1..15 are markers;
            // the 0x3F overflow record was already handled above.
            record_type = (rec.bits.channel == 0) ? RECORD_PHOTON : RECORD_MARKER;
            channel = static_cast<int16_t>(rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        }
        
        record_type = RECORD_PHOTON;
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.timetag;
        micro_time = 0;
        return true;
    }
};

// Specialization for SF-compressed HT3 (Suren Felekyan's HT3 conversion).
// Photon/marker records are identical to HydraHarp T3; the overflow record
// (special=1, channel=0x3F) carries the number of ADDITIONAL overflows in
// its lowest 24 bits, i.e. it advances the sync counter by
// (1 + count) * 1024. A count of 0 decodes identically to a plain HHT3v1
// overflow record.
template<>
struct RecordProcessor<PQ_RECORD_TYPE_SF_HT3> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time_or_marker,
        int16_t& channel,
        int16_t& record_type
    ) {
        const uint64_t T3WRAPAROUND = 1024;
        pq_hh_t3_record_t rec;
        rec.allbits = TTTRRecord;

        if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) {
            overflow_counter += T3WRAPAROUND * (1 + (TTTRRecord & 0xFFFFFF));
            return false;
        }

        if (rec.bits.special == 1) {
            record_type = RECORD_MARKER;
        } else {
            record_type = RECORD_PHOTON;
        }
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.n_sync;
        micro_time_or_marker = rec.bits.dtime;
        return true;
    }
};

// Specialization for MultiHarp 150 / PicoHarp 330 T3 (Generic T3)
// Bit layout: [special:1][channel:6][dtime:15][n_sync:10]
// Identical to HHT3v2 but identified as a distinct record type
template<>
struct RecordProcessor<PQ_RECORD_TYPE_GENERIC_T3> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time_or_marker,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int64_t T3WRAPAROUND = 1024;
        pq_hh_t3_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) {
            // n_sync holds overflow count; 0 means legacy single overflow
            overflow_counter += T3WRAPAROUND * (rec.bits.n_sync == 0 ? 1 : rec.bits.n_sync);
            return false;
        }
        
        if (rec.bits.special == 1) {
            record_type = RECORD_MARKER;
        } else {
            record_type = RECORD_PHOTON;
        }
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.n_sync;
        micro_time_or_marker = rec.bits.dtime;
        return true;
    }
};

// Specialization for MultiHarp 150 / PicoHarp 330 T2 (Generic T2)
// Bit layout: [special:1][channel:6][timetag:25]
// Identical to HHT2v2 but identified as a distinct record type
template<>
struct RecordProcessor<PQ_RECORD_TYPE_GENERIC_T2> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        const int64_t T2WRAPAROUND_V2 = 33554432;
        pq_hh_t2_record_t rec;
        rec.allbits = TTTRRecord;
        
        if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) {
            // Number of overflows stored in timetag (new firmware style)
            if (rec.bits.timetag == 0) {
                overflow_counter += T2WRAPAROUND_V2;  // Old style single overflow
            } else {
                overflow_counter += T2WRAPAROUND_V2 * rec.bits.timetag;  // New style multiple overflows
            }
            return false;
        }
        
        if (rec.bits.special == 1) {
            // channel 0 is the sync input: a genuine photon on channel 0
            // (matches PicoQuant ptudemo/ptufile). channels 1..15 are markers;
            // the 0x3F overflow record was already handled above.
            record_type = (rec.bits.channel == 0) ? RECORD_PHOTON : RECORD_MARKER;
            channel = static_cast<int16_t>(rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        }
        
        record_type = RECORD_PHOTON;
        channel = static_cast<int16_t>(rec.bits.channel);
        true_nsync = overflow_counter + rec.bits.timetag;
        micro_time = 0;
        return true;
    }
};

// Specialization for Becker & Hickl SPC-130
template<>
struct RecordProcessor<BH_RECORD_TYPE_SPC130> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        bh_spc130_record_t rec;
        rec.allbits = TTTRRecord;
        
        // Case 1: Valid photon record (INVALID=0)
        if (!rec.bits.invalid) {
            overflow_counter += rec.bits.mtov;
            true_nsync = rec.bits.mt + overflow_counter * 4096;
            micro_time = static_cast<uint16_t>(4095 - rec.bits.adc);
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_PHOTON;
            return true;
        }
        
        // Case 2: Marker record (INVALID=1, MARK=1)
        // Routing bits encode marker type:
        //   rout bit 0 (value 1) = Pixel marker
        //   rout bit 1 (value 2) = Line marker
        //   rout bit 2 (value 4) = Frame marker
        if (rec.bits.mark) {
            overflow_counter += rec.bits.mtov;  // Handle overflow for markers too
            true_nsync = rec.bits.mt + overflow_counter * 4096;
            micro_time = rec.bits.rout;  // Store marker type in micro_time
            channel = static_cast<int16_t>(rec.bits.rout);  // Marker type as channel
            record_type = RECORD_MARKER;
            return true;
        }
        
        // Case 3: Overflow record (INVALID=1, MARK=0, MTOV=1)
        if (rec.bits.mtov) {
            bh_overflow_t overflow_record;
            overflow_record.allbits = TTTRRecord;
            overflow_counter += overflow_record.bits.cnt;
            return false;
        }
        
        // Case 4: Invalid record (INVALID=1, MARK=0, MTOV=0) - skip
        return false;
    }
};

// Specialization for Becker & Hickl SPC-QC-104 / QC-004 (2 bit channel)
template<>
struct RecordProcessor<BH_RECORD_TYPE_SPCQC_X04> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        bh_spcqc_record_t rec;
        rec.allbits = TTTRRecord;
        const unsigned event = rec.bits.type >> 2;  // bits 31-30

        // Macro time overflow: a bare 0x80000000 standing for one wrap of the
        // 12 bit macro time field. There is no count field to accumulate.
        if (event == BH_SPCQC_X04_TYPE_OVERFLOW) {
            overflow_counter += 1;
            return false;
        }

        true_nsync = rec.bits.mt + overflow_counter * BH_SPCQC_MT_WRAP;

        // Marker: the routing field carries the marker type, as on SPC-130,
        // and neither channel nor micro time are meaningful.
        if (event == BH_SPCQC_X04_TYPE_MARKER) {
            micro_time = rec.bits.rout;
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_MARKER;
            return true;
        }

        // Photon, or a GAP record -- which *is* a photon, only with a possible
        // FIFO overrun in front of it, so its fields are decoded the same way.
        // No reverse start-stop correction: SPCM already stores the micro time
        // the way it histograms it.
        micro_time = rec.bits.adc;
        // The detector is the input channel *and* the router signal, so keep
        // both, packed the way Becker & Hickl number QC detectors themselves:
        // routing in bits 3-0, input channel in bits 6-4 (see BH_SPCQC_CH_SHIFT).
        channel = static_cast<int16_t>(
                rec.bits.rout | ((rec.bits.type & 0x3) << BH_SPCQC_CH_SHIFT));
        record_type = RECORD_PHOTON;
        return true;
    }
};

// Specialization for Becker & Hickl SPC-QC-106 / QC-006 (3 bit channel)
template<>
struct RecordProcessor<BH_RECORD_TYPE_SPCQC_X06> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        bh_spcqc_record_t rec;
        rec.allbits = TTTRRecord;
        const unsigned event = rec.bits.type;  // bits 31-28

        if (event == BH_SPCQC_TYPE_OVERFLOW) {
            overflow_counter += 1;
            return false;
        }

        true_nsync = rec.bits.mt + overflow_counter * BH_SPCQC_MT_WRAP;

        if (event == BH_SPCQC_X06_TYPE_MARKER) {
            micro_time = rec.bits.rout;
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_MARKER;
            return true;
        }

        micro_time = rec.bits.adc;
        if (event & 0x8) {
            // Only GAP (11xx) remains; it is a photon, but with just the two
            // low channel bits -- the format has no room for Ch[2] there.
            if ((event & 0xC) != 0xC) return false;  // undefined selector, skip
            channel = static_cast<int16_t>(
                    rec.bits.rout | ((event & 0x3) << BH_SPCQC_CH_SHIFT));
        } else {
            channel = static_cast<int16_t>(
                    rec.bits.rout | ((event & 0x7) << BH_SPCQC_CH_SHIFT));
        }
        record_type = RECORD_PHOTON;
        return true;
    }
};

// Specialization for Becker & Hickl SPC-600 with 256 channels
template<>
struct RecordProcessor<BH_RECORD_TYPE_SPC600_256> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        // The macro time field is 17 bit wide; every overflow accounts for
        // 2**17 = 131072 macro time units.
        const uint64_t MT_WRAP = 131072;
        bh_spc600_256_record_t rec;
        rec.allbits = TTTRRecord;

        if (!rec.bits.mtov && !rec.bits.invalid) {
            true_nsync = rec.bits.mt + overflow_counter * MT_WRAP;
            micro_time = static_cast<uint16_t>(255 - rec.bits.adc);
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_PHOTON;
            return true;
        }

        if (!rec.bits.invalid && rec.bits.mtov) {
            overflow_counter += 1;
            true_nsync = rec.bits.mt + overflow_counter * MT_WRAP;
            micro_time = static_cast<uint16_t>(255 - rec.bits.adc);
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_PHOTON;
            return true;
        }

        if (rec.bits.invalid && rec.bits.mtov) {
            bh_overflow_t ovf;
            ovf.allbits = TTTRRecord;
            overflow_counter += ovf.bits.cnt;
            return false;
        }

        return false;
    }
};

// Specialization for Becker & Hickl SPC-600 with 4096 channels
template<>
struct RecordProcessor<BH_RECORD_TYPE_SPC600_4096> {
    // SPC-600/630 4096-channel records are 6 bytes wide; the macro time bytes
    // mt1/mt2 live in bytes 4-5, beyond a 32-bit load. Decode from the raw
    // record bytes (process_bytes) instead of a truncated 32-bit word.
    static inline bool process_bytes(
        const signed char* record_ptr,
        size_t bytes_per_record,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        bh_spc600_4096_record_t rec;
        std::memset(&rec, 0, sizeof(rec));
        std::memcpy(&rec, record_ptr,
                    bytes_per_record < sizeof(rec) ? bytes_per_record : sizeof(rec));

        if (!rec.bits.invalid) {
            uint32_t mt = rec.bits.mt1 +
                         (rec.bits.mt2 << 8) +
                         (rec.bits.mt3 << 16);
            true_nsync = mt + overflow_counter * 16777216;
            channel = static_cast<int16_t>(255 - rec.bits.rout);
            micro_time = static_cast<uint16_t>(4095 - rec.bits.adc);
            record_type = RECORD_PHOTON;
            return true;
        }

        overflow_counter += rec.bits.mtov;
        return false;
    }

    // 32-bit entry point kept for interface compatibility; loses mt1/mt2.
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        return process_bytes(
            reinterpret_cast<const signed char*>(&TTTRRecord), 4,
            overflow_counter, true_nsync, micro_time, channel, record_type);
    }
};

// Specialization for Carl Zeiss ConfoCor3 Raw
template<>
struct RecordProcessor<CZ_RECORD_TYPE_CONFOCOR3> {
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        cz_confocor3_raw_record_t rec;
        rec.allbits = TTTRRecord;
        
        true_nsync = rec.bits.mt + overflow_counter;
        micro_time = 1;
        record_type = 0;
        overflow_counter += rec.bits.mt;
        
        return true;
    }
};

// Template function for batch processing with compile-time dispatch
// Optimized with loop unrolling (4x) and branch prediction hints
// Standard version: outputs absolute macro times
template<int RecordType>
inline void process_records_batch(
    const signed char* buffer,
    size_t num_records,
    size_t bytes_per_record,
    uint64_t& overflow_counter,
    unsigned long long* macro_times,
    unsigned short* micro_times,
    signed char* routing_channels,
    signed char* event_types,
    size_t& valid_count
) {
    const signed char* record_ptr = buffer;

    // Per-record body, inlined into the unrolled loop below
    auto process_one = [&](const signed char* ptr) {
        uint64_t true_nsync;
        uint32_t micro_time;
        int16_t channel;
        int16_t record_type;
        bool is_valid;

        if constexpr (RecordType == BH_RECORD_TYPE_SPC600_4096) {
            // 6-byte records: decode from raw bytes (mt1/mt2 sit past 32 bit)
            is_valid = RecordProcessor<RecordType>::process_bytes(
                ptr, bytes_per_record, overflow_counter,
                true_nsync, micro_time, channel, record_type);
        } else {
            uint32_t record = *(uint32_t*)ptr;
            is_valid = RecordProcessor<RecordType>::process(
                record, overflow_counter,
                true_nsync, micro_time, channel, record_type);
        }

        // Branch hint: most records are valid
        if (is_valid) [[likely]] {
            macro_times[valid_count] = true_nsync;
            micro_times[valid_count] = static_cast<unsigned short>(micro_time);
            routing_channels[valid_count] = static_cast<signed char>(channel);
            event_types[valid_count] = static_cast<signed char>(record_type);
            valid_count++;
        }
    };

    // Process in blocks of 4 for better pipeline utilization
    size_t num_blocks = num_records / 4;
    size_t remainder = num_records % 4;

    // Unrolled loop for main processing
    for (size_t block = 0; block < num_blocks; block++) {
        process_one(record_ptr); record_ptr += bytes_per_record;
        process_one(record_ptr); record_ptr += bytes_per_record;
        process_one(record_ptr); record_ptr += bytes_per_record;
        process_one(record_ptr); record_ptr += bytes_per_record;
    }

    // Handle remaining records
    for (size_t j = 0; j < remainder; j++) {
        process_one(record_ptr);
        record_ptr += bytes_per_record;
    }
}

/*!
 * \brief Runtime-to-compile-time dispatch: decode a batch of records.
 *
 * Selects the process_records_batch specialization for a record type. Public
 * because a caller that wants to walk a record stream without materialising it
 * -- the PTO cue builder is the one that does -- needs the same decode TTTR
 * uses, and a second copy of it would be a second place for a record layout to
 * be wrong.
 *
 * \return false if the record type is unknown.
 */
inline bool dispatch_process_records_batch(
    int record_type,
    const signed char* buffer,
    size_t num_records,
    size_t bytes_per_record,
    uint64_t& overflow_counter,
    unsigned long long* macro_times,
    unsigned short* micro_times,
    signed char* routing_channels,
    signed char* event_types,
    size_t& valid_count
) {
    #define TTTRLIB_CASE_PROCESS(RT) \
        case RT: process_records_batch<RT>( \
            buffer, num_records, bytes_per_record, overflow_counter, \
            macro_times, micro_times, routing_channels, event_types, \
            valid_count); return true;
    switch(record_type) {
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_PHT3)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_PHT2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT3v1)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT3v2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT2v1)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT2v2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_GENERIC_T3)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_GENERIC_T2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_SF_HT3)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPC130)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPCQC_X04)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPCQC_X06)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPC600_256)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPC600_4096)
        TTTRLIB_CASE_PROCESS(CZ_RECORD_TYPE_CONFOCOR3)
        default:
            return false;
    }
    #undef TTTRLIB_CASE_PROCESS
}

// ============================================================================
// RECORD TYPE METADATA
// What a caller holding a buffer needs to know before it can decode one: how
// wide a record is, whether this library can decode it at all, and what to call
// it when it cannot. The three used to be spread over the file readers, where a
// caller without a file could not reach them.
// ============================================================================

/*!
 * \brief Bytes one record of \p record_type occupies in a stream, or 0.
 *
 * Zero means the encoding has no fixed width this library can state -- SM
 * records interleave two different word sizes, and a Photon-HDF5 container
 * stores decoded arrays rather than records.
 */
inline std::size_t record_bytes(int record_type) {
    switch (record_type) {
        case PQ_RECORD_TYPE_PHT3:
        case PQ_RECORD_TYPE_PHT2:
        case PQ_RECORD_TYPE_HHT3v1:
        case PQ_RECORD_TYPE_HHT3v2:
        case PQ_RECORD_TYPE_HHT2v1:
        case PQ_RECORD_TYPE_HHT2v2:
        case PQ_RECORD_TYPE_GENERIC_T3:
        case PQ_RECORD_TYPE_GENERIC_T2:
        case PQ_RECORD_TYPE_SF_HT3:
        case BH_RECORD_TYPE_SPC130:
        case BH_RECORD_TYPE_SPCQC_X04:
        case BH_RECORD_TYPE_SPCQC_X06:
        case BH_RECORD_TYPE_SPC600_256:
        case CZ_RECORD_TYPE_CONFOCOR3:
            return 4;
        case BH_RECORD_TYPE_SPC600_4096:
            return 6;
        case BE_RECORD_TYPE_TTR:
            return 2;   // a bare uint16 word stream
        case FL_RECORD_TYPE_STT1:
            return 17;  // {u8 event, f64 micro ns, f64 macro ns}
        case FL_RECORD_TYPE_ITT1:
            return 9;   // {u8 event, f64 time ns}
        default:
            return 0;
    }
}

/*!
 * \brief A stable name for \p record_type, e.g. "SPC-130".
 *
 * Used where a decode has to decline: "SM cannot be decoded from a buffer" is
 * an answer a caller can act on, and "record type 11" is not.
 */
inline std::string record_type_name(int record_type) {
    switch (record_type) {
        case PQ_RECORD_TYPE_PHT3:       return "PHT3";
        case PQ_RECORD_TYPE_PHT2:       return "PHT2";
        case PQ_RECORD_TYPE_HHT3v1:     return "HHT3v1";
        case PQ_RECORD_TYPE_HHT3v2:     return "HHT3v2";
        case PQ_RECORD_TYPE_HHT2v1:     return "HHT2v1";
        case PQ_RECORD_TYPE_HHT2v2:     return "HHT2v2";
        case PQ_RECORD_TYPE_GENERIC_T3: return "GENERIC_T3";
        case PQ_RECORD_TYPE_GENERIC_T2: return "GENERIC_T2";
        case PQ_RECORD_TYPE_SF_HT3:     return "SF_HT3";
        case BH_RECORD_TYPE_SPC130:     return "SPC-130";
        case BH_RECORD_TYPE_SPCQC_X04:  return "SPC-QC-x04";
        case BH_RECORD_TYPE_SPCQC_X06:  return "SPC-QC-x06";
        case BH_RECORD_TYPE_SPC600_256: return "SPC-600_256";
        case BH_RECORD_TYPE_SPC600_4096:return "SPC-600_4096";
        case CZ_RECORD_TYPE_CONFOCOR3:  return "CZ-CONFOCOR3";
        case SM_RECORD_TYPE:            return "SM";
        case BE_RECORD_TYPE_TTR:        return "BRIGHTEYES-TTR";
        case FL_RECORD_TYPE_STT1:       return "FLIMLABS-STT1";
        case FL_RECORD_TYPE_ITT1:       return "FLIMLABS-ITT1";
        default:                        return "record type " + std::to_string(record_type);
    }
}

/*!
 * \brief True if a buffer of these records can be decoded on its own.
 *
 * The encodings that cannot are not gaps in the dispatch table; each needs
 * something that is not in the record stream. A CZ ConfoCor3 record carries no
 * channel -- the header does. A BrightEyes word means nothing without the
 * instrument's sample clock. FLIM LABS records are read out of a JSON envelope
 * that says which of the two layouts they are. SM interleaves word widths.
 *
 * \note CZ-CONFOCOR3 *is* decodable here, and its channel comes back as
 *       whatever the caller left in the array, because the record does not
 *       carry one. See TTTR::backfill_cz_routing_channels.
 */
inline bool record_type_is_decodable(int record_type) {
    return record_bytes(record_type) > 0 &&
           record_type != BE_RECORD_TYPE_TTR &&
           record_type != FL_RECORD_TYPE_STT1 &&
           record_type != FL_RECORD_TYPE_ITT1;
}

/// Every record type \ref record_type_is_decodable accepts, ascending.
inline std::vector<int> decodable_record_types() {
    std::vector<int> out;
    for (int rt = 1; rt <= FL_RECORD_TYPE_ITT1; rt++)
        if (record_type_is_decodable(rt)) out.push_back(rt);
    return out;
}

#endif //TTTRLIB_PQ_H
