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


#ifndef TTTRLIB_READHEADER_H
#define TTTRLIB_READHEADER_H

#include <stdint.h>

/*
#if defined(_WIN32)
#include "../thirdparty/stdint.h" // Windows
    #define _CRT_SECURE_NO_DEPRECATE
#elif defined(_WIN64)
#include "../thirdparty/stdint.h" // Windows
    #define _CRT_SECURE_NO_DEPRECATE
#endif
*/

 #include <stdio.h>
#include <iostream>
#include <map>


// some important Tag Idents (TTagHead.Ident) that we will need to read the most common content of a PTU file
// check the output of this program and consult the tag dictionary if you need more
#define TTTRTagTTTRRecType "TTResultFormat_TTTRRecType"
#define TTTRTagRes         "MeasDesc_Resolution"       // Resolution for the Dtime (T3 Only)
#define TTTRTagGlobRes     "MeasDesc_GlobalResolution" // Global Resolution of TimeTag(T2) /NSync (T3)
#define FileTagEnd         "Header_End"                // Always appended as last tag (BLOCKEND)

#define tyEmpty8      0xFFFF0008
#define tyBool8       0x00000008
#define tyInt8        0x10000008
#define tyBitSet64    0x11000008
#define tyColor8      0x12000008
#define tyFloat8      0x20000008
#define tyTDateTime   0x21000008
#define tyFloat8Array 0x2001FFFF
#define tyAnsiString  0x4001FFFF
#define tyWideString  0x4002FFFF
#define tyBinaryBlob  0xFFFFFFFF

#define rtPicoHarpT3     0x00010303    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $03 (PicoHarp)
#define rtPicoHarpT2     0x00010203    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $03 (PicoHarp)
#define rtHydraHarpT3    0x00010304    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $04 (HydraHarp)
#define rtHydraHarpT2    0x00010204    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $04 (HydraHarp)
#define rtHydraHarp2T3   0x01010304    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $04 (HydraHarp)
#define rtHydraHarp2T2   0x01010204    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $04 (HydraHarp)
#define rtTimeHarp260NT3 0x00010305    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $05 (TimeHarp260N)
#define rtTimeHarp260NT2 0x00010205    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $05 (TimeHarp260N)
#define rtTimeHarp260PT3 0x00010306    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $06 (TimeHarp260P)
#define rtTimeHarp260PT2 0x00010206    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $06 (TimeHarp260P)
#define rtMultiHarpNT3   0x00010307    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $07 (MultiHarp150N)
#define rtMultiHarpNT2   0x00010207    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $07 (MultiHarp150N)


#define PQ_PTU_CONTAINER          0
#define PQ_HT3_CONTAINER          1
#define BH_SPC130_CONTAINER       2
#define BH_SPC600_256_CONTAINER   3
#define BH_SPC600_4096_CONTAINER  4
#define PHOTON_HDF_CONTAINER      5


#define PQ_RECORD_TYPE_HHT2v2       1
#define PQ_RECORD_TYPE_HHT2v1       2
#define PQ_RECORD_TYPE_HHT3v1       3
#define PQ_RECORD_TYPE_HHT3v2       4
#define PQ_RECORD_TYPE_PHT3         5
#define PQ_RECORD_TYPE_PHT2         6
#define BH_RECORD_TYPE_SPC130       7
#define BH_RECORD_TYPE_SPC600_256   8
#define BH_RECORD_TYPE_SPC600_4096  9



typedef struct {
    int32_t MapTo;
    int32_t Show;
} CurveMapping_t;

typedef struct {
    float Start;
    float Step;
    float End;
} ParamStruct_t;


/// The following represents the readable ASCII file header portion
typedef struct {
    char Ident[16];                //"PicoHarp 300"
    char FormatVersion[6];        //file format version
    char CreatorName[18];        //name of creating software
    char CreatorVersion[12];    //version of creating software
    char FileTime[18];
    char CRLF[2];
    char CommentField[256];
} pq_ht3_ascii_t;



/// The following is binary file header information
typedef struct {
    int32_t Curves;
    int32_t BitsPerChannel;
    int32_t RoutingChannels;
    int32_t NumberOfBoards;

    int32_t ActiveCurve;
    int32_t MeasMode;
    int32_t SubMode;
    int32_t RangeNo;
    int32_t Offset;

    int32_t AquisitionTime;
    int32_t StopAt;
    int32_t StopOnOvfl;
    int32_t Restart;

    int32_t DispLinLog;
    int32_t DispTimeFrom;
    int32_t DispTimeTo;
    int32_t DispCountsFrom;
    int32_t DispCountsTo;
    CurveMapping_t DispCurves[8];
    ParamStruct_t Params[3];

    int32_t RepeatMode;
    int32_t RepeatsPerCurve;
    int32_t RepeatTime;
    int32_t RepeatWaitTime;

    char ScriptName[20];


} pq_ht3_BinHdr_t;



typedef struct {
    char HardwareIdent[16];
    char HardwareVersion[8];
    int HardwareSerial;                /* in ms */
    int SyncDivider;
} pq_ht3_BoardHdr;


typedef struct{
    int32_t BoardSerial;
    int32_t SyncDivider;
    int32_t CFDZeroCross;
    int32_t CFDZeroLevel;
} pq_ht3_board_settings_t;


typedef struct {
    int32_t Globclock;
    int32_t ExtDevices;
    int32_t Reserved1;
    int32_t Reserved2;
    int32_t Reserved3;
    int32_t Reserved4;
    int32_t Reserved5;
    int32_t SyncRate;
    int32_t TTTRCFDRate;
    int32_t TTTRStopAfter;
    int32_t TTTRStopReason;
    int32_t NoOfRecords;
    int32_t SpecialHeaderSize;
} pq_ht3_TTTRHdr;



/// A Header Tag entry of a PTU file
typedef struct tag_head {
    char Ident[32];     // Identifier of the tag
    int Idx;            // Index for multiple tags or -1
    uint32_t Typ;       // Type of tag ty..... see const section
    uint64_t TagValue;   // Value of tag.
} tag_head_t;


class Header {

private:
    std::FILE *fpin;

protected:

    int tttr_container_type;

public:

    int tttr_record_type;
    unsigned int number_of_tac_channels;

    /*!
     * Marks the end of the header in the file
     */
    size_t header_end;


    /*!
     * Stores the bytes per TTTR record of the associated TTTR file
     */
    size_t bytes_per_record;

    /// Resolution for the micro time in nanoseconds
    double micro_time_resolution;

    /// Resolution for the macro time in nanoseconds
    double macro_time_resolution;

    /*!
     * @return The TTTR container type of the associated TTTR file as a char
     */
    int getTTTRRecordType();

    /*!
     * Default constructor
     */
    Header();

    /*!
     * Constructor for the @class Header that takes a file pointer and the container type of the
     * file represented by the file pointer. The container type refers either to a PicoQuant (PQ) PTU or
     * HT3 file, or a BeckerHickl (BH) spc file. There are three different types of BH spc files SPC130,
     * SPC600_256 (256 bins in micro time) or SPC600_4096 (4096 bins in micro time).
     * PQ HT3 files may contain different TTTR record types depending on the counting device (HydraHarp, PicoHarp)
     * and firmware revision of the counting device. Similarly, PTU files support a diverse set of TTTR records.
     *
     * @param fpin the file pointer to the TTTR file
     * @param tttr_container_type the container type
     *
     */
    Header(
            std::FILE *fpin,
            int tttr_container_type
            );
    ~Header();

    ///
    /*!
     * The @param data stores the data read out of the file header in a string map.
     */
    std::map<std::string, std::string> data;

};


/*! Reads the header of a ptu file and sets the reading routing for
 *
 * @param fpin
 * @param rewind
 * @param tttr_record_type
 * @param data
 * @param macro_time_resolution
 * @param micro_time_resolution
 * @return The position of the file pointer at the end of the header
 */
size_t read_ptu_header(
        std::FILE *fpin,
        bool rewind,
        int &tttr_record_type,
        std::map<std::string, std::string> &data,
        double &macro_time_resolution,
        double &micro_time_resolution
);


/*! Reads the header of a ht3 file and sets the reading routing for
  *
  * @param fpin
  * @param rewind
  * @param tttr_record_type
  * @param data
  * @param macro_time_resolution
  * @param micro_time_resolution
 * @return The position of the file pointer at the end of the header
  */
size_t read_ht3_header(
        std::FILE *fpin,
        bool rewind,
        int &tttr_record_type,
        std::map<std::string, std::string> &data,
        double &macro_time_resolution,
        double &micro_time_resolution
);


#endif //TTTRLIB_READHEADER_H
