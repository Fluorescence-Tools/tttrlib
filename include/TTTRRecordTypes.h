// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TTTRRECORDTYPES_H
#define TTTRLIB_TTTRRECORDTYPES_H

#include <cstdint>

// SM files
typedef union sm_record {
    uint64_t time;
    uint32_t channel;
} sm_record_t;


// HydraHarp/TimeHarp260 T2 record
typedef union pq_hh_t2_record {
    uint32_t allbits;
    struct {
        unsigned timetag  :25;
        unsigned channel  :6;
        unsigned special  :1; // or sync, if channel==0
    } bits;
} pq_hh_t2_record_t;


// HydraHarp/TimeHarp260 T3 record
typedef union pq_hh_t3_record {
    uint32_t allbits;
    struct {
        unsigned n_sync    :10;    // number of sync period
        unsigned dtime    :15;     // delay from last sync in units of chosen macrotime_resolution
        unsigned channel  :6;
        unsigned special  :1;
    } bits;
} pq_hh_t3_record_t;

// PicoHarp T2 input
typedef union ph_ph_t2_record {
    uint32_t allbits;
    struct {
        unsigned time     :28;
        unsigned channel  :4;
    } bits;

} pq_ph_t2_record_t;

// PicoHarp T3 input
typedef union pq_ph_t3_record {
    uint32_t allbits;
    struct {
        unsigned n_sync   :16;
        unsigned dtime    :12;
        unsigned channel  :4;
    } bits;
} pq_ph_t3_record_t;


// Becker Hickl SPC-600/630 256 Channel Mode, regular record
typedef union bh_spc600_256_record{
    uint32_t allbits;
    struct {
        unsigned adc     :8; //
        unsigned mt      :17;
        unsigned rout    :3;
        unsigned empty   :1;
        unsigned gap     :1;
        unsigned mtov    :1;
        unsigned invalid :1;
    } bits;
} bh_spc600_256_record_t;


// Becker Hickl SPC-600/630 4096 Channel Mode.
// The information about the subsequent photons is stored one after another in the measurement
// data file. For each photon 6 bytes are used. The parameter @param adc corresponds to the value of
// the analog to digital converter and related to the micro time (mt) by
// mt = (4095 - adc) * TACRange / (TACGain * 4096)
// @param invalid is true for invalid TTTR records. If invalid is true all data in the record
// except the mtov bit are invalid. @param mtov marks that a macro timer overflows. In case @param mtov
// is true 2**24 counts should be added to the overflow counter (the overflow counter counts the
// number of overflows. @param gap marks possible gaps, e.g., due to FIFO overflows. @param rout
// provides the (inverted) routing number of the TTTR record.
typedef union bh_spc600_4096_record{
    uint32_t allbits;
    struct {
        unsigned adc     :12;
        unsigned invalid :1;
        unsigned mtov    :1;
        unsigned gap     :1;
        unsigned empty   :1;
        unsigned mt3     :8;
        unsigned rout    :8;
        unsigned mt1     :8;
        unsigned mt2     :8;
    } bits;
} bh_spc600_4096_record_t;


// Becker Hickl SPC-130, regular record
typedef union bh_spc130_record{
    uint32_t allbits;
    struct {
        unsigned mt       :12;
        unsigned rout     :4;
        unsigned adc      :12;
        unsigned mark     :1;
        unsigned gap      :1;
        unsigned mtov     :1;
        unsigned invalid  :1;
    } bits;
} bh_spc130_record_t;


/*!
 * \brief Becker & Hickl SPC-QC records (QC-104/004 and QC-106/006)
 *
 * The QC modules use a layout of their own that only superficially resembles
 * the SPC-130 record. The lower 28 bit are common to both flavours and to every
 * event kind; the top four bits select what the event is, and how wide the
 * input-channel field is differs between QC-x04 and QC-x06.
 *
 *  * bit  0-11  macro time, low 12 bit. The high bits come from the overflow
 *               records, one of which accounts for 4096 macro time units.
 *  * bit 12-15  routing signal (marker type on marker records). The number of
 *               routing bits actually in use is in the file header.
 *  * bit 16-27  micro time (12 bit ADC). Unlike every classic SPC card the
 *               value is *not* reversed -- it is the micro time the way SPCM
 *               histograms it, so 0x000 is 0 ns.
 *
 * QC-x04 (SPC-QC-104 / QC-004), bits 31-30 select the event, bits 29-28 hold
 * the two channel bits:
 *
 *  | 31 | 30 | event                                                        |
 *  |----|----|--------------------------------------------------------------|
 *  |  0 |  0 | photon                                                       |
 *  |  1 |  0 | macro time overflow; every other bit is zero by definition    |
 *  |  0 |  1 | marker, type in bits 15-12, macro time valid                  |
 *  |  1 |  1 | GAP: a photon, but a FIFO overrun may precede it              |
 *
 * QC-x06 (SPC-QC-106 / QC-006), bit 31 flags a non-photon and the channel
 * widens to three bits (30-28):
 *
 *  | 31-28  | event                                                          |
 *  |--------|----------------------------------------------------------------|
 *  | 0xxx   | photon, channel in bits 30-28                                  |
 *  | 1000   | macro time overflow                                            |
 *  | 1010   | marker, type in bits 15-12                                     |
 *  | 11xx   | GAP photon, channel in bits 29-28                              |
 *
 * Layout per Becker & Hickl's own `SPC_data_file_structure.h`. Cross-checked on
 * SPC-QC-004 recordings against the companion .sdt SPCM writes for a FIFO
 * measurement: histogramming the micro times per channel reproduces its decay
 * curves bin for bin, and binning the macro times reproduces its 1 ms intensity
 * trace.
 *
 * \note Not handled: the QC "absolute time" FIFO mode, where the micro time
 *       instead carries the low 9 bit of a 4 ps absolute time. Nothing in the
 *       .spc header distinguishes it, so it cannot be detected from the record
 *       stream alone.
 */
typedef union bh_spcqc_record{
    uint32_t allbits;
    struct {
        unsigned mt       :12;  ///< macro time, low 12 bit
        unsigned rout     :4;   ///< routing signal / marker type
        unsigned adc      :12;  ///< micro time, not reversed
        unsigned type     :4;   ///< event selector, see above (QC-x04 uses the top 2 bit)
    } bits;
} bh_spcqc_record_t;

/*!
 * Bit position of the input channel within a decoded SPC-QC routing channel.
 *
 * A QC detector is identified by the router signal *and* the module input the
 * photon arrived on, so both have to be kept. Becker & Hickl combine them as
 * "routing channel number, bits 6-4 = input channel for QC-x0x modules"
 * (`MeasFCSInfo.chan` in SPC_data_file_structure.h) -- routing in bits 3-0,
 * input channel in bits 6-4 -- which is the packing the record decoders emit.
 * The two fields together occupy exactly the 7 bits of a positive signed char.
 *
 * Reserving four routing bits when no router is attached would leave a plain
 * three-input measurement on channels 0, 16, 32, so TTTR::read_records_file
 * afterwards moves the input channel down onto the routing width the header
 * declares (see TTTR::compact_spcqc_routing_channels). Only when that width is
 * the full four bits does a routing channel equal SPCM's own `chan` value.
 *
 * \note This is the opposite packing from phconvert's BH reader, which puts the
 *       input channel in the low bits. Verified against SPCM: for a three-input
 *       measurement without a router it writes chan = 0, 16, 32.
 */
#define BH_SPCQC_CH_SHIFT        4

/// Event selector (bits 31-28) of a macro time overflow, both QC flavours
#define BH_SPCQC_TYPE_OVERFLOW   0x8
/// Event selector (bits 31-28) of a QC-x06 marker
#define BH_SPCQC_X06_TYPE_MARKER 0xA
/// Event selector (bits 31-30) of a QC-x04 photon / overflow / marker / GAP
#define BH_SPCQC_X04_TYPE_PHOTON   0x0
#define BH_SPCQC_X04_TYPE_OVERFLOW 0x2
#define BH_SPCQC_X04_TYPE_MARKER   0x1
#define BH_SPCQC_X04_TYPE_GAP      0x3


// Becker Hickl SPC-130/600 macro time overflow record
typedef union bh_overflow{
    uint32_t allbits;
    struct {
        unsigned cnt        :28;
        unsigned empty      :2;
        unsigned mtov       :1;
        unsigned invalid    :1;
    } bits;
} bh_overflow_t;


// Carl Zeiss Confocor3 raw dat
typedef union cz_confocor3_raw_record{
    uint32_t allbits;
    struct {
        unsigned mt      :32;
    } bits;
} cz_confocor3_raw_record_t;

#endif //TTTRLIB_TTTRRECORDTYPES_H
