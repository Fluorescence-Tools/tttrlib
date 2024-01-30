#ifndef TTTRLIB_PQ_H
#define TTTRLIB_PQ_H

#include <iostream>
#include <cstdint>
#include "TTTRRecordTypes.h"



bool ProcessCzRaw(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessSPC130(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessSPC600_4096(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessSPC600_256(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

/* See:  https://github.com/tsbischof/libpicoquant/blob/master/src/hydraharp/hh_v20.h                       ***/

bool ProcessHHT2v2(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessHHT2v1(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessHHT3v2(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);


bool ProcessHHT3v1(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessPHT3(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessPHT2(
        uint32_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

#endif //TTTRLIB_PQ_H
