// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TTTRTAGS_H
#define TTTRLIB_TTTRTAGS_H

/*!
 * \file TTTRTags.h
 * \brief Reading and writing the metadata tags a TTTR header carries.
 *
 * A tag is a named, typed entry in the header's JSON document -- the shape
 * PicoQuant's PTU format uses, which tttrlib adopted for every container so one
 * reader can describe any of them.
 *
 * These live at the io layer because every vendor header reader needs them and
 * none of them needs anything else: they manipulate a nlohmann::json document
 * and touch no TTTRHeader state. Keeping them on TTTRHeader would have meant a
 * format module depending on core, and core depending on the format modules --
 * a link cycle CMake refuses between shared libraries.
 *
 * TTTRHeader::add_tag / get_tag / find_tag remain as forwarders, so this is not
 * an API change.
 */

#include <any>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include <string>

#include "TTTRHeaderTypes.h"

// some important Tag Idents (TTagHead.Ident) that we will need to read the most common content of a PTU file
// check the output of this program and consult the tag dictionary if you need more
const std::string TTTRTagRes = "MeasDesc_Resolution";              // Resolution for the Dtime (T3 Only) - in seconds
const std::string TTTRTagGlobRes = "MeasDesc_GlobalResolution";    // Global Resolution of TimeTag(T2) /NSync (T3) - in seconds
const std::string TTTRSyncRate = "SyncRate";                       // SyncRate - in Hz
const std::string TTTRNMicroTimes = "MeasDesc_NumberMicrotimes";   // The number of micro time channels
const std::string TTTRRecordType = "MeasDesc_RecordType";         // Internal record type (see tttrlib record type identifier definitions)
const std::string TTTRContainerType = "MeasDesc_ContainerType";   // Internal container type (see tttrlib record type identifier definitions)
const std::string TTTRTagTTTRRecType = "TTResultFormat_TTTRRecType";
const std::string TTTRTagBits = "TTResultFormat_BitsPerRecord";    // Bits per TTTR record
const std::string TTTRTagNumRecords = "TTResult_NumberOfRecords";  // Number of TTTR records in the file
const std::string FileTagEnd = "Header_End";                       // Always appended as last tag (BLOCKEND)

namespace tttrlib {
namespace io {

/*!
 * \brief Add a tag, or replace the value of one that already exists.
 *
 * \param json_data the header document
 * \param name tag name, e.g. "MeasDesc_GlobalResolution"
 * \param value the value; the accepted types follow \p type
 * \param type one of the ty* constants in TTTRHeaderTypes.h
 * \param idx index for repeated tags, -1 for a scalar tag
 */
void add_tag(nlohmann::json &json_data, const std::string &name, std::any value,
             unsigned int type = tyAnsiString, int idx = -1);

/// The tag \p name (optionally at \p idx), or a null json if there is none.
nlohmann::json get_tag(const nlohmann::json &json_data, const std::string &name,
                       int idx = -1);

/// Position of the tag in the "tags" array, or -1.
int find_tag(nlohmann::json &json_data, const std::string &name, int idx = -1);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_TTTRTAGS_H
