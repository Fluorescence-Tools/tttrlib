// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_BECKERHICKL_H
#define TTTRLIB_IO_BECKERHICKL_H

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
 * \brief Read a ".set" sidecar into \p data.
 *
 * Extracts SP_IMG_X, SP_IMG_Y and SP_PIX_CLK as ImgHdr_PixX / ImgHdr_PixY /
 * BH_UsePixelClock. For SPC-QC containers SP_TAC_R and SP_ADC_RE are read as
 * well and define the micro time resolution the 4-byte header cannot carry.
 */
bool read_bh_set_file(const std::string& filename, nlohmann::json& data);

/// Write a ".set" sidecar from the imaging tags in \p data; false if there are none.
bool write_bh_set_file(const std::string& filename, nlohmann::json& data);

/// Write an SPC-130 header describing \p data to \p fn.
void write_spc132_header(std::string fn, nlohmann::json& data, std::string modes = "w");

/// Write an SPC-QC header describing \p data to \p fn.
void write_spcqc_header(std::string fn, nlohmann::json& data, std::string modes = "w");

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_BECKERHICKL_H
