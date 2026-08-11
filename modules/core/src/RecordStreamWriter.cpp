// SPDX-License-Identifier: BSD-3-Clause
#include "RecordStreamWriter.h"

#include "TTTR.h"
#include "TTTRHeader.h"
#include "TTTRHeaderTypes.h"
#include "TTTRFormat.h"
#include "TTTRTags.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

// Defined in TTTR.cpp, which owns the PTU record-type mapping. Declared here
// rather than copied: a second mapping is a second thing to keep in step.
int pq_ptu_record_type_identifier(int record_type);

namespace tttrlib {
namespace io {

namespace {

/// Does an encoder exist for this record type? \see RecordStreamWriter
bool has_encoder(int record_type) {
    switch (record_type) {
        case BH_RECORD_TYPE_SPC130:
        case BH_RECORD_TYPE_SPCQC_X04:
        case BH_RECORD_TYPE_SPCQC_X06:
        case BH_RECORD_TYPE_SPC600_256:
        case BH_RECORD_TYPE_SPC600_4096:
        case PQ_RECORD_TYPE_HHT3v2:
        case PQ_RECORD_TYPE_GENERIC_T3:
        case PQ_RECORD_TYPE_HHT3v1:
        case PQ_RECORD_TYPE_SF_HT3:
        case PQ_RECORD_TYPE_HHT2v2:
        case PQ_RECORD_TYPE_GENERIC_T2:
        case PQ_RECORD_TYPE_HHT2v1:
        case PQ_RECORD_TYPE_PHT3:
        case PQ_RECORD_TYPE_PHT2:
        case CZ_RECORD_TYPE_CONFOCOR3:
        case SM_RECORD_TYPE:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool record_stream_supported(int container_type, int record_type) {
    // Not a record stream at all: their layouts are a dataset tree, a
    // reconstructed position table or a bare word stream, none of which a
    // chunk can be appended to.
    switch (container_type) {
        case PHOTON_HDF_CONTAINER:
        case PS_PHOTONS_CONTAINER:
        case BE_TTR_CONTAINER:
        case FL_STT1_CONTAINER:
        case FL_ITT1_CONTAINER:
            return false;
        case SM_CONTAINER:
            // "Header plus records" is not one shape, and this is where that
            // bites. A PTU header is a tag list a reader walks to a terminator,
            // so an extra field is harmless; an SM header is a FIXED sequence
            // of fields, and the header this writer produces came out 280 bytes
            // where the reader parses 176 -- the payload was then offset by 104
            // and the file read as zero events.
            //
            // Declined rather than left to write a file its own reader cannot
            // parse: a named refusal is recoverable and a silently unreadable
            // acquisition is not. Whole-file TTTR.write to .sm is unaffected.
            // See BUGS.md; closing it means building the header exactly as
            // TTTR::write does for this container rather than running
            // ensure_minimal_tags over a copy.
            return false;
        default:
            break;
    }
    // Through the registry rather than a switch: the container/record table
    // lives there now, and asking it is what keeps a plugin-provided format
    // answering the same question as a built-in one.
    const FileFormat* f = IORegistry::by_container_type(container_type);
    if (f == nullptr) return false;
    int rt = record_type;
    if (rt < 0 || !f->accepts_record_type(rt)) rt = f->default_record_type;
    if (rt < 0 || !f->accepts_record_type(rt)) return false;
    return has_encoder(rt);
}

struct RecordStreamWriter::Impl {
    int container = -1;
    int record = -1;
    std::string filename;
    std::FILE* fp = nullptr;
    TTTRHeader header;
    bool have_header = false;

    /// Carried across chunks. The whole reason the encoders were made resumable.
    std::uint64_t mt_ov = 0;
    std::uint64_t records = 0;

    /// How long the header came out, so a regenerated one can be checked
    /// against it before being written over the records.
    std::size_t header_bytes = 0;
};

RecordStreamWriter::RecordStreamWriter(int container_type, int record_type)
        : p_(new Impl) {
    p_->container = container_type;
    p_->record = record_type;
}

RecordStreamWriter::~RecordStreamWriter() {
    if (is_open()) {
        try { close(); } catch (...) {}
    }
}

std::uint64_t RecordStreamWriter::n_records() const { return p_->records; }

bool RecordStreamWriter::open_target(const std::string& filename, TTTRHeader* header,
                                     const std::string& name) {
    (void) name;   // a record stream holds one measurement and has no place for it
    Impl& m = *p_;

    if (m.container < 0) return fail("no container type was given");
    // The same predicate the factory answers with, so create() and
    // can_stream() cannot disagree -- open_target used to repeat the checks
    // and missed the containers record_stream_supported excludes.
    if (!record_stream_supported(m.container, m.record))
        return fail("container " + std::to_string(m.container) +
                    " cannot be streamed into as a record stream");
    const FileFormat* f = IORegistry::by_container_type(m.container);
    if (f == nullptr) return fail("no format with container type " +
                                  std::to_string(m.container));
    int rt = m.record;
    if (rt < 0 || !f->accepts_record_type(rt)) rt = f->default_record_type;
    if (rt < 0 || !f->accepts_record_type(rt))
        return fail("container " + std::to_string(m.container) +
                    " and record type " + std::to_string(m.record) +
                    " do not go together");
    if (!has_encoder(rt))
        return fail("record type " + std::to_string(rt) +
                    " has no encoder, so it cannot be streamed into");
    m.record = rt;

    // A copy: writing stamps the container and record type into the header,
    // and doing that to the caller's object would leave it describing the file
    // just written.
    m.header = TTTRHeader(*header);
    m.header.set_tttr_container_type(m.container);
    m.header.set_tttr_record_type(m.record);
    // The count is unknown at open. Zero is the honest placeholder and is
    // patched at every checkpoint; see the class documentation.
    TTTRHeader::ensure_minimal_tags(&m.header, m.container, 0);
    if (m.container == PQ_PTU_CONTAINER) {
        // json_data() is protected, so this goes through the public JSON.
        nlohmann::json j = nlohmann::json::parse(m.header.get_json(), nullptr, false);
        if (!j.is_discarded()) {
            TTTRHeader::add_tag(j, TTTRTagTTTRRecType,
                                ::pq_ptu_record_type_identifier(m.record), tyInt8);
            m.header.set_json(j.dump());
        }
    }
    m.have_header = true;

    // TTTR::write_header is what every whole-file write uses, so a streamed
    // file and a written one have byte-identical headers.
    TTTR probe;
    std::string fn = filename;
    probe.write_header(fn, &m.header);

    m.fp = std::fopen(filename.c_str(), "ab");
    if (m.fp == nullptr) return fail("cannot open " + filename + " for writing");
    m.header_bytes = (std::size_t) std::ftell(m.fp);
    m.filename = filename;
    m.mt_ov = 0;
    m.records = 0;
    return true;
}

bool RecordStreamWriter::write_chunk(const std::uint64_t* macro_times,
                                     const std::uint16_t* micro_times,
                                     const std::int8_t* routing_channels,
                                     const std::int8_t* event_types,
                                     std::size_t n, bool durable) {
    Impl& m = *p_;
    if (m.fp == nullptr) return fail("this stream is not open");

    if (n > 0) {
        // A TTTR over the chunk, so the encoders are reached exactly as
        // TTTR::write reaches them -- one encoder, not a second copy of one.
        TTTR chunk;
        chunk.append_events(
                const_cast<unsigned long long*>(
                        reinterpret_cast<const unsigned long long*>(macro_times)),
                static_cast<int>(n),
                const_cast<unsigned short*>(
                        reinterpret_cast<const unsigned short*>(micro_times)),
                static_cast<int>(n),
                const_cast<signed char*>(
                        reinterpret_cast<const signed char*>(routing_channels)),
                static_cast<int>(n),
                const_cast<signed char*>(
                        reinterpret_cast<const signed char*>(event_types)),
                static_cast<int>(n), false, 0);

        const long before = std::ftell(m.fp);
        TTTR w;
        switch (m.record) {
            case BH_RECORD_TYPE_SPC130:
                w.write_spc132_events(m.fp, &chunk, &m.mt_ov); break;
            case BH_RECORD_TYPE_SPCQC_X04:
                w.write_spcqc_events(m.fp, &chunk, false, &m.mt_ov); break;
            case BH_RECORD_TYPE_SPCQC_X06:
                w.write_spcqc_events(m.fp, &chunk, true, &m.mt_ov); break;
            case BH_RECORD_TYPE_SPC600_256:
                w.write_spc600_256_events(m.fp, &chunk, &m.mt_ov); break;
            case BH_RECORD_TYPE_SPC600_4096:
                w.write_spc600_4096_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_HHT3v2:
            case PQ_RECORD_TYPE_GENERIC_T3:
                w.write_hht3v2_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_HHT3v1:
                w.write_hht3v1_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_SF_HT3:
                w.write_sf_ht3_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_HHT2v2:
            case PQ_RECORD_TYPE_GENERIC_T2:
                w.write_hht2v2_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_HHT2v1:
                w.write_hht2v1_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_PHT3:
                w.write_pht3_events(m.fp, &chunk, &m.mt_ov); break;
            case PQ_RECORD_TYPE_PHT2:
                w.write_pht2_events(m.fp, &chunk, &m.mt_ov); break;
            case CZ_RECORD_TYPE_CONFOCOR3:
                w.write_cz_events(m.fp, &chunk); break;
            case SM_RECORD_TYPE:
                w.write_sm_events(m.fp, &chunk); break;
            default:
                return fail("record type " + std::to_string(m.record) +
                            " has no encoder");
        }
        const long after = std::ftell(m.fp);
        if (after < before) return fail("the record encoder did not advance the file");
        // Records, not events: an overflow record is written and is not a photon.
        const int width = m.header.get_bytes_per_record();
        m.records += static_cast<std::uint64_t>(after - before) / (width > 0 ? width : 4);
    }

    if (durable) {
        std::fflush(m.fp);
        if (!patch_record_count()) return false;
    }
    return true;
}

/*!
 * \brief Put the real record count into the header, in place.
 *
 * A vendor header states how many records follow and must be written before
 * any of them, so the count starts as a placeholder. It is regenerated here
 * with the true value and written over the original bytes -- and **only** if
 * it came out the same length, because a header that grew would overwrite the
 * first records and one that shrank would leave a gap. A PTU tag value is
 * fixed width and an HT3 header is a fixed struct, so the length is stable in
 * practice; the check is what makes "in practice" safe rather than assumed.
 *
 * A writer killed between the last record and this call leaves a file claiming
 * FEWER records than it holds. The extra ones are ignored rather than
 * misread, which is the safe direction to be wrong in.
 */
bool RecordStreamWriter::patch_record_count() {
    Impl& m = *p_;
    if (m.header_bytes == 0) return true;

    TTTRHeader updated(m.header);
    {
        nlohmann::json j = nlohmann::json::parse(updated.get_json(), nullptr, false);
        if (j.is_discarded()) return true;
        TTTRHeader::add_tag(j, TTTRTagNumRecords, (int) m.records, tyInt8);
        updated.set_json(j.dump());
    }

    // Regenerated through the same TTTR::write_header every whole-file write
    // uses, into a scratch file, so a streamed header and a written one cannot
    // drift apart.
    const std::string tmp = m.filename + ".hdr.tmp";
    {
        TTTR probe;
        std::string fn = tmp;
        probe.write_header(fn, &updated);
    }
    std::vector<unsigned char> bytes;
    if (std::FILE* h = std::fopen(tmp.c_str(), "rb")) {
        std::fseek(h, 0, SEEK_END);
        const long len = std::ftell(h);
        std::fseek(h, 0, SEEK_SET);
        if (len > 0) {
            bytes.resize((std::size_t) len);
            if (std::fread(bytes.data(), 1, bytes.size(), h) != bytes.size())
                bytes.clear();
        }
        std::fclose(h);
    }
    std::remove(tmp.c_str());

    if (bytes.size() != m.header_bytes) {
        // Not patched rather than patched wrongly. The file stays readable
        // with the older count.
        return true;
    }

    const long here = std::ftell(m.fp);
    if (std::FILE* patch = std::fopen(m.filename.c_str(), "r+b")) {
        std::fwrite(bytes.data(), 1, bytes.size(), patch);
        std::fflush(patch);
        std::fclose(patch);
    }
    std::fseek(m.fp, here, SEEK_SET);
    return true;
}

bool RecordStreamWriter::close_target() {
    Impl& m = *p_;
    if (m.fp != nullptr) {
        std::fflush(m.fp);
        patch_record_count();
        std::fclose(m.fp);
        m.fp = nullptr;
    }
    return true;
}

namespace {

/// \see FileFormat::make_stream_writer. The caller takes ownership.
struct RegisterRecordStreams {
    RegisterRecordStreams() {
        // Every container that is a header plus records gets the same writer,
        // told which one it is. One implementation, not one per format -- the
        // formats differ by an encoder call and nothing else.
        static const int kContainers[] = {
            PQ_PTU_CONTAINER, PQ_HT3_CONTAINER,
            BH_SPC130_CONTAINER, BH_SPC600_256_CONTAINER,
            BH_SPC600_4096_CONTAINER, BH_SPCQC_CONTAINER,
            CZ_CONFOCOR3_CONTAINER, SM_CONTAINER,
        };
        for (int c : kContainers) {
            if (!record_stream_supported(c)) continue;
            const FileFormat* f = IORegistry::by_container_type(c);
            if (f == nullptr) continue;
            IORegistry::set_stream_writer(
                    f->name,
                    [](void* ctx) -> void* {
                        const int container = (int) (std::intptr_t) ctx;
                        return static_cast<TTTRStreamWriter*>(
                                new RecordStreamWriter(container));
                    },
                    (void*) (std::intptr_t) c);
        }
    }
};
const RegisterRecordStreams register_record_streams;

}  // namespace

}  // namespace io
}  // namespace tttrlib
