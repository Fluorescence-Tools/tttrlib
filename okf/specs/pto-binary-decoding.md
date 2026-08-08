---
okf_version: "0.2"
title: "PTO Container Binary Decoding Specification & Embedded TTTR Record Format (fmt)"
status: "Normative Specification"
target_os: ["Linux", "macOS"]
---

# PTO Container Binary Decoding & Embedded TTTR Format (`fmt`) Specification

This document provides a self-contained binary decoding specification for `.pto` (PhoTon cOntainer) files targeted at **Linux** and **macOS (OSX)** environments. It includes:
1. The binary layout & EBML framing rules for Linux and macOS.
2. A clean, standalone ASCII C99 decoding guide for coding agents.
3. The format definition (`fmt`) of embedded TTTR (Time-Tagged Single-Photon) photon record streams.

---

## 1. Platform & Binary Scope (Linux / macOS Focus)

* **Target Operating Systems:** Linux (ELF 64-bit) and macOS / OSX (Mach-O 64-bit, x86_64 / arm64).
* **Polyglot & Binary Execution:** Executable `.pto` bundles consist of a native Linux ELF or macOS Mach-O executable stub prepended to or wrapped within EBML framing.
* **Byte Alignment:** All payload data blocks (`FileData` elements, ID `0x465C`) are aligned to **8-byte boundaries** on disk, permitting direct zero-copy memory mapping (`mmap`) of `uint32_t`, `uint64_t`, and `double` arrays on POSIX systems.
* **No External Dependencies:** Parsing requires only standard C library (`libc`) POSIX syscalls (`open`, `read`, `lseek`, `mmap`, `close`).

---

## 2. `.pto` Binary Layout & EBML Framing

A `.pto` file is an EBML (Extensible Binary Meta Language, RFC 8794) document with `DocType = "pto"`.

### Binary Structure Overview

```
+-------------------------------------------------------------------------------+
| Executable Stub (Optional: Linux ELF / macOS Mach-O Header)                   |
+-------------------------------------------------------------------------------+
| EBML Header Element (ID: 0x1A45DFA3)                                          |
|   - EBMLVersion (0x4286)         : uint = 1                                  |
|   - EBMLReadVersion (0x42F7)     : uint = 1                                  |
|   - EBMLMaxIDLength (0x42F2)     : uint = 4                                  |
|   - EBMLMaxSizeLength (0x42F3)    : uint = 8                                  |
|   - DocType (0x4282)             : string = "pto"                            |
|   - DocTypeVersion (0x4287)      : uint = 1                                  |
|   - DocTypeReadVersion (0x4285)  : uint = 1                                  |
+-------------------------------------------------------------------------------+
| Segment Element (ID: 0x18538067, Data Size: Variable VINT)                    |
|   |-- SeekHead (0x114D9B74)       : Index of top-level elements               |
|   |-- Info (0x1549A966)           : Container metadata (SegmentUUID, etc.)   |
|   |-- Attachments (0x1941A469)    : Container of attached objects             |
|   |     |-- AttachedFile (0x61A7) : One embedded object payload               |
|   |     |     |-- FileUID (0x46AE)      : 64-bit uint object ID                |
|   |     |     |-- FileName (0x466E)     : String label                         |
|   |     |     |-- PtoKind (0x1E54F001)  : String (e.g. "tttr.stream")           |
|   |     |     |-- PtoEncoding (0x1E54F002): String encoding format           |
|   |     |     |-- FileData (0x465C)     : Binary raw payload (TTTR data)     |
|   |-- Tags (0x1254C367)          : Typed tag metadata                         |
+-------------------------------------------------------------------------------+
```

### EBML Variable-Size Integer (VINT) Encoding Rules

EBML Element IDs and Data Sizes are encoded as VINTs. The length of a VINT is determined by counting leading zero bits up to the first set bit (`1`).

| Leading Zero Bits | Mask / Header Bit | Total VINT Bytes | Available Value Bits | Value Range |
|---|---|---|---|---|
| `0` | `1xxx xxxx` | 1 byte | 7 bits | `0` to `2^7 - 2` |
| `1` | `01xx xxxx` | 2 bytes | 14 bits | `0` to `2^14 - 2` |
| `2` | `001x xxxx` | 3 bytes | 21 bits | `0` to `2^21 - 2` |
| `3` | `0001 xxxx` | 4 bytes | 28 bits | `0` to `2^28 - 2` |
| `N` | `(1 << (7-N))` | `N + 1` bytes | `7*(N+1) - N` bits | |

---

## 3. C99 ASCII Decoder Implementation (For Coding Agents)

The following lightweight, standalone C code demonstrates how a coding agent or parser decodes a `.pto` file, parses EBML VINT headers, extracts `AttachedFile` payloads, and locates embedded TTTR streams.

```c
/*
 * pto_decoder_simple.c - Minimal C99 PTO Container & EBML Decoder for Linux/macOS
 *
 * Compiles with: cc -O2 pto_decoder_simple.c -o pto_decoder_simple
 * Usage: ./pto_decoder_simple sample.pto
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* EBML Element IDs */
#define ID_EBML_HEADER  0x1A45DFA3
#define ID_SEGMENT      0x18538067
#define ID_ATTACHMENTS  0x1941A469
#define ID_ATTACHEDFILE 0x61A7
#define ID_FILE_UID     0x46AE
#define ID_FILE_NAME    0x466E
#define ID_PTO_KIND     0x1E54F001
#define ID_FILE_DATA    0x465C

/* Read an EBML VINT (ID or Size). Returns bytes consumed, or 0 on error. */
static size_t read_vint(const uint8_t *buf, size_t max_len, uint64_t *out_val, int mask_first_bit) {
    if (max_len == 0) return 0;
    uint8_t first = buf[0];
    if (first == 0) return 0; /* Invalid VINT */

    int num_bytes = 1;
    uint8_t mask = 0x80;
    while ((first & mask) == 0) {
        mask >>= 1;
        num_bytes++;
        if (num_bytes > 8) return 0;
    }

    if ((size_t)num_bytes > max_len) return 0;

    uint64_t val = mask_first_bit ? (first & ~mask) : first;
    for (int i = 1; i < num_bytes; i++) {
        val = (val << 8) | buf[i];
    }

    *out_val = val;
    return (size_t)num_bytes;
}

/* Parse PTO Container payload */
int decode_pto(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

    size_t filesize = (size_t)st.st_size;
    const uint8_t *data = (const uint8_t *)mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return -1;
    }

    size_t pos = 0;

    /* Scan for EBML Header Magic (0x1A45DFA3) */
    while (pos + 4 <= filesize) {
        uint32_t magic = (uint32_t)data[pos] << 24 | (uint32_t)data[pos+1] << 16 |
                         (uint32_t)data[pos+2] << 8  | (uint32_t)data[pos+3];
        if (magic == ID_EBML_HEADER) break;
        pos++;
    }

    if (pos + 4 > filesize) {
        printf("Error: No valid EBML header found.\n");
        munmap((void*)data, filesize);
        close(fd);
        return -1;
    }

    printf("Found EBML Header at offset 0x%ZX (%ZU)\n", pos, pos);

    /* Walk master elements */
    while (pos < filesize) {
        uint64_t elem_id = 0, elem_size = 0;
        size_t id_len = read_vint(data + pos, filesize - pos, &elem_id, 0);
        if (id_len == 0) break;
        pos += id_len;

        size_t size_len = read_vint(data + pos, filesize - pos, &elem_size, 1);
        if (size_len == 0) break;
        pos += size_len;

        if (elem_id == ID_ATTACHEDFILE) {
            size_t end_attached = pos + elem_size;
            uint64_t uid = 0;
            char name[256] = {0};
            char kind[256] = {0};
            const uint8_t *payload = NULL;
            size_t payload_size = 0;

            size_t sub_pos = pos;
            while (sub_pos < end_attached && sub_pos < filesize) {
                uint64_t sub_id = 0, sub_size = 0;
                size_t s_id_len = read_vint(data + sub_pos, filesize - sub_pos, &sub_id, 0);
                if (s_id_len == 0) break;
                sub_pos += s_id_len;

                size_t s_sz_len = read_vint(data + sub_pos, filesize - sub_pos, &sub_size, 1);
                if (s_sz_len == 0) break;
                sub_pos += s_sz_len;

                if (sub_id == ID_FILE_UID && sub_size <= 8) {
                    uid = 0;
                    for (size_t b = 0; b < sub_size; b++) uid = (uid << 8) | data[sub_pos + b];
                } else if (sub_id == ID_FILE_NAME && sub_size < sizeof(name)) {
                    memcpy(name, data + sub_pos, sub_size);
                    name[sub_size] = '\0';
                } else if (sub_id == ID_PTO_KIND && sub_size < sizeof(kind)) {
                    memcpy(kind, data + sub_pos, sub_size);
                    kind[sub_size] = '\0';
                } else if (sub_id == ID_FILE_DATA) {
                    payload = data + sub_pos;
                    payload_size = (size_t)sub_size;
                }

                sub_pos += sub_size;
            }

            printf("-> Attached Object UID: %llu | Name: '%s' | Kind: '%s' | Size: %ZU bytes\n",
                   (unsigned long long)uid, name, kind, payload_size);
        }

        /* Advance to next top-level element if container */
        if (elem_id != ID_SEGMENT && elem_id != ID_ATTACHMENTS && elem_id != ID_EBML_HEADER) {
            pos += elem_size;
        }
    }

    munmap((void*)data, filesize);
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <file.pto>\n", argv[0]);
        return 1;
    }
    return decode_pto(argv[1]);
}
```

---

## 4. Embedded TTTR File Format (`fmt`) Definition

When a `.pto` object carries photon event stream data (`PtoKind` = `"tttr.stream"`), the `FileData` payload consists of a structured TTTR stream.

### 4.1 TTTR Stream Header (`fmt` Header)

The embedded TTTR payload begins with a standard header defining the hardware sync clock, microtime channel properties, and record type:

| Field Name | Type | Size | Description |
|---|---|---|---|
| `magic` | ASCII String | 6 bytes | Format magic bytes (`"PQTTTR"` or `"TTTR32"`) |
| `version` | ASCII String | 8 bytes | Format version (e.g. `"1.0.0\0\0"`) |
| `record_type` | `uint32_t` | 4 bytes | Numerical identifier for TTTR record format |
| `macro_sync_rate` | `uint32_t` | 4 bytes | Laser sync repetition rate in Hz (e.g., 20,000,000 Hz) |
| `macro_time_resolution` | `double` | 8 bytes | Macrotime clock period in seconds ($1 / \text{sync\_rate}$) |
| `micro_time_resolution` | `double` | 8 bytes | TAC / TCSPC channel width in seconds (e.g., $4 \times 10^{-12}$ s) |
| `number_of_records` | `uint64_t` | 8 bytes | Total number of 32-bit photon records following header |

---

### 4.2 TTTR 32-Bit Record Format Definitions (`fmt`)

Following the header, `number_of_records` packed 32-bit (`uint32_t`) records follow sequentially.

#### Format 0: PTU / HydraHarp / MultiHarp T3 Record (32-bit Little-Endian)

In **T3 mode**, macrotime measures sync pulses (laser clock ticks) since start/overflow, while microtime measures decay lifetime within the pulse.

```
 Bit 31     Bits 30..25    Bits 24..10                Bits 9..0
+---------+---------------+--------------------------+--------------------------+
| Special | Channel (6b)  | Microtime TAC Bins (15b) | Macrotime Sync Ticks(10b)|
+---------+---------------+--------------------------+--------------------------+
```

* `Special` (1 bit): `0` = Valid Photon Event; `1` = Special Event (Overflow or Hardware Marker).
* `Channel` (6 bits): Routing channel (`0..63`).
  * If `Special == 1` and `Channel == 63` (`0x3F`), the record is a **Macrotime Overflow**.
  * If `Special == 1` and `Channel < 63`, the record is an external hardware marker (e.g. frame/line sync in CLSM).
* `Microtime` (15 bits): TAC/ADC value (`0..32767`), converted to nanoseconds by `microtime * micro_time_resolution`.
* `Macrotime` (10 bits): Sync clock count since last overflow (`0..1023`).

#### Format 1: PTU / HydraHarp / MultiHarp T2 Record (32-bit Little-Endian)

In **T2 mode**, macrotime directly records high-resolution arrival time; no microtime lifetime bin is recorded.

```
 Bit 31     Bits 30..25    Bits 24..0
+---------+---------------+-----------------------------------------------------+
| Special | Channel (6b)  | Macrotime Sync Ticks (25 bits)                      |
+---------+---------------+-----------------------------------------------------+
```

* `Special` (1 bit): `0` = Photon; `1` = Marker or Overflow.
* `Channel` (6 bits): Detector channel (`0..63`). If `Channel == 63` and `Special == 1`, it is a **Macrotime Overflow**.
* `Macrotime` (25 bits): Absolute arrival time in clock ticks ($25 \text{ bits} \implies \text{rollover at } 2^{25} = 33,554,432 \text{ ticks}$).

#### Format 2: Becker & Hickl SPC-130 Record (32-bit Little-Endian)

```
 Bits 31..16                Bits 15..12   Bits 11..0
+--------------------------+-------------+--------------------------------------+
| Macrotime (16 bits)      | Channel (4b)| Microtime ADC (12 bits)              |
+--------------------------+-------------+--------------------------------------+
```

---

### 4.3 Macrotime Overflow & Arrival Time Calculation Pseudocode

Because macrotime counters use fixed bit widths ($10 \text{ bits} = 1024$, $25 \text{ bits} = 33,554,432$), the absolute arrival time of photon $k$ is calculated by accumulating overflows:

```c
/* Pseudocode for photon decoding loop */
uint64_t overflow_accumulator = 0;
const uint64_t OVERFLOW_STEP_T3 = 1024; /* 2^10 */

for (size_t i = 0; i < number_of_records; i++) {
    uint32_t raw_rec = record_buffer[i];

    uint32_t special   = (raw_rec >> 31) & 0x01;
    uint32_t channel   = (raw_rec >> 25) & 0x3F;
    uint32_t microtime = (raw_rec >> 10) & 0x7FFF;
    uint32_t macrotime = raw_rec & 0x03FF;

    if (special == 1) {
        if (channel == 63) {
            /* Macrotime Overflow record */
            overflow_accumulator += OVERFLOW_STEP_T3;
        } else {
            /* Hardware Marker Event */
            uint32_t marker_mask = channel;
            handle_marker(marker_mask, overflow_accumulator + macrotime);
        }
    } else {
        /* Valid Photon Event */
        uint64_t abs_macrotime = overflow_accumulator + macrotime;
        double   macro_sec     = (double)abs_macrotime * macro_time_resolution;
        double   micro_sec     = (double)microtime * micro_time_resolution;

        store_photon(channel, macro_sec, micro_sec);
    }
}
```

---

## 5. Summary Checklist for Coding Agents

When implementing a `.pto` reader or writer agent:
- [x] Use standard POSIX file I/O or `mmap` for Linux/macOS binary access.
- [x] Locate the EBML header (`0x1A45DFA3`) and segment elements.
- [x] Parse VINT lengths dynamically to walk element trees.
- [x] Extract target attached objects using `PtoKind` (`"tttr.stream"`).
- [x] Decode 32-bit TTTR records handling `Special` overflow records (`Channel == 63`) to compute absolute photon timestamps.
