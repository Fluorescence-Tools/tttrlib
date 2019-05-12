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


#include "../include/TTTR.h"
#include "../test/tttr_test.h"
#include <iostream>
#include <string>
#include <include/RecordReader.h>


/*! Processes PicoHarp T3 Tags
 * @param TTTRRecord contains the data of the TTTR record
 * @param overflow_counter counts the number of overflows and
 * gets incremented in case the TTTR record contains an overflow
 * @param true_nsync counts the number of detected sync pulses
 * since the start of the recording
 * @param micro_time_or_marker conatins either the microtime
 * measuring the time between the sync pulses or contains marker
 * signals. For @param channel < 16 @param micro_time_or_marker is the
 * microtime. For @param channel > 15 @param micro_time_or_marker
 * contains marker.
 */
bool ProcessPHT3(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time_or_marker,
        int16_t &channel,
        int16_t &record_type
        ) {
    const int T3WRAPAROUND = 65536;
    pq_ph_t3_record_t rec;
    rec.allbits = TTTRRecord;
    if ((rec.bits.channel == 0xF) && (rec.bits.dtime == 0)) //this means we have a special record
    {
        overflow_counter += T3WRAPAROUND; // unwrap the time tag overflow
        return false;
    }
    else {
        if(rec.bits.dtime == 0){
            record_type = RECORD_MARKER;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.n_sync;
            micro_time_or_marker = rec.bits.dtime;
           return true;
        } else{
            record_type = RECORD_PHOTON;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.n_sync;
            micro_time_or_marker = rec.bits.dtime;
            return true;
        }
    }
}


/// PicoHarp T2 input
bool ProcessPHT2(
    uint64_t &TTTRRecord,
    uint64_t &overflow_counter,
    uint64_t &true_nsync,
    uint32_t &micro_time,
    int16_t &channel,
    int16_t &record_type
    ){
    const int T2WRAPAROUND = 210698240;
    pq_ph_t2_record_t rec;
    rec.allbits = TTTRRecord;
    if (rec.bits.channel == 0xF) //this means we have a special record
    {
        //in a special record the lower 4 bits of time are marker bits
        auto markers = (int16_t) (rec.bits.time & 0xF);
        if (markers == 0) //this means we have an overflow record
        {
            overflow_counter += T2WRAPAROUND; // unwrap the time tag overflow
            return false;
        } else //a marker
        {
            true_nsync = overflow_counter + rec.bits.time;
            channel = markers;
        }
    } else {
        true_nsync = overflow_counter + rec.bits.time;
        channel = (int16_t) rec.bits.channel;
        return true;
    }
        return false;
}


bool ProcessHHT2v1(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
        ) {
    const int64_t T2WRAPAROUND_V1 = 33552000;
    pq_hh_t2_record_t rec;
    rec.allbits = TTTRRecord;

    if ((rec.bits.special == 1) && (rec.bits.channel == 0x3F)) {
        overflow_counter += T2WRAPAROUND_V1;
        return false;
    }
    else {
        if(rec.bits.special == 1){
            record_type = RECORD_MARKER;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        } else{
            record_type = RECORD_PHOTON;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        }
    }
}


/// HydraHarp/TimeHarp260 T2 input
bool ProcessHHT2v2(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
        ) {

    const int64_t T2WRAPAROUND_V2 = 33554432;
    pq_hh_t2_record_t rec;
    rec.allbits = TTTRRecord;

    if ((rec.bits.channel == 0x3F) && (rec.bits.special == 1)) //an overflow record
    {
        //number of overflows is stored in timetag
        if (rec.bits.timetag == 0) //if it is zero it is an old style single overflow
        {
            overflow_counter += T2WRAPAROUND_V2;  //should never happen with new Firmware!
        } else {
            overflow_counter += T2WRAPAROUND_V2 * rec.bits.timetag;
        }
        return false;
    }
    else {
        if(rec.bits.special == 1){
            record_type = RECORD_MARKER;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        } else{
            record_type = RECORD_PHOTON;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.timetag;
            micro_time = 0;
            return true;
        }
    }
}



bool ProcessHHT3v2(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
) {
    const int T3WRAPAROUND = 1024;

    pq_hh_t3_record_t rec;
    rec.allbits = TTTRRecord;
    if ((rec.bits.special == 1) &&  (rec.bits.channel == 0x3F)) {
        overflow_counter += (int64_t) T3WRAPAROUND * rec.bits.n_sync;
        return false;
    }
    else {
        if(rec.bits.special == 1){
            record_type = RECORD_MARKER;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.n_sync;
            micro_time = rec.bits.dtime;
            return true;
        } else{
            record_type = RECORD_PHOTON;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.n_sync;
            micro_time = rec.bits.dtime;
            return true;
        }
    }
}


bool ProcessHHT3v1(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
) {
    const int T3WRAPAROUND = 1024;

    pq_hh_t3_record_t rec;
    rec.allbits = TTTRRecord;
    if ((rec.bits.special == 1) &&  (rec.bits.channel == 0x3F)) {
        overflow_counter += (int64_t) T3WRAPAROUND;
        return false;
    }
    else {
        if(rec.bits.special == 1){
            record_type = RECORD_MARKER;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.n_sync;
            micro_time = 0;
            return true;
        } else{
            record_type = RECORD_PHOTON;
            channel = (uint16_t) (rec.bits.channel);
            true_nsync = overflow_counter + rec.bits.n_sync;
            micro_time = rec.bits.dtime;
            return true;
        }
    }
}


bool ProcessSPC130(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
        ){
    bh_spc130_record_t rec;
    rec.allbits = TTTRRecord;

    if(!rec.bits.mtov && !rec.bits.invalid){
        // normal record
        true_nsync = rec.bits.mt + overflow_counter * 4096;
        micro_time = (uint16_t) (4095 - rec.bits.adc);
        channel = (uint16_t) (rec.bits.rout);
        return true;
    } else{
        if(!rec.bits.invalid && rec.bits.mtov){
            // valid record with a single macro time overflow
            overflow_counter += 1;
            true_nsync = rec.bits.mt + overflow_counter * 4096; // 4096 = 2**12 (12 bits for macro time counter)
            micro_time = (uint16_t) (4095 - rec.bits.adc);
            channel = (uint16_t) (rec.bits.rout);
            return false;
        } else{
            if(rec.bits.invalid && rec.bits.mtov){
                bh_overflow_t overflow_record;
                overflow_record.allbits = TTTRRecord;
                overflow_counter += overflow_record.bits.cnt;
                return false;
            }
        }
    }
    return false;
}


bool ProcessSPC600_4096(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
        ){
    bh_spc600_4096_record_t rec;
    rec.allbits = TTTRRecord;
    uint32_t mt = 0;
    if(!rec.bits.invalid){
        // normal record
        mt = rec.bits.mt1 +
             (rec.bits.mt2 << 8) +
             (rec.bits.mt3 << 16);
        true_nsync = mt + overflow_counter * 16777216;
        channel = (int16_t) (255 - rec.bits.rout);
        micro_time = (uint16_t) (4095 - rec.bits.adc);
        return true;
    } else
    {
        overflow_counter += rec.bits.mtov;
        return false;
    }
}


bool ProcessSPC600_256(
        uint64_t &TTTRRecord,
        uint64_t &overflow_counter,
        uint64_t &true_nsync,
        uint32_t &micro_time,
        int16_t &channel,
        int16_t &record_type
        ){
    bh_spc600_256_record_t rec;
    rec.allbits = TTTRRecord;

    if(!rec.bits.mtov && !rec.bits.invalid){
        // normal record
        true_nsync = rec.bits.mt + overflow_counter * 4096;
        micro_time = (uint16_t) (255 - rec.bits.adc);
        channel = (uint16_t) (rec.bits.rout);
        return true;
    } else{
        if(!rec.bits.invalid && rec.bits.mtov){
            // valid record with a single macro time overflow
            overflow_counter += 1;
            true_nsync = rec.bits.mt + overflow_counter * 65536; // 65536 = 2**16 (16 bits for macro time counter)
            micro_time = (uint16_t) (255 - rec.bits.adc);
            channel = (uint16_t) (rec.bits.rout);
            return true;
        } else{
            if(rec.bits.invalid && rec.bits.mtov){
                bh_overflow_t ovf;
                ovf.allbits = TTTRRecord;
                overflow_counter += ovf.bits.cnt;
                return false;
            }
        }
    }
    return false;
}



