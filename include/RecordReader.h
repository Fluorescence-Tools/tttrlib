/****************************************************************************
 * Copyright (C) 2019 by Thomas-Otavio Peulen                               *
 *                                                                          *
 * This file is part of the library tttrlib.                                *
 *                                                                          *
 *   tttrlib is free software: you can redistribute it and/or modify it     *
 *   under the terms of the MIT License.                                    *
 *                                                                          *
 *   tttrlib is distributed in the hope that it will be useful,             *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                   *
 *                                                                          *
 ****************************************************************************/


#ifndef TTTRLIB_PQ_H
#define TTTRLIB_PQ_H

#include <iostream>
#include <stdint.h>
#include "RecordTypes.h"


/*********************************************/
/***                                       ***/
/***                HT3 HHv1.0             ***/
/***  https://github.com/tsbischof/libpicoquant/blob/master/src/hydraharp/hh_v20.h                       ***/
/*********************************************/


bool ProcessSPC130(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessSPC600_4096(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessSPC600_256(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessHHT2v2(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessHHT2v1(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessHHT3v2(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);


bool ProcessHHT3v1(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessPHT3(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

bool ProcessPHT2(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
);

#endif //TTTRLIB_PQ_H
