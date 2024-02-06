#ifndef TTTRLIB_TTTRHEADERTYPES_H
#define TTTRLIB_TTTRHEADERTYPES_H

#include <cstdint> // int32, uint32, etc.


// Definitions used in PTU header and JSON dictionary
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
#define rtMultiHarpT3    0x00010307    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $07 (MultiHarp)
#define rtMultiHarpT2    0x00010207    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $07 (MultiHarp)

// tttrlib internal container identifier definitions
#define PQ_PTU_CONTAINER          0
#define PQ_HT3_CONTAINER          1
#define BH_SPC130_CONTAINER       2
#define BH_SPC600_256_CONTAINER   3
#define BH_SPC600_4096_CONTAINER  4
#define PHOTON_HDF_CONTAINER      5
#define CZ_CONFOCOR3_CONTAINER    6

// tttrlib record type identifier definitions
#define PQ_RECORD_TYPE_HHT2v2       1
#define PQ_RECORD_TYPE_HHT2v1       2
#define PQ_RECORD_TYPE_HHT3v1       3
#define PQ_RECORD_TYPE_HHT3v2       4
#define PQ_RECORD_TYPE_PHT3         5
#define PQ_RECORD_TYPE_PHT2         6
#define BH_RECORD_TYPE_SPC130       7
#define BH_RECORD_TYPE_SPC600_256   8
#define BH_RECORD_TYPE_SPC600_4096  9
#define CZ_RECORD_TYPE_CONFOCOR3    10


/*
 * PicoQuant HT3 file definitions
 */

typedef struct {
    int32_t MapTo;
    int32_t Show;
} CurveMapping_t;

typedef struct {
    float Start;
    float Step;
    float End;
} ParamStruct_t;

typedef struct{
    int32_t ModelCode;
    int32_t VersionCode;
} pq_ht3_board_settings_t;

/// The following represents the readable ASCII file header portion in a HT3 file
typedef struct {
    char Ident[16];                //"PicoHarp 300"
    char FormatVersion[6];         //file format version
    char CreatorName[18];          //name of creating software
    char CreatorVersion[12];       //version of creating software
    char FileTime[18];
    char CRLF[2];
    char CommentField[256];
    int32_t NumberOfCurves;
    int32_t BitsPerRecord;
    int32_t ActiveCurve;
    int32_t MeasurementMode;
    int32_t SubMode;
    int32_t Binning;
    double  Resolution; // in ps
    int32_t Offset;
    int32_t AquisitionTime;
    uint32_t StopAt;
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
    char HardwareIdent[16];
    char HardwareVersion[8];
    int32_t HardwareSerial;
    int32_t nModulesPresent;
    pq_ht3_board_settings_t HardwareModel[10];
    double BaseResolution;
    int64_t InputsEnabled;
    int32_t InpChansPresent;
    int32_t RefClockSource;
    int32_t ExtDevices;
    int32_t MarkerSettings;
    int32_t SyncDivider;
    int32_t SyncCFDLevel;
    int32_t SyncCFDZeroCross;
    int32_t SyncOffset;
} pq_ht3_Header_t;

typedef struct {
    int32_t InputModuleIndex;
    int32_t InputCFDLevel;
    int32_t InputCFDZeroCross;
    int32_t InputOffset;
    int32_t InputRate;
} pq_ht3_ChannelHeader_t;

typedef struct {
    int32_t SyncRate;
    int32_t StopAfter;
    int32_t StopReason;
    int32_t ImgHdrSize;
    uint64_t nRecords;
} pq_ht3_TTModeHeader_t;


/// Carl Zeiss Confocor3 raw data
typedef union cz_confocor3_settings{
    uint32_t allbits;
    struct{
        char Ident[52];
        char dummy1[11];
        unsigned channel               :8;
        // 64 byte
        uint32_t measure_id[4];  // 16
        uint32_t measurement_position; // 8
        uint32_t kinetic_index; // 8
        uint32_t repetition_number; // 8
        uint32_t frequency; // 8
        char dummy2[32]; //32
        // 64 + 64 byte
    } bits;
} cz_confocor3_settings_t;


/// Becker&Hickl SPC132 Header
typedef union bh_spc132_header{
    uint32_t allbits;
    struct{
        unsigned macro_time_clock :24;   // the resolution of the macro time
        unsigned unused           :7;    // unclear usage
        bool invalid              :1;    // true if dataset is marked as invalid
    } bits;
} bh_spc132_header_t;


/// A Header Tag entry of a PTU file
typedef struct tag_head {
    char Ident[32];     // Identifier of the tag
    int Idx;            // Index for multiple tags or -1
    uint32_t Typ;       // Type of tag ty..... see const section
    uint64_t TagValue;  // Value of tag.
} tag_head_t;


#endif //TTTRLIB_TTTRHEADERTYPES_H
