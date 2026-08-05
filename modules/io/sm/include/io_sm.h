// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_SM_H
#define TTTRLIB_IO_SM_H

/*!
 * \file io_sm.h
 * \brief The single-molecule (SM) container's header.
 *
 * A big-endian header of length-prefixed strings, then the record stream. Both
 * functions take the header document directly rather than a TTTRHeader, which
 * is what lets this module sit below core: a format reader has no business
 * knowing about the photon-stream data model.
 *
 * TTTRHeader::read_sm_header / write_sm_header forward here, so this is not an
 * API change.
 */

#include <cstdio>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace tttrlib {
namespace io {

/// Read the SM header into \p data. Returns the offset of the first record.
std::size_t read_sm_header(FILE* file, nlohmann::json& data);

/// Write an SM header describing \p data to \p fn.
void write_sm_header(std::string fn, nlohmann::json& data, std::string modes = "wb");

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_SM_H
