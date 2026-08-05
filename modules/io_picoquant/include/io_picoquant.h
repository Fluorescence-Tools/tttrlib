// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_PICOQUANT_H
#define TTTRLIB_IO_PICOQUANT_H

/*!
 * \file io_picoquant.h
 * \brief PicoQuant PTU and HT3 containers.
 *
 * PTU is a tagged format -- the header is a list of typed name/value entries,
 * which is the shape tttrlib adopted for every container's metadata. HT3 is a
 * fixed binary header whose Ident and FormatVersion fields select the record
 * decoder, so they must describe the records actually written rather than be
 * inherited from wherever the data came from: an HHT3v2 stream under a "1.0"
 * header is read as HydraHarp v1, and then as SF-compressed, because both
 * encodings count their macro-time overflows.
 */

#include <cstdio>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace tttrlib {
namespace io {

/*!
 * \brief Read a PTU header into \p data.
 * \param tttr_record_type receives the record encoding named by the header.
 * \return the offset of the first record.
 */
std::size_t read_ptu_header(FILE* fpin, int& tttr_record_type, nlohmann::json& data,
                            bool rewind = true);

/// Read an HT3 header into \p data. Returns the offset of the first record.
std::size_t read_ht3_header(FILE* fpin, nlohmann::json& data, bool rewind = true);

/// Write a PTU header describing \p data to \p fn.
void write_ptu_header(std::string fn, nlohmann::json& data, std::string modes = "wb");

/// Write an HT3 header describing \p data to \p fn.
void write_ht3_header(std::string fn, nlohmann::json& data, std::string modes = "wb");

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_PICOQUANT_H
