// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_CARLZEISS_H
#define TTTRLIB_IO_CARLZEISS_H

/*!
 * \file io_cz.h
 * \brief The Zeiss ConfoCor3 raw container's header.
 *
 * The file opens with an ASCII banner -- "Carl Zeiss ConfoCor3 - raw data file
 * - version 3.000 - Channel 1" -- which the settings struct overlays, so the
 * channel arrives as a character and the reader subtracts 48. That banner is
 * also what identifies the format; see isCZConfocor3File.
 *
 * TTTRHeader::read_cz_confocor3_header / write_cz_confocor3_header forward
 * here, so this is not an API change.
 */

#include <cstdio>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace tttrlib {
namespace io {

/// Read the ConfoCor3 header into \p data. Returns the offset of the first record.
std::size_t read_cz_confocor3_header(FILE* fpin, nlohmann::json& data, bool rewind = true);

/// Write a ConfoCor3 header describing \p data to \p fn.
void write_cz_confocor3_header(std::string fn, nlohmann::json& data, std::string modes = "wb");

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_CARLZEISS_H
