//
// Created by thomas on 5/11/19.
//

#ifndef TTTRLIB_RECORDTYPES_H
#define TTTRLIB_RECORDTYPES_H

#include <stdint.h>


/// HydraHarp/TimeHarp260 T2 record
typedef union pq_hh_t2_record {
    uint64_t allbits;
    struct {
        unsigned timetag  :25;
        unsigned channel  :6;
        unsigned special  :1; // or sync, if channel==0
    } bits;
} pq_hh_t2_record_t;

/// HydraHarp/TimeHarp260 T3 record
typedef union pq_hh_t3_record {
    uint64_t allbits;
    struct {
        unsigned n_sync    :10;    // number of sync period
        unsigned dtime    :15;    // delay from last sync in units of chosen macrotime_resolution
        unsigned channel  :6;
        unsigned special  :1;
    } bits;
} pq_hh_t3_record_t;

/// PicoHarp T2 input
typedef union ph_ph_t2_record {
    uint64_t allbits;
    struct {
        unsigned time     :28;
        unsigned channel  :4;
    } bits;

} pq_ph_t2_record_t;

/// PicoHarp T3 input
typedef union pq_ph_t3_record {
    uint64_t allbits;
    struct {
        unsigned n_sync   :16;
        unsigned dtime    :12;
        unsigned channel  :4;
    } bits;
} pq_ph_t3_record_t;


/// Becker Hickl SPC-600/630 256 Channel Mode, regular record
typedef union bh_spc600_256_record{
    uint64_t allbits;
    struct {
        unsigned adc     : 8; //
        unsigned mt      : 17;
        unsigned rout    : 3;
        unsigned empty   : 1;
        unsigned gap     : 1;
        unsigned mtov    : 1;
        unsigned invalid : 1;
    } bits;
} bh_spc600_256_record_t;


/// Becker Hickl SPC-600/630 4096 Channel Mode.
typedef union bh_spc600_4096_record{
/// The information about the subsequent photons is stored one after another in the measurement
/// data file. For each photon 6 bytes are used. The parameter @param adc corresponds to the value of
/// the analog to digital converter and related to the micro time (mt) by
/// mt = (4095 - adc) * TACRange / (TACGain * 4096)
/// @param invalid is true for invalid TTTR records. If invalid is true all data in the record
/// except the mtov bit are invalid. @param mtov marks that a macro timer overflows. In case @param mtov
/// is true 2**24 counts should be added to the overflow counter (the overflow counter counts the
/// number of overflows. @param gap marks possible gaps, e.g., due to FIFO overflows. @param rout
/// provides the (inverted) routing number of the TTTR record.
    uint64_t allbits;
    struct {
        unsigned adc     : 12;
        unsigned invalid : 1;
        unsigned mtov    : 1;
        unsigned gap     : 1;
        unsigned empty   : 1;
        unsigned mt3     : 8;
        unsigned rout    : 8;
        unsigned mt1     : 8;
        unsigned mt2     : 8;
    } bits;
} bh_spc600_4096_record_t;


/// Becker Hickl SPC-130, regular record
typedef union bh_spc130_record{
    uint64_t allbits;
    struct {
        unsigned mt       : 12;
        unsigned rout     : 4;
        unsigned adc      : 12;
        unsigned mark     : 1;
        unsigned gap      : 1;
        unsigned mtov     : 1;
        unsigned invalid  : 1;
    } bits;
} bh_spc130_record_t;


/// Becker Hickl SPC-130/600 macro time overflow record
typedef union bh_overflow{
    uint64_t allbits;
    struct {
        unsigned cnt        : 28;
        unsigned empty      : 6;
        unsigned mtov       : 1;
        unsigned invalid    : 1;
    } bits;
} bh_overflow_t;



#endif //TTTRLIB_RECORDTYPES_H
