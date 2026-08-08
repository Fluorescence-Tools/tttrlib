/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef PTO_READ_H
#define PTO_READ_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PTO Element IDs */
#define PTO_ID_EBML             0x1A45DFA3U
#define PTO_ID_DOCTYPE          0x4282U
#define PTO_ID_DOCTYPE_VERSION  0x4287U
#define PTO_ID_DOCTYPE_READ_VER 0x4285U
#define PTO_ID_SEGMENT          0x18538067U
#define PTO_ID_SEEKHEAD         0x114D9B74U
#define PTO_ID_SEEK             0x4DBBU
#define PTO_ID_SEEKID           0x53ABU
#define PTO_ID_SEEKPOSITION     0x53ACU
#define PTO_ID_INFO             0x1549A966U
#define PTO_ID_TITLE            0x7BA9U
#define PTO_ID_SEGMENT_UUID     0x73A4U
#define PTO_ID_MUXING_APP       0x4D80U
#define PTO_ID_WRITING_APP      0x5741U
#define PTO_ID_ATTACHMENTS      0x1941A469U
#define PTO_ID_ATTACHED_FILE    0x61A7U
#define PTO_ID_FILE_NAME        0x466EU
#define PTO_ID_FILE_UID         0x46AEU
#define PTO_ID_FILE_MEDIA       0x4660U
#define PTO_ID_FILE_DATA        0x465CU
#define PTO_ID_TAGS             0x1254C367U
#define PTO_ID_TAG              0x7373U
#define PTO_ID_TARGETS          0x63C0U
#define PTO_ID_TARGET_TYPE_VAL  0x68CAU
#define PTO_ID_TARGET_ATTACH    0x63C5U
#define PTO_ID_SIMPLE_TAG       0x67C8U
#define PTO_ID_TAG_NAME         0x45A3U
#define PTO_ID_TAG_STRING       0x4487U
#define PTO_ID_VOID             0xECU
#define PTO_ID_CUES             0x1C53BB6BU

/* PTO Custom Element IDs */
#define PTO_ID_KIND             0x1E54F001U
#define PTO_ID_ENCODING         0x1E54F002U
#define PTO_ID_ROW_COUNT        0x1E54F003U
#define PTO_ID_GENERATION       0x1E54F010U
#define PTO_ID_SEEK_UID         0x1E54F011U
#define PTO_ID_TAG_INDEX        0x1E54F020U
#define PTO_ID_TAG_SRC_TYPE     0x1E54F021U
#define PTO_ID_TAG_UINT         0x1E54F022U
#define PTO_ID_TAG_INT          0x1E54F023U
#define PTO_ID_TAG_FLOAT        0x1E54F024U
#define PTO_ID_TAG_DATE         0x1E54F025U
#define PTO_ID_TAG_UID          0x1E54F026U
#define PTO_ID_TAG_UIDS         0x1E54F027U
#define PTO_ID_TAG_FLOATS       0x1E54F028U
#define PTO_ID_TAG_INTS         0x1E54F029U
#define PTO_ID_BANNER           0x1E54F040U
#define PTO_ID_ANNOTATIONS      0x1E54F100U
#define PTO_ID_ANNOTATION       0x1E54F101U

typedef struct {
    uint64_t uid;
    char kind[64];
    char encoding[64];
    char name[256];
    uint64_t size;
    uint64_t rows;
    uint64_t offset;       /* Absolute offset of FileData payload */
    uint64_t elem_offset;  /* Absolute offset of AttachedFile element */
    bool aligned;
} PtoReadObject;

typedef struct {
    char name[256];
    uint32_t type_code;
    uint64_t target;
    int32_t index;
    uint32_t source_type;
    uint64_t val_u;
    int64_t val_i;
    double val_d;
    char text[512];
    uint64_t val_uid;
} PtoReadTag;

typedef struct {
    uint32_t id;
    char name[64];
    uint64_t offset;
    uint64_t data_offset;
    uint64_t size;
    uint64_t total_size;
    int depth;
} PtoReadElement;

typedef struct {
    char filename[512];
    char title[256];
    char uuid_hex[33];
    uint64_t generation;
    uint64_t created;
    char muxing_app[256];
    char writing_app[256];
    char banner[4096];
    uint64_t doctype_version;
    uint64_t doctype_read_version;
    uint64_t ebml_offset;
    uint64_t segment_offset;
    uint64_t file_size;

    size_t num_objects;
    PtoReadObject* objects;

    size_t num_tags;
    PtoReadTag* tags;

    size_t num_elements;
    PtoReadElement* elements;
} PtoReadFileInfo;

#define PTO_MAX_INSPECTION_DECAYS 16
#define PTO_MAX_DECAY_BINS 4096
#define PTO_MAX_TRACE_BINS 2048

typedef struct {
    int channel;
    uint32_t n_bins;
    double bin_width_ns;
    uint32_t counts[PTO_MAX_DECAY_BINS];
    uint32_t max_count;
    uint64_t total_counts;
} PtoReadDecayData;

typedef struct {
    uint32_t n_bins;
    double dt_seconds;
    uint32_t counts[PTO_MAX_TRACE_BINS];
    uint32_t min_count;
    uint32_t max_count;
    double mean_count;
} PtoReadTraceData;

typedef struct {
    bool has_trace;
    PtoReadTraceData trace;

    size_t num_decays;
    PtoReadDecayData decays[PTO_MAX_INSPECTION_DECAYS];

    char metadata_json[4096];
} PtoReadInspectionData;

/* C API */
int pto_read_open(const char* filename, PtoReadFileInfo* info);
void pto_read_close(PtoReadFileInfo* info);
int pto_read_cat(const char* filename, const PtoReadObject* obj, FILE* out_fp);
int pto_read_extract(const char* filename, const PtoReadObject* obj, const char* out_path);
int pto_read_verify(const char* filename, const PtoReadFileInfo* info, char* errbuf, size_t errbuf_len);
int pto_read_get_inspection_data(const char* filename, const PtoReadFileInfo* info, PtoReadInspectionData* out_data);

const PtoReadObject* pto_read_find_object(const PtoReadFileInfo* info, const char* sel, int force_mode, int* match_count);
void pto_format_bytes(uint64_t bytes, char* buf, size_t buf_size);
const char* pto_element_name(uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* PTO_READ_H */
