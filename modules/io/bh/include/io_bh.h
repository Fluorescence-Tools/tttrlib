// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_BECKERHICKL_H
#define TTTRLIB_IO_BECKERHICKL_H

// Validation: A/B-TESTED 2026-08-17 -- SPC-130 photon stream vs phconvert's SPC-1xx reader (macro, micro, routing
//   exact; timestamps_unit equal). QC formats are not in phconvert correctly
//   (its QC reader is known-wrong, see memory) and are pinned by fixture in
//   test/python/tttr/test_bh_spcqc.py. test/python/test_ab_core_reference.py.
//   Register: okf/testing/algorithm-validation.md

/*!
 * \file io_bh.h
 * \brief Becker & Hickl SPC containers: SPC-130, SPC-600 and SPC-QC.
 *
 * All four share the ".spc" extension and a 4-byte header, which is why
 * detection has to look at the record stream rather than the name. SPC-QC is
 * the odd one: its macro time clock is in femtoseconds and its micro-time
 * resolution cannot be derived from the header at all, because the QC modules
 * run their TAC independently of the macro clock -- a default TAC range is
 * assumed and replaced from the ".set" sidecar when one is present.
 *
 * The sidecar is the reason this module owns more than a header reader. A B&H
 * SPC record file cannot store the CLSM imaging geometry; the vendor software
 * keeps it in a companion ".set" file next to the ".spc".
 */

#include <cstdio>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace tttrlib {
namespace io {

/// Read the 4-byte SPC-130 header into \p data. Returns the first record's offset.
std::size_t read_bh132_header(FILE* fpin, nlohmann::json& data, bool rewind = true);

/// Read the SPC-QC header into \p data. Returns the first record's offset.
std::size_t read_bh_spcqc_header(FILE* fpin, nlohmann::json& data, bool rewind = true);

/*!
 * \brief Read the first frame of an SPC-600/630 FIFO file into \p data.
 *
 * Becker & Hickl (`SPC_data_file_structure.h`): the software prepends one
 * photon frame carrying the macro time clock in 0.1 ns units and the number
 * of routing bits, flagged INVALID. In 256-channel (32-bit) mode it is the
 * same word as the SPC-130 header (bits 0-23 clock, 27-30 routing bits, 31
 * invalid); in 4096-channel (48-bit) mode it is 6 bytes with the clock in
 * bytes 2-3 and the routing bits in byte 1. Returns the first record's
 * offset (4 or 6). Until 2026-08-17 the frame was decoded as a record and
 * dropped as invalid, so the macro time resolution stayed at 1.0.
 */
std::size_t read_bh_spc600_header(FILE* fpin, nlohmann::json& data, bool rewind = true, bool wide_48bit = false);

/*!
 * \brief Read a ".set" sidecar into \p data.
 *
 * Extracts SP_IMG_X, SP_IMG_Y and SP_PIX_CLK as ImgHdr_PixX / ImgHdr_PixY /
 * BH_UsePixelClock. For SPC-QC containers SP_TAC_R and SP_ADC_RE are read as
 * well and define the micro time resolution the 4-byte header cannot carry.
 */
bool read_bh_set_file(const std::string& filename, nlohmann::json& data);

/*!
 * \brief \see read_bh_set_file, for a sidecar the caller already has.
 *
 * A .spc embedded in a container has no directory to look in, so whoever
 * unpacked it hands the bytes over instead of a path.
 */
bool parse_bh_set(const std::string& content, nlohmann::json& data);

/// Write a ".set" sidecar from the imaging tags in \p data; false if there are none.
bool write_bh_set_file(const std::string& filename, nlohmann::json& data);

/// Write an SPC-130 header describing \p data to \p fn.
void write_spc132_header(std::string fn, nlohmann::json& data, std::string modes = "w");

/// Write the SPC-600/630 first frame (4 bytes, or 6 in 48-bit mode) describing \p data to \p fn.
void write_spc600_header(std::string fn, nlohmann::json& data, std::string modes = "w", bool wide_48bit = false);

/// Write an SPC-QC header describing \p data to \p fn.
void write_spcqc_header(std::string fn, nlohmann::json& data, std::string modes = "w");

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_BECKERHICKL_H
