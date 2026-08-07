// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TTTRSTREAM_H
#define TTTRLIB_TTTRSTREAM_H

/*!
 * \file TTTRStream.h
 * \brief Reading a container in pieces.
 *
 * Fourteen container types are registered and, until this file, exactly one of
 * them -- PTO -- could be read in anything other than one go. Everything
 * downstream that wanted a progress bar, a first look at a large file, or a
 * live view of one still being written had to either wait for the whole read or
 * write its own reader.
 *
 * Two calls are enough, because they compose:
 *
 * \code
 * TTTR t;
 * TTTRDecodeState state;
 * const auto info = tttrlib::container_records("run.ptu");
 * for (std::uint64_t at = 0; at < info.n_records; at += 100000) {
 *     auto buf = tttrlib::container_read_records("run.ptu", at, 100000);
 *     t.decode_records(buf.data(), (int) buf.size(), info.record_type, &state);
 * }
 * t.set_header(new TTTRHeader("run.ptu", info.container_type));
 * t.apply_container_channels(info.container_type);
 * \endcode
 *
 * That loop is five lines in any of the four bindings and gives byte-identical
 * results to `TTTR("run.ptu")`, because the state carries the overflow count
 * across the boundaries. The library does not have to own the loop.
 *
 * The last two lines matter for exactly two containers -- CZ-RAW and SPC-QC,
 * whose records do not carry the whole routing channel -- and are a no-op for
 * the other five. \see TTTR::apply_container_channels.
 *
 * \ref container_read_events covers the common case without one, and pays for
 * it in the only way it can: see the note on its macro times.
 */

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "TTTRHeaderTypes.h"    // the record-type ids, as macros
#include "TTTRRecordReader.h"   // where the four helpers below are defined

class TTTR;

/*!
 * \brief The record encodings, as values a binding can name.
 *
 * One enumerator per `*_RECORD_TYPE_*` macro in TTTRHeaderTypes.h, taking its
 * value from the macro rather than repeating it -- so these cannot drift, and
 * adding a record type there is one line here. They exist because a macro is
 * invisible to SWIG, and that header cannot be handed to it wholesale: it also
 * carries a dozen vendor header structs nothing in a binding wants.
 */
enum TTTRRecordEncoding {
    RECORD_HHT2v2      = PQ_RECORD_TYPE_HHT2v2,
    RECORD_HHT2v1      = PQ_RECORD_TYPE_HHT2v1,
    RECORD_HHT3v1      = PQ_RECORD_TYPE_HHT3v1,
    RECORD_HHT3v2      = PQ_RECORD_TYPE_HHT3v2,
    RECORD_PHT3        = PQ_RECORD_TYPE_PHT3,
    RECORD_PHT2        = PQ_RECORD_TYPE_PHT2,
    RECORD_SPC130      = BH_RECORD_TYPE_SPC130,
    RECORD_SPC600_256  = BH_RECORD_TYPE_SPC600_256,
    RECORD_SPC600_4096 = BH_RECORD_TYPE_SPC600_4096,
    RECORD_CONFOCOR3   = CZ_RECORD_TYPE_CONFOCOR3,
    RECORD_SM          = SM_RECORD_TYPE,
    RECORD_GENERIC_T3  = PQ_RECORD_TYPE_GENERIC_T3,
    RECORD_GENERIC_T2  = PQ_RECORD_TYPE_GENERIC_T2,
    RECORD_SF_HT3      = PQ_RECORD_TYPE_SF_HT3,
    RECORD_SPCQC_X04   = BH_RECORD_TYPE_SPCQC_X04,
    RECORD_SPCQC_X06   = BH_RECORD_TYPE_SPCQC_X06,
    RECORD_TTR         = BE_RECORD_TYPE_TTR,
    RECORD_STT1        = FL_RECORD_TYPE_STT1,
    RECORD_ITT1        = FL_RECORD_TYPE_ITT1
};

// Declared, not defined: these live in TTTRRecordReader.h, which is a file of
// template record decoders that no binding has any use for. Repeating four
// signatures is what makes them reachable from Python, R, Java and JavaScript.
std::size_t record_bytes(int record_type);
std::string record_type_name(int record_type);
bool record_type_is_decodable(int record_type);
std::vector<int> decodable_record_types();

namespace tttrlib {

/*!
 * \brief What a container holds, learned without decoding any of it.
 *
 * Costs a header read. Everything in it comes from the header plus the file
 * size, which is why \ref n_records is available on a four-gigabyte file for
 * the price of a few kilobytes.
 */
struct ContainerRecords {
    /// The container this was read as, resolved from the file when not given.
    int container_type = -1;

    /// The record encoding, including the ones only the record stream reveals
    /// (an HT3 that turns out to be SF-compressed). -1 when unknown.
    int record_type = -1;

    /// How many records, from the file size and the header's record width.
    std::uint64_t n_records = 0;

    /// Bytes per record. Fixed by the encoding: 4 almost everywhere, 6 for
    /// SPC-600 in 4096-channel mode.
    std::uint64_t bytes_per_record = 0;

    /// Absolute file offset of the first record, i.e. where the header ended.
    std::uint64_t records_begin = 0;

    /// False when this container cannot be read in pieces; \ref reason says why.
    bool ranged = false;

    /// Empty on success. Otherwise the named decline -- which format it is and
    /// what about it makes a range meaningless.
    std::string reason;
};

/*!
 * \brief Describe a container's record stream without decoding it.
 *
 * \param spec A path, or a container path naming an object after a `|`.
 * \param container_type The container, or -1 to infer it from the file.
 */
ContainerRecords container_records(const std::string& spec, int container_type = -1);

/*!
 * \brief How many records a container holds, or 0 if it cannot say.
 *
 * Zero is also a legitimate answer for an empty file, so a caller that needs to
 * tell the two apart wants \ref container_records and its `reason`.
 */
std::uint64_t container_n_records(const std::string& spec, int container_type = -1);

/// True if \p container_type can be read in pieces. \see FileFormat::ranged_reads.
bool container_supports_ranged_reads(int container_type);

/*!
 * \brief Records [`first`, `first` + `n`) of a container, undecoded.
 *
 * The bytes as they are in the file, for \ref TTTR::decode_records or for a
 * caller that wants to look at them itself. A short read at the end of the file
 * comes back short rather than padded, and `n` = 0 means "to the end".
 *
 * \throws std::invalid_argument if the container cannot be read in pieces. The
 *         message names the format; \ref container_records says the same thing
 *         without throwing.
 */
std::vector<unsigned char> container_read_records(
        const std::string& spec,
        std::uint64_t first, std::uint64_t n,
        int container_type = -1);

/*!
 * \brief Records [`first`, `first` + `n`) of a container, decoded into \p out.
 *
 * \warning **Macro times count from record `first`, not from the start of the
 *          file**, unless `first` is 0. They have to: the overflow count at
 *          record `first` is not in the records, and finding it means reading
 *          every record before it, which is the cost a ranged read exists to
 *          avoid. A caller that needs absolute times over a whole file reads it
 *          in chunks from 0 and carries a \ref TTTRDecodeState -- which is the
 *          composition at the top of this file, and gives times identical to a
 *          whole-file read.
 *
 * \return 1 on success, 0 on failure.
 */
int container_read_events(
        const std::string& spec,
        std::uint64_t first, std::uint64_t n,
        TTTR* out,
        int container_type = -1);

}  // namespace tttrlib

#endif  // TTTRLIB_TTTRSTREAM_H
