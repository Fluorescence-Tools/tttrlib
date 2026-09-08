// SPDX-License-Identifier: BSD-3-Clause
//
// tttrlib's half of the PTO container: what needs a photon library. The
// container itself is ptolib (thirdparty/ptolib/ptolib.h), compiled once in
// modules/core/src/DataStore.cpp.
#include "io_pto.h"
#include "FileIO.h"

#include "io_store.h"
#include "FileCheck.h"
#include "TTTR.h"
#include "TTTRFormat.h"

#include "TTTRRecordReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace tttrlib {
namespace io {

// --- the banner, and construction ---------------------------------------------

namespace {
// ptolib's decoding note, with tttrlib's own first lines in front of it: what
// this container is, and where a reader for photon containers is found.
const std::string kBannerText =
        "pto\n"
        "This is a .pto photon container (tttrlib).\n"
        "Get a reader: https://github.com/Fluorescence-Tools/tttrlib/releases\n"
        "\n" +
        std::string(pto::kDefaultBanner).substr(4);   // past ptolib's own "pto\n"
}  // namespace

const char* const kTttrlibBanner = kBannerText.c_str();

PtoFile::PtoFile() = default;
PtoFile::~PtoFile() = default;

bool PtoFile::create(const std::string& filename, const std::string& title) {
    return pto::File::create(filename, title, kBannerText);
}

// --- inspection data -----------------------------------------------------------

bool PtoFile::add_inspection_trace(const std::vector<std::uint32_t>& counts, double dt_s) {
    if (!is_open() || counts.empty()) return false;
    std::size_t nbytes = counts.size() * sizeof(std::uint32_t);
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(counts.data());
    std::uint64_t trace_uid = add("trace", "uint32", "time_trace", ptr, nbytes);
    if (trace_uid != 0) {
        PtoTag t1;
        t1.name = "trace_dt_s";
        t1.type = PtoType::Float;
        t1.d = dt_s;
        t1.target = trace_uid;
        add_tag(t1);

        PtoTag t2;
        t2.name = "trace_bins";
        t2.type = PtoType::UInt;
        t2.u = counts.size();
        t2.target = trace_uid;
        add_tag(t2);
        return true;
    }
    return false;
}

bool PtoFile::add_inspection_decay(int channel, const std::vector<std::uint32_t>& counts, double microtime_ns) {
    if (!is_open() || counts.empty()) return false;
    std::size_t nbytes = counts.size() * sizeof(std::uint32_t);
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(counts.data());
    std::string obj_name = "decay_ch" + std::to_string(channel);
    std::uint64_t decay_uid = add("decay", "uint32", obj_name, ptr, nbytes);
    if (decay_uid != 0) {
        PtoTag t1;
        t1.name = "decay_channel";
        t1.type = PtoType::UInt;
        t1.u = channel;
        t1.target = decay_uid;
        add_tag(t1);

        PtoTag t2;
        t2.name = "decay_bins";
        t2.type = PtoType::UInt;
        t2.u = counts.size();
        t2.target = decay_uid;
        add_tag(t2);

        PtoTag t3;
        t3.name = "microtime_resolution_ns";
        t3.type = PtoType::Float;
        t3.d = microtime_ns;
        t3.target = decay_uid;
        add_tag(t3);
        return true;
    }
    return false;
}

bool PtoFile::add_inspection_metadata(const std::string& metadata_json) {
    if (!is_open() || metadata_json.empty()) return false;
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(metadata_json.data());
    return add("metadata", "json", "tttr_metadata", ptr, metadata_json.size()) != 0;
}

bool PtoFile::add_inspection_data(const std::vector<std::uint32_t>& trace_counts,
                                  double trace_dt_s,
                                  const std::vector<std::uint32_t>& decay_counts,
                                  double microtime_ns,
                                  const std::string& metadata_json) {
    bool ok = true;
    if (!trace_counts.empty()) ok &= add_inspection_trace(trace_counts, trace_dt_s);
    if (!decay_counts.empty()) ok &= add_inspection_decay(0, decay_counts, microtime_ns);
    if (!metadata_json.empty()) ok &= add_inspection_metadata(metadata_json);
    return ok;
}

std::uint64_t PtoFile::add_sidecar_file(const std::string& kind, const std::string& encoding,
                                       const std::string& name, const std::string& file_path) {
    std::string store_path = file_path;
    try {
        std::filesystem::path fp = std::filesystem::u8path(file_path);
        std::filesystem::path base = std::filesystem::u8path(filename()).parent_path();
        if (!base.empty()) {
            std::error_code ec;
            std::filesystem::path rel = std::filesystem::relative(fp, base, ec);
            if (!ec && !rel.empty()) {
                store_path = rel.string();
            }
        }
    } catch (...) {}

    std::uint64_t uid = add(kind, encoding, name, nullptr, 0, 0);
    if (!uid) return 0;

    PtoTag tag_path;
    tag_path.target = uid;
    tag_path.name = "_mmfdb_artifact.file_path";
    tag_path.type = PtoType::Text;
    tag_path.text = store_path;
    add_tag(tag_path);

    PtoTag tag_sidecar;
    tag_sidecar.target = uid;
    tag_sidecar.name = "_mmfdb_artifact.is_sidecar";
    tag_sidecar.type = PtoType::Text;
    tag_sidecar.text = "true";
    add_tag(tag_sidecar);

    return uid;
}

std::string PtoFile::external_payload_path(std::uint64_t uid) const {
    for (const auto& tag : tags_for(uid)) {
        if (tag.name == "_mmfdb_artifact.file_path" && tag.type == PtoType::Text) {
            std::filesystem::path p(tag.text);
            if (p.is_absolute()) return p.string();
            std::filesystem::path base = std::filesystem::u8path(filename()).parent_path();
            return (base / p).string();
        }
    }
    return "";
}

// --- opening the photon data inside a container -------------------------------

namespace {

/// What an encoding means to the photon reader. Only the record-stream
/// containers can be read where they lie; the rest read by path.
struct Readable { const char* encoding; const char* container; };
const Readable kReadable[] = {
    {"ptu", "PTU"}, {"ht3", "HT3"}, {"spc", "SPC-130"}, {"spc-130", "SPC-130"},
    {"spc-600", "SPC-600_256"}, {"spc-qc", "SPC-QC"},
    {"cz-raw", "CZ-CONFOCOR3"}, {"sm", "SM"},
};

std::string lowered(std::string s) {
    for (std::size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = static_cast<char>(s[i] + ('a' - 'A'));
    return s;
}

/*!
 * \brief The container id an encoding reads as, or -1.
 *
 * A tttrlib container NAME is tried first, so a writer that knows exactly what
 * it embedded can say so: four different formats claim the extension "spc", and
 * an SPC-QC stored as "spc" would come back as an SPC-130 -- readable, wrong,
 * and silent about it. The extension names below stay for a file somebody wrote
 * by hand, where "ptu" is the obvious thing to put.
 */
int container_for(const std::string& encoding) {
    const std::string e = lowered(encoding);
    const std::vector<FileFormat>& all = IORegistry::formats();
    for (std::size_t i = 0; i < all.size(); i++)
        if (lowered(all[i].name) == e) return all[i].container_type;
    for (std::size_t i = 0; i < sizeof(kReadable) / sizeof(kReadable[0]); i++) {
        if (e != kReadable[i].encoding) continue;
        const FileFormat* f = IORegistry::by_name(kReadable[i].container);
        return f ? f->container_type : -1;
    }
    return -1;
}

bool holds_photons(const PtoObject& o) {
    return container_for(o.encoding) >= 0 ||
           (lowered(o.encoding) == "dstore" && o.kind == "photons");
}

/// A `.set` that accompanies `uid`, as bytes, or empty. \see kSidecarTag.
std::string sidecar_text(const PtoFile& file, std::uint64_t uid) {
    const std::vector<PtoTag> tags = file.tags();
    for (std::size_t i = 0; i < tags.size(); i++) {
        if (tags[i].name != kPtoSidecarTag || tags[i].type != PtoType::UID) continue;
        if (tags[i].u != uid) continue;
        const PtoObject o = file.object(tags[i].target);
        const std::string name = lowered(o.name);
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".set") != 0) continue;
        const std::vector<unsigned char> bytes = file.read(tags[i].target);
        return std::string(bytes.begin(), bytes.end());
    }
    return std::string();
}

/*!
 * \brief The namespace every preserved source-header row is written under.
 *
 * A PTU calls a tag `ImgHdr_PixX`; writing that name bare into a container
 * would claim an authority nobody holds (doc/formats/pto.rst, "Why two
 * namespaces"). The prefix says what the name *is* -- a row of the instrument
 * header, kept verbatim -- without pretending PTO or MMFDB defines it. The
 * original type code rides along in \ref PtoTag::source_type and the array
 * position in \ref PtoTag::index, which is what those two fields were put in
 * the format for.
 */
const char* const kSourceHeaderPrefix = "_pto_source_header.";

/// The `PtoType` that carries a PTU tag of type \p ty without losing anything.
PtoType pto_type_for_source(std::uint32_t ty) {
    switch (ty) {
        case tyEmpty8:      return PtoType::Empty;
        case tyBool8:
        case tyInt8:
        case tyBitSet64:
        case tyColor8:      return PtoType::Int;
        case tyFloat8:
        // A TDateTime is a double in the source and stays one here. PtoType::Date
        // would be the tidier home but is integer nanoseconds, so routing through
        // it would round the value -- and source_type already records what it was.
        case tyTDateTime:   return PtoType::Float;
        case tyFloat8Array: return PtoType::Floats;
        case tyAnsiString:
        case tyWideString:  return PtoType::Text;
        case tyBinaryBlob:  return PtoType::Bytes;
        default:            return PtoType::Text;
    }
}

/*!
 * \brief Every row of `header` as a tag on `uid`. \see kSourceHeaderPrefix.
 *
 * "Open fidelity" in the specification: the three required tags say what the
 * photons *are*, and this says everything else the instrument said. Losing it
 * turns an imaging measurement into an unreconstructable list of photons --
 * `ImgHdr_*` is what CLSM configures itself from -- so preserving everything is
 * the default rather than a curated subset. A curated subset is also a
 * judgement about which instrument settings matter, which is not the
 * container's to make.
 */
void write_source_header_tags(PtoFile& file, std::uint64_t uid, TTTRHeader* header) {
    if (header == nullptr) return;
    // get_json() rather than json_data(): the latter is protected, and this
    // module is above core rather than part of it.
    const nlohmann::json j = nlohmann::json::parse(header->get_json(), nullptr, false);
    if (j.is_discarded() || !j.contains("tags") || !j["tags"].is_array()) return;

    for (const nlohmann::json& row : j["tags"]) {
        if (!row.is_object() || !row.contains("name")) continue;
        const std::uint32_t ty =
                row.value("type", static_cast<std::uint32_t>(tyAnsiString));

        PtoTag t;
        t.target = uid;
        t.name = kSourceHeaderPrefix + row["name"].get<std::string>();
        t.index = row.value("idx", -1);
        t.source_type = ty;
        t.type = pto_type_for_source(ty);

        const nlohmann::json& v = row.contains("value") ? row["value"] : nlohmann::json();
        switch (t.type) {
            case PtoType::Empty: break;
            case PtoType::Int:
                if (v.is_boolean()) t.i = v.get<bool>() ? 1 : 0;
                else if (v.is_number()) t.i = v.get<long long>();
                break;
            case PtoType::Float:
                if (v.is_number()) t.d = v.get<double>();
                break;
            case PtoType::Floats:
                if (v.is_array()) for (const auto& e : v)
                    if (e.is_number()) t.floats.push_back(e.get<double>());
                break;
            case PtoType::Bytes:
                // One int32 per byte, the representation the header JSON uses.
                if (v.is_array()) for (const auto& e : v)
                    if (e.is_number()) t.bytes.push_back(
                            static_cast<unsigned char>(e.get<long long>() & 0xFF));
                break;
            default:
                t.type = PtoType::Text;
                t.text = v.is_string() ? v.get<std::string>() : v.dump();
                break;
        }
        file.add_tag(t);
    }
}

/*!
 * \brief Put the preserved source-header rows back into `out`'s header JSON.
 *
 * The inverse of \ref write_source_header_tags, and it restores the original
 * `(name, idx, type, value)` -- not an approximation of it -- because
 * `source_type` recorded the type code the row actually had. Without this the
 * tags survive in the file and are invisible to everything that reads a header,
 * which is the same as not having written them.
 */
void apply_source_header_tags(const PtoFile& file, std::uint64_t uid, TTTR* out) {
    TTTRHeader* h = out->get_header();
    if (h == nullptr) return;
    const std::size_t prefix_len = std::strlen(kSourceHeaderPrefix);

    nlohmann::json j = nlohmann::json::parse(h->get_json(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) j = nlohmann::json::object();
    if (!j.contains("tags") || !j["tags"].is_array()) j["tags"] = nlohmann::json::array();
    bool any = false;

    for (const PtoTag& t : file.tags_for(uid)) {
        if (t.name.compare(0, prefix_len, kSourceHeaderPrefix) != 0) continue;
        const std::string name = t.name.substr(prefix_len);
        const std::uint32_t ty = t.source_type != 0 ? t.source_type : tyAnsiString;

        nlohmann::json row;
        row["name"] = name;
        row["type"] = ty;
        row["idx"] = t.index;
        switch (t.type) {
            case PtoType::Empty: row["value"] = nullptr; break;
            case PtoType::Int:
                // Restored to the JSON type the source reader wrote, so a
                // round trip through the container is byte-identical rather
                // than merely equal-valued.
                if (ty == tyBool8) row["value"] = (t.i != 0);
                else row["value"] = t.i;
                break;
            case PtoType::Float: row["value"] = t.d; break;
            case PtoType::Floats: row["value"] = t.floats; break;
            case PtoType::Bytes: {
                std::vector<long long> blob;
                blob.reserve(t.bytes.size());
                for (unsigned char b : t.bytes) blob.push_back(b);
                row["value"] = blob;
                break;
            }
            default: row["value"] = t.text; break;
        }
        j["tags"].emplace_back(row);
        any = true;
    }
    // set_json replaces the whole document, so this must run before whatever
    // configures the header from the required tags -- see the call site.
    if (any) h->set_json(j.dump());
}

/*!
 * \brief Apply a native photons object's header tags to the TTTR it produced.
 *
 * Without this the reader hands back a column of integers with no unit: the
 * events load, and every derived quantity -- a lifetime, a correlation lag, a
 * CLSM reconstruction -- is silently in the wrong units or impossible. The
 * tags are the header, and they are required by the format for exactly that
 * reason (see doc/formats/pto.rst, "Photon streams, natively").
 *
 * Naming follows the specification rather than convenience: the two clock tags
 * are MMFDB dictionary terms and are spelled as the dictionary spells them,
 * because a term that already exists must not be re-coined. The bin count has
 * no MMFDB term and deliberately does *not* borrow the near-misses --
 * `_mmfdb_setup.n_bins` is FCS correlator bins and `micro_time_binning` is a
 * factor, not a count -- so it lives in PTO's own namespace until MMFDB
 * defines one.
 *
 * \return true when every required tag was present and applied.
 */
bool apply_photon_header(const PtoFile& file, std::uint64_t uid, TTTR* out) {
    TTTRHeader* h = out->get_header();
    if (h == nullptr) return false;

    bool have_macro = false, have_micro = false, have_bins = false;
    for (const PtoTag& t : file.tags_for(uid)) {
        if (t.name == "_mmfdb_setup.macro_time_resolution") {
            h->set_macro_time_resolution(t.d);
            have_macro = true;
        } else if (t.name == "_mmfdb_setup.micro_time_resolution") {
            h->set_micro_time_resolution(t.d);
            have_micro = true;
        } else if (t.name == "_pto_photons.number_of_micro_time_channels") {
            // Int tags arrive in `i`; tolerate an unsigned writer using `u`.
            const long long n = t.i != 0 ? t.i : static_cast<long long>(t.u);
            if (n > 0) { h->set_number_of_micro_time_channels(static_cast<int>(n)); have_bins = true; }
        } else if (t.name == "_pto_photons.source_container_type") {
            // Provenance, and the one thing it decides: which marker
            // convention the source format used. See the writer.
            h->set_tttr_container_type(static_cast<int>(t.i));
        } else if (t.name == "_pto_photons.source_record_type") {
            h->set_tttr_record_type(static_cast<int>(t.i));
        }
    }
    return have_macro && have_micro && have_bins;
}

/*!
 * \brief `n_rows` rows of a native photons table, starting at `first_row`.
 *
 * What makes a range over a native table cheap, and the reason a
 * cue index is not needed here: a dstore knows where every row of every column
 * begins, so asking for 5,000 events out of 870,161 reads 5,000 events. The
 * record-stream path cannot do that -- a record stream has to be decoded from
 * somewhere known to be counted at all, which is what cues exist for.
 *
 * Without this the native path fell back to reading the whole object and
 * slicing it in memory, which is correct and was **9x slower** than the
 * embedded-PTU-with-cues path it is supposed to beat.
 *
 * \param n_rows 0 means "to the end".
 */
bool read_one_rows(const PtoFile& file, const PtoObject& o, TTTR* out,
                   std::uint64_t first_row, std::uint64_t n_rows) {
    data::DataStore store;
    pto_read_store(file, o.uid, store, std::vector<std::string>(), first_row, n_rows);
    const int mt = store.find("macro_time"), ut = store.find("micro_time");
    const int rc = store.find("routing_channel"), et = store.find("event_type");
    const std::size_t n = store.n_rows();
    std::vector<unsigned long long> macro(n, 0);
    std::vector<unsigned short> micro(n, 0);
    std::vector<signed char> chan(n, 0), type(n, 0);
    for (std::size_t i = 0; i < n; i++) {
        if (mt >= 0) macro[i] = static_cast<unsigned long long>(store.column(mt).value_at(i));
        if (ut >= 0) micro[i] = static_cast<unsigned short>(store.column(ut).value_at(i));
        if (rc >= 0) chan[i] = static_cast<signed char>(store.column(rc).value_at(i));
        if (et >= 0) type[i] = static_cast<signed char>(store.column(et).value_at(i));
    }
    out->append_events(macro.data(), static_cast<int>(n), micro.data(),
                       static_cast<int>(n), chan.data(), static_cast<int>(n),
                       type.data(), static_cast<int>(n), false, 0);
    apply_source_header_tags(file, o.uid, out);
    apply_photon_header(file, o.uid, out);
    return true;
}

/// One object into `out`, read where it lies.
bool read_one(const PtoFile& file, const std::string& path, const PtoObject& o,
              TTTR* out) {
    if (lowered(o.encoding) == "dstore") {
        data::DataStore store;
        pto_read_store(file, o.uid, store);
        const int mt = store.find("macro_time"), ut = store.find("micro_time");
        const int rc = store.find("routing_channel"), et = store.find("event_type");
        const std::size_t n = store.n_rows();
        std::vector<unsigned long long> macro(n, 0);
        std::vector<unsigned short> micro(n, 0);
        std::vector<signed char> chan(n, 0), type(n, 0);
        for (std::size_t i = 0; i < n; i++) {
            if (mt >= 0) macro[i] = static_cast<unsigned long long>(store.column(mt).value_at(i));
            if (ut >= 0) micro[i] = static_cast<unsigned short>(store.column(ut).value_at(i));
            if (rc >= 0) chan[i] = static_cast<signed char>(store.column(rc).value_at(i));
            if (et >= 0) type[i] = static_cast<signed char>(store.column(et).value_at(i));
        }
        out->append_events(macro.data(), static_cast<int>(n), micro.data(),
                           static_cast<int>(n), chan.data(), static_cast<int>(n),
                           type.data(), static_cast<int>(n), false, 0);
        // The events are only half of it. Without the header tags this returns
        // dimensionless integers, which is what a native photons object looked
        // like before -- see apply_photon_header.
        //
        // Source rows FIRST: they go in through set_json, which replaces the
        // whole document, so applying them after the three required tags would
        // discard exactly what those tags configured.
        apply_source_header_tags(file, o.uid, out);
        apply_photon_header(file, o.uid, out);
        return true;
    }
    const int container = container_for(o.encoding);
    if (container < 0) return false;
    return out->read_embedded(path.c_str(), container, o.offset, o.size,
                              sidecar_text(file, o.uid)) != 0;
}

}  // namespace

// --- cues, and positioning in a photon stream ---------------------------------

std::uint64_t PtoFile::build_cues(std::uint64_t uid, std::uint64_t every_n_events) {
    set_error("");
    if (!has(uid)) { set_error("no object with that uid"); return 0; }
    const PtoObject meta = object(uid);
    if (every_n_events == 0) { set_error("a cue spacing of zero indexes nothing"); return 0; }
    // A native PHOTONS table needs no index and saying so is not a failure.
    // Narrowly a photons object, not any dstore: a burst table is also a
    // dstore and a cue into one would index nothing, which is a genuine error
    // and stays one.
    // A cue exists to answer "where does event N start" for a record stream,
    // which has to be decoded from the beginning to be counted. Columnar
    // storage answers it arithmetically: row N is at a known offset, so the
    // seek IS the row number. Returning 0 cues with no error is the honest
    // report -- an error would make a caller that indexes before reading think
    // the object was unreadable, when it is the one kind that never needed the
    // index. \see pto_read_events, which slices such a table directly.
    if (holds_photons(meta) && lowered(meta.encoding) == "dstore") return 0;

    const int container = container_for(meta.encoding);
    if (container < 0) {
        set_error("object " + std::to_string(uid) + " is encoded as '" + meta.encoding +
                  "', which is not a record stream this build can index");
        return 0;
    }
    const std::uint64_t payload_at = meta.offset, payload_bytes = meta.size;
    flush();

    // The header says how wide a record is and how to decode one; nothing else
    // is read here, and no events are materialised.
    TTTR probe;
    if (!probe.open_embedded(filename().c_str(), container, payload_at, payload_bytes,
                             sidecar_text(*this, uid))) {
        set_error("could not read the header of object " + std::to_string(uid));
        return 0;
    }
    const std::size_t width = probe.get_header()->get_bytes_per_record();
    const std::uint64_t records_at = probe.get_records_begin();
    const std::uint64_t n_records = probe.n_records_in_file;
    const int record_type = probe.get_tttr_record_type();
    if (width == 0 || n_records == 0) { set_error("the object holds no records"); return 0; }

    std::FILE* in = open_file(filename(), "rb", false);
    if (in == nullptr || fseek64(in, static_cast<std::int64_t>(records_at), SEEK_SET) != 0) {
        if (in != nullptr) std::fclose(in);
        set_error("cannot reach the records of " + std::to_string(uid));
        return 0;
    }

    const std::size_t kChunk = 1u << 16;
    std::vector<signed char> raw(kChunk * width);
    std::vector<unsigned long long> macro(kChunk);
    std::vector<unsigned short> micro(kChunk);
    std::vector<signed char> chan(kChunk), type(kChunk);

    std::vector<PtoCue> made;
    std::uint64_t overflow = 0, events = 0, next_cue = 0, done = 0;
    while (done < n_records) {
        const std::size_t take =
                static_cast<std::size_t>(n_records - done < kChunk ? n_records - done : kChunk);
        if (std::fread(raw.data(), 1, take * width, in) != take * width) break;

        // Whole chunk first: if no cue falls in it, the per-record walk below is
        // wasted work, and on a 10^9-event stream at one cue per 10^6 that is
        // the overwhelming majority of chunks.
        const std::uint64_t overflow_before = overflow;
        std::size_t valid = 0;
        if (!dispatch_process_records_batch(record_type, raw.data(), take, width,
                                            overflow, macro.data(), micro.data(),
                                            chan.data(), type.data(), valid)) {
            std::fclose(in);
            set_error("this build cannot decode record type " + std::to_string(record_type));
            return 0;
        }
        if (events + valid <= next_cue) { events += valid; done += take; continue; }

        // A cue lands in here somewhere, so walk it a record at a time to find
        // which record made which event.
        overflow = overflow_before;
        for (std::size_t r = 0; r < take; r++) {
            std::size_t one = 0;
            dispatch_process_records_batch(record_type, raw.data() + r * width, 1, width,
                                           overflow, macro.data(), micro.data(),
                                           chan.data(), type.data(), one);
            if (one == 0) continue;
            if (events == next_cue) {
                PtoCue c;
                c.event = events;
                c.offset = records_at + (done + r) * width - payload_at;
                c.time = macro[0];
                made.push_back(c);
                next_cue += every_n_events;
            }
            events++;
        }
        done += take;
    }
    std::fclose(in);

    set_cues(uid, made);
    return made.size();
}

namespace {

/*!
 * \brief The cue at or before `event`, and the one at or after `end`.
 *
 * "At or before" is the whole discipline: a cue is advisory, so a decode starts
 * no later than the truth and walks forward to it. A cue that is wrong then
 * costs time and cannot cost correctness.
 */
void bracket(const std::vector<PtoCue>& cues, std::uint64_t first, std::uint64_t end,
             PtoCue* from, bool* have_from, std::uint64_t* stop_at) {
    *have_from = false;
    *stop_at = 0;
    for (std::size_t i = 0; i < cues.size(); i++) {
        if (cues[i].event <= first && (!*have_from || cues[i].event > from->event)) {
            *from = cues[i];
            *have_from = true;
        }
    }
    if (end == 0) return;
    for (std::size_t i = 0; i < cues.size(); i++) {
        if (cues[i].event >= end && (*stop_at == 0 || cues[i].offset < *stop_at))
            *stop_at = cues[i].offset;
    }
}

/*!
 * \brief Trim a decoded TTTR down to events [`skip`, `skip` + `want`).
 *
 * A decode that started at a cue overshoots at both ends by construction; this
 * is where the caller's actual range is cut out of it.
 */
void keep_range(TTTR* t, std::uint64_t skip, std::uint64_t want) {
    const std::size_t have = t->size();
    if (skip == 0 && (want == 0 || want >= have)) return;
    const std::size_t from = skip < have ? static_cast<std::size_t>(skip) : have;
    const std::size_t left = have - from;
    const std::size_t take = (want == 0 || want > left) ? left : static_cast<std::size_t>(want);
    std::vector<int> keep(take);
    for (std::size_t i = 0; i < take; i++) keep[i] = static_cast<int>(from + i);
    TTTR cut(*t, keep.empty() ? nullptr : keep.data(), static_cast<int>(keep.size()), false);
    t->copy_from(cut, true);
}

}  // namespace
namespace {
/// The extension, lowercased and without the dot. Empty when there is none.
std::string extension_of(const std::string& path) {
    std::string e = std::filesystem::u8path(path).extension().string();
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    return lowered(e);
}
}  // namespace

PtoFileType pto_classify_path(const std::string& path) {
    PtoFileType t;
    const std::string ext = extension_of(path);

    // The name proposes and the bytes dispose. Only a file some photon format
    // claims by extension is offered to the sniffers at all -- several of them
    // recognise a container by little more than its record size dividing evenly,
    // and asked about forty arbitrary bytes one of them says yes.
    //
    // Among the formats that do claim it, the bytes decide: four of them claim
    // ".spc", and the encoding written down is the format's own name, which
    // container_for reads back. "spc-130" says which one this is; "spc" does not.
    std::error_code ec;
    if (!ext.empty() && !IORegistry::by_extension(ext).empty() &&
        std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) {
        const int container = inferTTTRFileType(path.c_str());
        const FileFormat* f =
                container >= 0 ? IORegistry::by_container_type(container) : nullptr;
        if (f != nullptr) {
            t.kind = "photons";
            t.encoding = lowered(f->name);
            if (t.encoding == "photon-hdf5") t.media_type = "application/x-hdf5";
            return t;
        }
    }
    return classify_by_extension(path);
}

PtoFileType PtoFile::classify(const std::string& path) const { return pto_classify_path(path); }

int pto_read_events(const std::string& spec, std::uint64_t first_event,
                    std::uint64_t n_events, ::TTTR* out) {
    const std::string path = subfile_path(spec);
    const std::string selector = subfile_selector(spec);

    PtoFile file;
    if (!file.open(path)) {
        std::cerr << "pto: " << file.error() << std::endl;
        return 0;
    }

    const std::vector<PtoObject> all = file.objects();
    const PtoObject* chosen = nullptr;
    for (std::size_t i = 0; i < all.size(); i++) {
        if (!holds_photons(all[i])) continue;
        if (!selector.empty() && all[i].name != selector &&
            std::to_string(all[i].uid) != selector) continue;
        chosen = &all[i];
        break;
    }
    if (chosen == nullptr) {
        std::cerr << "pto: " << path << " holds no photon data"
                  << (selector.empty() ? "" : " called '" + selector + "'") << std::endl;
        return 0;
    }

    const int container = container_for(chosen->encoding);
    const std::uint64_t end = n_events == 0 ? 0 : first_event + n_events;
    const std::vector<PtoCue> cues = file.cues(chosen->uid);

    // A dstore payload is not a record stream, and a container with no cues has
    // nothing to seek by. Both decode the whole object and slice it -- correct,
    // and exactly as slow as it was before cues existed.
    PtoCue from;
    bool have_from = false;
    std::uint64_t stop_at = 0;
    if (container >= 0) bracket(cues, first_event, end, &from, &have_from, &stop_at);
    if (lowered(chosen->encoding) == "dstore") {
        // The row IS the seek position; no whole-object read and no slice.
        if (!read_one_rows(file, *chosen, out, first_event, n_events)) return 0;
        out->find_used_routing_channels();
        return 1;
    }
    if (container < 0 || !have_from || from.event == 0) {
        if (!read_one(file, path, *chosen, out)) return 0;
        keep_range(out, first_event, n_events);
        out->find_used_routing_channels();
        return 1;
    }

    const std::uint64_t records_at = chosen->offset + from.offset;
    const std::uint64_t records_end = stop_at == 0 ? 0 : chosen->offset + stop_at;
    if (!out->read_embedded_range(path.c_str(), container, chosen->offset, chosen->size,
                                  records_at, records_end,
                                  sidecar_text(file, chosen->uid)))
        return 0;

    /*
     * The overflow count at the cue is not in the records, so a decode that
     * starts there reports macro times short by however many overflows came
     * before. The cue's own macro time is what closes that gap: the first event
     * decoded IS the cue's event, so the difference between what it should be
     * and what it came out as applies to every event after it.
     */
    if (out->size() > 0 && from.time > out->get_macro_time_at(0)) {
        const unsigned long long delta = from.time - out->get_macro_time_at(0);
        const std::size_t n = out->size();
        for (std::size_t i = 0; i < n; i++)
            out->set_macro_time_at(i, out->get_macro_time_at(i) + delta);
    }
    keep_range(out, first_event - from.event, n_events);
    out->find_used_routing_channels();
    return 1;
}
// --- streaming photons into a container -------------------------------------

struct PtoPhotonStream::Impl {
    PtoFile file;
    std::string name;
    std::uint64_t chunk = 0;

    // Clocks, copied at open rather than held by pointer: a stream outlives
    // the call that opened it and the caller's header may not.
    double macro_res = 0.0, micro_res = 0.0;
    long long n_micro_channels = 0;
    long long src_container = -1, src_record = -1;
    std::string source_header_json;
};

PtoPhotonStream::PtoPhotonStream() : p_(new Impl) {}

PtoPhotonStream::~PtoPhotonStream() {
    // Closed HERE, not in the base destructor: close() drains through
    // write_chunk() and close_target(), and by the time ~TTTRStreamWriter runs
    // this object's overrides no longer exist. A stream dropped without an
    // explicit close still keeps the photons it was holding.
    if (is_open()) {
        try { close(); } catch (...) {}
    }
}

std::uint64_t PtoPhotonStream::n_chunks() const { return p_->chunk; }

bool PtoPhotonStream::open_target(const std::string& filename, TTTRHeader* header,
                                  const std::string& name) {
    Impl& m = *p_;
    if (std::filesystem::exists(std::filesystem::u8path(filename)))
        return fail(filename + " already exists; a photon stream writes a new "
                    "container, because chunks written between somebody "
                    "else's objects would be absorbed into their measurement");

    if (!m.file.create(filename, name)) return fail(m.file.error());
    m.file.set_writing_app("tttrlib");
    m.name = name.empty() ? std::string("photons") : name;
    m.chunk = 0;

    m.macro_res = header->get_macro_time_resolution();
    m.micro_res = header->get_micro_time_resolution();
    m.n_micro_channels = static_cast<long long>(header->get_number_of_micro_time_channels());
    m.src_container = header->get_tttr_container_type();
    m.src_record = header->get_tttr_record_type();
    m.source_header_json = header->get_json();

    if (!m.file.commit()) return fail(m.file.error());
    return true;
}

bool PtoPhotonStream::write_chunk(const std::uint64_t* macro_times,
                                  const std::uint16_t* micro_times,
                                  const std::int8_t* routing_channels,
                                  const std::int8_t* event_types,
                                  std::size_t n, bool durable) {
    Impl& m = *p_;
    // A chunk object IS the durability: it is committed or it does not exist,
    // so an empty checkpoint has nothing left to do.
    if (n == 0) return true;
    (void) durable;

    data::DataStore store("photons");
    store.set_n_rows(n);
    const int cm = store.add_column("macro_time", data::ColumnType::UInt64);
    const int cu = store.add_column("micro_time", data::ColumnType::UInt16);
    const int cc = store.add_column("routing_channel", data::ColumnType::Int8);
    const int ct = store.add_column("event_type", data::ColumnType::Int8);
    store.column(cm).resize_uninitialized(n);
    store.column(cu).resize_uninitialized(n);
    store.column(cc).resize_uninitialized(n);
    store.column(ct).resize_uninitialized(n);
    std::memcpy(store.column(cm).data_ptr(), macro_times, n * sizeof(std::uint64_t));
    std::memcpy(store.column(cu).data_ptr(), micro_times, n * sizeof(std::uint16_t));
    std::memcpy(store.column(cc).data_ptr(), routing_channels, n);
    std::memcpy(store.column(ct).data_ptr(), event_types, n);

    // Zero-padded, so lexical order -- which is the order the reader stacks
    // them in -- is numeric order. Six digits is a million chunks.
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "/%06llu",
                  static_cast<unsigned long long>(m.chunk));
    const std::string chunk_name = m.name + suffix;

    const std::uint64_t uid = pto_add_store(m.file, "photons", chunk_name, store);
    if (uid == 0) return fail(m.file.error());

    // Every chunk carries the clocks: each is a complete photons object, and a
    // reader may be handed any one of them -- including a reader recovering a
    // container whose writer was killed.
    PtoTag t;
    t.target = uid;
    t.type = PtoType::Float;
    t.name = "_mmfdb_setup.macro_time_resolution"; t.d = m.macro_res; m.file.add_tag(t);
    t.name = "_mmfdb_setup.micro_time_resolution"; t.d = m.micro_res; m.file.add_tag(t);
    PtoTag b;
    b.target = uid;
    b.type = PtoType::Int;
    b.name = "_pto_photons.number_of_micro_time_channels"; b.i = m.n_micro_channels;
    m.file.add_tag(b);
    b.name = "_pto_photons.source_container_type"; b.i = m.src_container; m.file.add_tag(b);
    b.name = "_pto_photons.source_record_type"; b.i = m.src_record; m.file.add_tag(b);
    // The instrument header on the first chunk only: it is identical on every
    // one, and repeating a hundred rows per chunk would make the tag block the
    // largest thing in a long acquisition.
    if (m.chunk == 0 && !m.source_header_json.empty()) {
        TTTRHeader tmp;
        tmp.set_json(m.source_header_json);
        write_source_header_tags(m.file, uid, &tmp);
    }

    if (!m.file.commit()) return fail(m.file.error());
    m.chunk++;
    return true;
}

bool PtoPhotonStream::close_target() {
    p_->file.close();
    return true;
}

namespace {

/*!
 * \brief Write a TTTR into a container as a native photons object.
 *        \see FileFormat::write_from.
 *
 * The counterpart of \ref read_one's dstore branch, and what makes a `.pto` a
 * *sink* rather than a wrapper: the stream goes in as its own four columns,
 * with no vendor file inside. `macro_time` is absolute, which is the native
 * table's advantage over every record stream -- no overflow events, so no
 * decode state to carry, and event `i` is a row index rather than a position
 * that has to be reached by decoding.
 *
 * The three required header tags go on the object, not the file: the header
 * belongs to the measurement, and a container may hold several. Writing them
 * is not optional -- an object without them reads back as dimensionless
 * integers, which is the defect \ref apply_photon_header was added to fix, and
 * a writer that produced one would be manufacturing it.
 *
 * Writing into an **existing** container appends: one measurement
 * per object, `tttr pto add` semantics. So a second write to the same path
 * does not destroy the first, which is what a caller writing two channels or
 * two runs into one container needs.
 */
int write_tttr_into_pto(void*, const char* path_c, void* tttr, void* header_v) {
    TTTR* in = static_cast<TTTR*>(tttr);
    if (in == nullptr) return 0;
    TTTRHeader* hdr = static_cast<TTTRHeader*>(header_v);
    if (hdr == nullptr) hdr = in->get_header();

    const std::string spec = path_c == nullptr ? "" : path_c;
    const std::string path = subfile_path(spec);
    // The selector names the object, so `run.pto|green` writes a photons
    // object called "green" -- the same spelling that reads it back.
    const std::string sel = subfile_selector(spec);
    const std::string name = sel.empty() ? "photons" : sel;

    const std::size_t n = static_cast<std::size_t>(in->get_n_valid_events());

    data::DataStore store("photons");
    store.set_n_rows(n);
    // The four normative columns, in the normative dtypes (doc/formats/pto.rst,
    // "Photon streams, natively"). Pinned to TTTR's in-memory types so the
    // round trip is a copy and not a conversion.
    const int c_macro = store.add_column("macro_time", data::ColumnType::UInt64);
    const int c_micro = store.add_column("micro_time", data::ColumnType::UInt16);
    const int c_chan = store.add_column("routing_channel", data::ColumnType::Int8);
    const int c_type = store.add_column("event_type", data::ColumnType::Int8);
    store.column(c_macro).resize_uninitialized(n);
    store.column(c_micro).resize_uninitialized(n);
    store.column(c_chan).resize_uninitialized(n);
    store.column(c_type).resize_uninitialized(n);
    {
        std::uint64_t* macro = static_cast<std::uint64_t*>(store.column(c_macro).data_ptr());
        std::uint16_t* micro = static_cast<std::uint16_t*>(store.column(c_micro).data_ptr());
        std::int8_t* chan = static_cast<std::int8_t*>(store.column(c_chan).data_ptr());
        std::int8_t* type = static_cast<std::int8_t*>(store.column(c_type).data_ptr());
        for (std::size_t i = 0; i < n; i++) {
            macro[i] = static_cast<std::uint64_t>(in->get_macro_time_at(i));
            micro[i] = static_cast<std::uint16_t>(in->get_micro_time_at(i));
            chan[i] = static_cast<std::int8_t>(in->get_routing_channel_at(i));
            type[i] = static_cast<std::int8_t>(in->get_event_type_at(i));
        }
    }

    PtoFile file;
    const bool existed = std::filesystem::exists(std::filesystem::u8path(path));
    if (existed) {
        // Append rather than replace: a container holds measurements, and
        // silently discarding the ones already in it is not a write, it is a
        // deletion nobody asked for.
        if (!file.open(path, true)) {
            std::cerr << "pto: " << file.error() << std::endl;
            return 0;
        }
    } else if (!file.create(path, "photons")) {
        std::cerr << "pto: " << file.error() << std::endl;
        return 0;
    }
    if (!existed) file.set_writing_app("tttrlib");

    const std::uint64_t uid = pto_add_store(file, "photons", name, store);
    if (uid == 0) {
        std::cerr << "pto: " << file.error() << std::endl;
        return 0;
    }

    // The header. Spelled exactly as apply_photon_header reads it -- the two
    // are one contract, and a test that pins only the round trip would not
    // notice them drifting together into a spelling nothing else accepts.
    if (hdr != nullptr) {
        PtoTag t;
        t.target = uid;
        t.type = PtoType::Float;
        t.name = "_mmfdb_setup.macro_time_resolution";
        t.d = hdr->get_macro_time_resolution();
        file.add_tag(t);
        t.name = "_mmfdb_setup.micro_time_resolution";
        t.d = hdr->get_micro_time_resolution();
        file.add_tag(t);

        PtoTag b;
        b.target = uid;
        b.type = PtoType::Int;
        b.name = "_pto_photons.number_of_micro_time_channels";
        b.i = static_cast<long long>(hdr->get_number_of_micro_time_channels());
        file.add_tag(b);

        // Provenance: which container and record type the events were decoded
        // from. This is the fourth required tag, and it is not
        // decoration -- a marker convention is a property of the source
        // format, not of the events. PTU stores marker *indices* that decode
        // to routing channels as 2^idx; HT3 stores the channel directly. A
        // native table records neither in its columns, so without this an
        // imaging measurement reconstructs to zero frames: the geometry is
        // right, the markers are all present, and nothing knows how to read
        // them. These configure nothing at read time except that convention.
        b.name = "_pto_photons.source_container_type";
        b.i = static_cast<long long>(hdr->get_tttr_container_type());
        file.add_tag(b);
        b.name = "_pto_photons.source_record_type";
        b.i = static_cast<long long>(hdr->get_tttr_record_type());
        file.add_tag(b);

        // Open fidelity: everything else the instrument header said, verbatim.
        write_source_header_tags(file, uid, hdr);
    }

    if (!file.commit()) {
        std::cerr << "pto: " << file.error() << std::endl;
        return 0;
    }
    file.close();
    return 1;
}

/// \see FileFormat::make_stream_writer. The caller takes ownership.
void* make_pto_stream_writer(void*) {
    return static_cast<TTTRStreamWriter*>(new PtoPhotonStream());
}

/*!
 * \brief Read the photon data of a container into a TTTR. \see FileFormat::read_into.
 *
 * With a selector, that one object. Without, the only one -- or, when there are
 * several, all of them stacked in lexical order of their names, so m001.spc,
 * m002.spc, m003.spc come back as one measurement in the order they were
 * recorded, with each one's macro times continuing after the last.
 *
 * Nothing is unpacked: a container that begins partway into the file is read
 * where it lies.
 */
int read_pto_into_tttr(void*, const char* spec_c, void* tttr) {
    TTTR* out = static_cast<TTTR*>(tttr);
    const std::string spec = spec_c == nullptr ? "" : spec_c;
    const std::string path = subfile_path(spec);
    const std::string selector = subfile_selector(spec);

    // A range asked for through the reader-parameter mechanism, which is how a
    // binding reaches pto_read_events without a TTTR constructor that would be
    // ambiguous with the four it already has.
    {
        const std::string params = out->get_container_parameters();
        if (params.find_first_not_of(" \t\r\n") != std::string::npos) {
            const nlohmann::json j = nlohmann::json::parse(params, nullptr, false);
            if (j.is_object() && (j.contains("first_event") || j.contains("n_events"))) {
                const std::uint64_t first = j.value("first_event", std::uint64_t(0));
                const std::uint64_t n = j.value("n_events", std::uint64_t(0));
                return pto_read_events(spec, first, n, out);
            }
        }
    }

    PtoFile file;
    if (!file.open(path)) {
        std::cerr << "pto: " << file.error() << std::endl;
        return 0;
    }

    std::vector<PtoObject> chosen;
    const std::vector<PtoObject> all = file.objects();
    for (std::size_t i = 0; i < all.size(); i++) {
        if (!holds_photons(all[i])) continue;
        if (!selector.empty() && all[i].name != selector &&
            std::to_string(all[i].uid) != selector) continue;
        chosen.push_back(all[i]);
        if (!selector.empty()) break;
    }
    if (chosen.empty()) {
        std::cerr << "pto: " << path << " holds no photon data"
                  << (selector.empty() ? "" : " called '" + selector + "'")
                  << std::endl;
        return 0;
    }
    // Lexical order, so a numbered series comes back in the order it was taken.
    if (chosen.size() > 1) {
        std::sort(chosen.begin(), chosen.end(),
                  [](const PtoObject& a, const PtoObject& b) { return a.name < b.name; });
    }

    if (!read_one(file, path, chosen[0], out)) return 0;
    for (std::size_t i = 1; i < chosen.size(); i++) {
        TTTR next;
        if (!read_one(file, path, chosen[i], &next)) return 0;
        // An embedded vendor file restarts its macro clock at zero, so the
        // next one has to continue after this one. A NATIVE table does not:
        // `macro_time` is absolute by specification, which is the whole
        // advantage of storing decoded events, and shifting it would move
        // every photon after the first object. That is not a preference --
        // an acquisition writes itself as a sequence of native chunks, and
        // shifting them scatters one continuous measurement across a
        // timeline it never occupied.
        const bool native = lowered(chosen[i].encoding) == "dstore";
        out->append(&next, !native, 0);
    }
    out->find_used_routing_channels();
    return 1;
}

/*!
 * \brief Put PTO in the format table, and make it read itself.
 *
 * A file-scope object rather than a call from core: core must not know this
 * module exists, or the dependency it inverts comes straight back.
 */
struct RegisterPto {
    RegisterPto() {
        FileFormat f;
        f.name = "PTO";
        f.container_type = 20;
        f.label = "PhoTon cOntainer";
        f.summary = "EBML container holding photon data and what was computed from it";
        f.extensions.push_back("pto");
        f.canonical_extension = "pto";
        f.sniff = [](const std::string& fn) { return is_pto_file(fn); };
        // Readable in pieces, but by EVENT rather than by record: a PTO holds
        // whole containers and stores, not a record stream of its own, so the
        // record range the fixed-width containers take does not apply here.
        // See PtoFile::build_cues; without cues these still work and cost a
        // full decode.
        f.ranged_reads = true;
        f.parameters_schema = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "first_event": {
      "type": "integer", "title": "First event", "default": 0, "minimum": 0,
      "description": "Skip this many events of the photon object before reading. With cues built over the object the decode starts at the nearest cue at or before it; without them the payload is decoded whole and sliced."
    },
    "n_events": {
      "type": "integer", "title": "Events to read", "default": 0, "minimum": 0,
      "description": "How many events to read, or 0 for all of them from first_event on."
    }
  }
})";
        f.can_read = true;
        IORegistry::add(f);
        IORegistry::set_reader("PTO", &read_pto_into_tttr, nullptr);
        // Sets can_write with it, so the flag cannot outlive the writer.
        IORegistry::set_writer("PTO", &write_tttr_into_pto, nullptr);
        // Separate capability: writable and streamable are different questions,
        // and PTO is currently the only format that answers yes to the second.
        IORegistry::set_stream_writer("PTO", &make_pto_stream_writer, nullptr);
    }
};
const RegisterPto register_pto;

}  // namespace

}  // namespace io
}  // namespace tttrlib
