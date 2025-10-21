#ifndef TTTRLIB_PQ_H
#define TTTRLIB_PQ_H

#include <iostream>
#include <cstdint>
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
            record_type = RECORD_MARKER;
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
            overflow_counter += T2WRAPAROUND_V2;
            return false;
        }
        
        if (rec.bits.special == 1) {
            record_type = RECORD_MARKER;
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
        
        if (!rec.bits.invalid) {
            overflow_counter += rec.bits.mtov;
            true_nsync = rec.bits.mt + overflow_counter * 4096;
            micro_time = static_cast<uint16_t>(4095 - rec.bits.adc);
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_PHOTON;
            return true;
        }
        
        if (rec.bits.invalid && rec.bits.mtov) {
            bh_overflow_t overflow_record;
            overflow_record.allbits = TTTRRecord;
            overflow_counter += overflow_record.bits.cnt;
            return false;
        }
        
        return false;
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
        bh_spc600_256_record_t rec;
        rec.allbits = TTTRRecord;
        
        if (!rec.bits.mtov && !rec.bits.invalid) {
            true_nsync = rec.bits.mt + overflow_counter * 4096;
            micro_time = static_cast<uint16_t>(255 - rec.bits.adc);
            channel = static_cast<int16_t>(rec.bits.rout);
            record_type = RECORD_PHOTON;
            return true;
        }
        
        if (!rec.bits.invalid && rec.bits.mtov) {
            overflow_counter += 1;
            true_nsync = rec.bits.mt + overflow_counter * 65536;
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
    static inline bool process(
        uint32_t& TTTRRecord,
        uint64_t& overflow_counter,
        uint64_t& true_nsync,
        uint32_t& micro_time,
        int16_t& channel,
        int16_t& record_type
    ) {
        bh_spc600_4096_record_t rec;
        rec.allbits = TTTRRecord;
        
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
    
    // Process in blocks of 4 for better pipeline utilization
    size_t num_blocks = num_records / 4;
    size_t remainder = num_records % 4;
    
    // Unrolled loop for main processing
    for (size_t block = 0; block < num_blocks; block++) {
        // Process 4 records per iteration
        for (int i = 0; i < 4; i++) {
            uint32_t record = *(uint32_t*)record_ptr;
            uint64_t true_nsync;
            uint32_t micro_time;
            int16_t channel;
            int16_t record_type;
            
            bool is_valid = RecordProcessor<RecordType>::process(
                record,
                overflow_counter,
                true_nsync,
                micro_time,
                channel,
                record_type
            );
            
            // Branch hint: most records are valid
            if (is_valid) [[likely]] {
                macro_times[valid_count] = true_nsync;
                micro_times[valid_count] = static_cast<unsigned short>(micro_time);
                routing_channels[valid_count] = static_cast<signed char>(channel);
                event_types[valid_count] = static_cast<signed char>(record_type);
                valid_count++;
            }
            
            record_ptr += bytes_per_record;
        }
    }
    
    // Handle remaining records
    for (size_t j = 0; j < remainder; j++) {
        uint32_t record = *(uint32_t*)record_ptr;
        uint64_t true_nsync;
        uint32_t micro_time;
        int16_t channel;
        int16_t record_type;
        
        bool is_valid = RecordProcessor<RecordType>::process(
            record,
            overflow_counter,
            true_nsync,
            micro_time,
            channel,
            record_type
        );
        
        if (is_valid) [[likely]] {
            macro_times[valid_count] = true_nsync;
            micro_times[valid_count] = static_cast<unsigned short>(micro_time);
            routing_channels[valid_count] = static_cast<signed char>(channel);
            event_types[valid_count] = static_cast<signed char>(record_type);
            valid_count++;
        }
        
        record_ptr += bytes_per_record;
    }
}

#endif //TTTRLIB_PQ_H
