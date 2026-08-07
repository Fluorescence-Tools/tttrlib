// SPDX-License-Identifier: BSD-3-Clause
#include "TTTRStream.h"

#include <cstdio>
#include <stdexcept>

#include "FileIO.h"
#include "TTTR.h"
#include "TTTRFormat.h"
#include "TTTRHeaderTypes.h"

namespace tttrlib {

namespace {

/// The registry row for \p container_type, or nullptr.
const FileFormat* format_of(int container_type) {
    return IORegistry::by_container_type(container_type);
}

/*!
 * \brief Resolve the container of \p spec, and refuse the ones that are not
 *        record streams.
 *
 * A named decline rather than a guess. "PHOTON-HDF5 cannot be read in pieces"
 * is something a caller can branch on; a silent full read is what a caller
 * discovers from a memory graph three months later.
 */
std::string range_decline(const std::string& path, int& container_type) {
    if (container_type < 0)
        container_type = IORegistry::infer_container_type(path);
    if (container_type < 0)
        return "the container of '" + path + "' could not be identified";
    const FileFormat* f = format_of(container_type);
    if (f == nullptr)
        return "container type " + std::to_string(container_type) + " is not registered";
    if (!f->ranged_reads)
        return f->name + " cannot be read in pieces: it is not a header followed "
                         "by fixed-width records";
    // PTO is readable in pieces and is not a record stream; its range is in
    // events and goes through pto_read_events.
    if (f->read_into != nullptr)
        return f->name + " is read in pieces by event, not by record; "
                         "use its first_event / n_events reader parameters";
    return {};
}

}  // namespace

bool container_supports_ranged_reads(int container_type) {
    const FileFormat* f = format_of(container_type);
    return f != nullptr && f->ranged_reads;
}

ContainerRecords container_records(const std::string& spec, int container_type) {
    ContainerRecords out;
    const std::string path = subfile_path(spec);
    out.container_type = container_type;
    out.reason = range_decline(path, out.container_type);
    if (!out.reason.empty()) return out;

    // open_embedded parses the header and decodes nothing -- this question,
    // already answered elsewhere. Wrapped because a header parser handed the
    // wrong kind of file throws, and a query answers "no, because", not that.
    try {
        TTTR probe;
        if (!probe.open_embedded(path.c_str(), out.container_type, 0, 0)) {
            out.reason = "could not open '" + path + "'";
            return out;
        }
        out.record_type = probe.get_tttr_record_type();
        out.records_begin = probe.get_records_begin();
        out.bytes_per_record = probe.get_header()->get_bytes_per_record();
        out.n_records = probe.n_records_in_file;
    } catch (const std::exception& e) {
        out.reason = std::string("could not read the header of '") + path + "': " + e.what();
        return out;
    } catch (...) {
        out.reason = "could not read the header of '" + path + "'";
        return out;
    }
    out.ranged = true;
    return out;
}

std::uint64_t container_n_records(const std::string& spec, int container_type) {
    return container_records(spec, container_type).n_records;
}

std::vector<unsigned char> container_read_records(
        const std::string& spec,
        std::uint64_t first, std::uint64_t n,
        int container_type) {
    const ContainerRecords info = container_records(spec, container_type);
    if (!info.ranged) throw std::invalid_argument("container_read_records: " + info.reason);

    std::vector<unsigned char> out;
    if (first >= info.n_records) return out;
    const std::uint64_t left = info.n_records - first;
    const std::uint64_t take = (n == 0 || n > left) ? left : n;
    if (take == 0 || info.bytes_per_record == 0) return out;

    const std::string path = subfile_path(spec);
    std::FILE* fp = open_file(path, "rb");
    if (fp == nullptr) throw std::invalid_argument("container_read_records: could not open '" + path + "'");

    out.resize(static_cast<std::size_t>(take * info.bytes_per_record));
    fseek64(fp, static_cast<std::int64_t>(
                    info.records_begin + first * info.bytes_per_record), SEEK_SET);
    const std::size_t got = std::fread(out.data(), 1, out.size(), fp);
    std::fclose(fp);
    // A file being written to underneath us is the case this is for; a short
    // read is data, not an error. Trim to whole records so a caller can decode
    // what came back without checking.
    out.resize((got / info.bytes_per_record) * info.bytes_per_record);
    return out;
}

int container_read_events(
        const std::string& spec,
        std::uint64_t first, std::uint64_t n,
        TTTR* out,
        int container_type) {
    if (out == nullptr) return 0;
    const ContainerRecords info = container_records(spec, container_type);
    if (!info.ranged) throw std::invalid_argument("container_read_events: " + info.reason);
    if (info.bytes_per_record == 0) return 0;

    const std::string path = subfile_path(spec);
    const std::uint64_t begin = info.records_begin + first * info.bytes_per_record;
    const std::uint64_t end = n == 0 ? 0 : begin + n * info.bytes_per_record;
    return out->read_embedded_range(path.c_str(), info.container_type,
                                    0, 0, begin, end);
}

}  // namespace tttrlib
