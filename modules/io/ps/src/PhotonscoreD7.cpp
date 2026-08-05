// SPDX-License-Identifier: BSD-3-Clause
#include "PhotonscoreD7.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "FileIO.h"    // open_file (Unicode-safe), fseek64/ftell64

namespace photonscore {

const char* const D7_MAGIC = "D7 Photons Data";

namespace {

// ---------------------------------------------------------------------------
// Low-level byte / protobuf-wire helpers (port of photonsfile/_d7.py)
// ---------------------------------------------------------------------------

using Bytes = std::string; // used as a byte buffer; access via unsigned char

inline unsigned char at(const Bytes& b, size_t i) {
    return static_cast<unsigned char>(b[i]);
}

// Read a base-128 varint starting at index i. Returns the value and advances i.
uint64_t read_varint(const Bytes& buf, size_t& i) {
    uint64_t val = 0;
    int shift = 0;
    while (i < buf.size()) {
        unsigned char c = at(buf, i++);
        val |= static_cast<uint64_t>(c & 0x7f) << shift;
        if (!(c & 0x80)) break;
        shift += 7;
    }
    return val;
}

// ZigZag decode: (v >> 1) ^ -(v & 1)
inline int64_t zigzag(uint64_t v) {
    return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1);
}

// Remove the 2-byte block header present at the start of every page. 'raw' is a
// slice of the physical file beginning at physical offset 'phys_start'.
Bytes strip_markers(const Bytes& raw, int64_t phys_start, uint32_t page) {
    const size_t n = raw.size();
    // first marker position within 'raw': (-phys_start) mod page, non-negative
    int64_t rem = phys_start % static_cast<int64_t>(page);
    size_t first = static_cast<size_t>((page - rem) % static_cast<int64_t>(page));
    if (first >= n) return raw;
    // Positions to drop: first, first+1, first+page, first+page+1, ...
    Bytes out;
    out.reserve(n);
    size_t next_marker = first;
    for (size_t idx = 0; idx < n; ++idx) {
        if (idx == next_marker || idx == next_marker + 1) {
            if (idx == next_marker + 1) next_marker += page;
            continue;
        }
        out.push_back(raw[idx]);
    }
    return out;
}

// Map a physical offset into the stripped-stream offset of a region that
// starts at physical offset 'region_start'.
size_t clean_pos(int64_t phys, int64_t region_start, uint32_t page) {
    int64_t markers = (phys / page) - (region_start / page);
    return static_cast<size_t>((phys - region_start) - 2 * markers);
}

// Parse a Header.DatasetInfo message: field 1 = name (string), field 2 = type.
D7Dataset parse_descriptor(const Bytes& payload) {
    D7Dataset ds;
    size_t k = 0;
    while (k < payload.size()) {
        unsigned char tag = at(payload, k++);
        int field = tag >> 3;
        int wt = tag & 7;
        if (wt == 2) {
            uint64_t ln = read_varint(payload, k);
            if (field == 1) ds.name = payload.substr(k, ln);
            k += ln;
        } else if (wt == 0) {
            uint64_t v = read_varint(payload, k);
            if (field == 2) ds.type_code = static_cast<int>(v);
        } else {
            break;
        }
    }
    return ds;
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

struct FileCloser {
    FILE* fp;
    ~FileCloser() { if (fp) std::fclose(fp); }
};

int64_t file_size(FILE* fp) {
    fseek64(fp, 0, SEEK_END);
    int64_t sz = ftell64(fp);
    return sz;
}

// Read 'length' bytes from physical offset 'offset'.
Bytes read_at(FILE* fp, int64_t offset, int64_t length) {
    if (length <= 0) return Bytes();
    Bytes buf;
    buf.resize(static_cast<size_t>(length));
    fseek64(fp, offset, SEEK_SET);
    size_t got = std::fread(&buf[0], 1, static_cast<size_t>(length), fp);
    buf.resize(got);
    return buf;
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

D7Header parse_header(const Bytes& head) {
    D7Header header;
    header.magic = D7_MAGIC;
    header.page_size = D7_DEFAULT_PAGE;

    size_t i = 2; // skip the 2-byte block header of page 0
    read_varint(head, i);              // FileEntry tag (header oneof)
    uint64_t hlen = read_varint(head, i);
    size_t end = i + hlen;
    if (end > head.size()) end = head.size();

    while (i < end) {
        unsigned char tag = at(head, i++);
        int field = tag >> 3;
        int wt = tag & 7;
        if (wt == 2) {
            uint64_t ln = read_varint(head, i);
            if (field == 7) { // DatasetInfo (table of contents)
                header.datasets.push_back(parse_descriptor(head.substr(i, ln)));
            }
            i += ln;
        } else if (wt == 0) {
            uint64_t v = read_varint(head, i);
            if (field == 2) header.version_major = static_cast<int>(v);
            else if (field == 6) header.index_step = static_cast<uint32_t>(v);
            else if (field == 8 && v > 0) header.page_size = static_cast<uint32_t>(v);
        } else {
            break;
        }
    }
    header.header_end = end;
    return header;
}

// Locate the global index offset from the epilogue at the end of the file.
int64_t read_epilogue(FILE* fp, int64_t filesize, uint32_t page) {
    int64_t n = std::min<int64_t>(filesize, 8192);
    Bytes tail = strip_markers(read_at(fp, filesize - n, n), filesize - n, page);

    static const std::string sig = "End of D7 Photons Data File";
    size_t e = tail.rfind(sig);
    if (e == std::string::npos) {
        throw std::runtime_error("D7 epilogue signature not found");
    }
    // Walk back to the first byte of the global_index_offset varint.
    // Layout before the signature: 0x08 <varint> 0x12 <len> <signature>.
    // e-1 = len, e-2 = 0x12, e-3 = last byte of the varint.
    if (e < 3) throw std::runtime_error("D7 epilogue truncated");
    size_t j = e - 3;
    while (j >= 1 && (at(tail, j - 1) & 0x80)) j -= 1;
    return static_cast<int64_t>(read_varint(tail, j));
}

// Parse the global Index: physical offsets of every Data block, grouped by
// dataset id and sorted ascending.
std::map<int, std::vector<int64_t>> parse_index(
        FILE* fp, int64_t index_off, int64_t filesize, uint32_t page, size_t n_datasets) {
    Bytes idx = strip_markers(read_at(fp, index_off, filesize - index_off), index_off, page);
    size_t p = 0;
    read_varint(idx, p);            // Index FileEntry tag
    uint64_t ln = read_varint(idx, p);
    Bytes body = idx.substr(p, ln);

    std::map<int, std::vector<int64_t>> offsets;
    const size_t n = body.size();
    size_t i = 0;
    while (i + 2 < n) {
        // DataInfo entries: tag 0x0a, len, then 0x08 <dataset_id> 0x10 <offset>
        if (at(body, i) == 0x0a) {
            unsigned char l = at(body, i + 1);
            if (i + 2 + l <= n) {
                Bytes pay = body.substr(i + 2, l);
                if (pay.size() >= 3 && at(pay, 0) == 0x08 &&
                    static_cast<size_t>(at(pay, 1)) < n_datasets && at(pay, 2) == 0x10) {
                    int did = at(pay, 1);
                    size_t k = 3;
                    int64_t off = static_cast<int64_t>(read_varint(pay, k));
                    offsets[did].push_back(off);
                    i += 2 + l;
                    continue;
                }
            }
        }
        i += 1;
    }
    for (auto& kv : offsets) std::sort(kv.second.begin(), kv.second.end());
    return offsets;
}

// Sequentially decode a packed run of zigzag varints into signed deltas.
std::vector<int64_t> decode_varints_zigzag(const Bytes& b) {
    std::vector<int64_t> out;
    size_t i = 0;
    const size_t n = b.size();
    while (i < n) {
        uint64_t val = 0;
        int shift = 0;
        while (i < n) {
            unsigned char c = at(b, i++);
            val |= static_cast<uint64_t>(c & 0x7f) << shift;
            if (!(c & 0x80)) break;
            shift += 7;
        }
        out.push_back(zigzag(val));
    }
    return out;
}

// Find and decode a single Data block whose message begins near 'cp' in the
// stripped region buffer 'buf'. Returns the reconstructed values (seed + cumsum
// of deltas) or an empty vector if no valid block was found.
std::vector<int64_t> decode_data_block(const Bytes& buf, size_t cp, size_t n, size_t n_datasets) {
    // Search for the 0x12 (Data oneof) tag in [cp, cp+16) that is followed by a
    // plausible Data message start: 0x08 <dataset_id < n_datasets> <0x18|0x22|0x2a>.
    size_t limit = std::min(cp + 16, n);
    size_t q = (cp == 0) ? 0 : cp - 1;
    size_t k = 0;
    bool found = false;
    for (size_t search = (cp == 0) ? 0 : cp; search < limit; ++search) {
        if (at(buf, search) != 0x12) continue;
        q = search;
        size_t kk = q + 1;
        read_varint(buf, kk); // message length varint; kk now at message start
        if (kk + 2 < n && at(buf, kk) == 0x08 &&
            static_cast<size_t>(at(buf, kk + 1)) < n_datasets &&
            (at(buf, kk + 2) == 0x18 || at(buf, kk + 2) == 0x22 || at(buf, kk + 2) == 0x2a)) {
            k = kk;
            found = true;
            break;
        }
    }
    if (!found) return {};

    // Re-read message length to bound the message.
    size_t klen = q + 1;
    uint64_t msg_len = read_varint(buf, klen);
    Bytes msg = buf.substr(k, msg_len);

    int64_t seed = 0;
    Bytes src;
    bool have_src = false;
    size_t m = 0;
    while (m < msg.size()) {
        unsigned char tag = at(msg, m++);
        int f = tag >> 3;
        int wt = tag & 7;
        if (wt == 0) {
            uint64_t v = read_varint(msg, m);
            if (f == 3) seed = zigzag(v); // Data.seed (sint64)
        } else if (wt == 2) {
            uint64_t l = read_varint(msg, m);
            if (f == 4 || f == 5) { // integers (sint32) / longs (sint64), packed
                src = msg.substr(m, l);
                have_src = true;
            }
            m += l;
        } else {
            break;
        }
    }
    if (!have_src) return {};

    std::vector<int64_t> deltas = decode_varints_zigzag(src);
    std::vector<int64_t> vals(deltas.size() + 1);
    vals[0] = seed;
    int64_t acc = seed;
    for (size_t d = 0; d < deltas.size(); ++d) {
        acc += deltas[d];
        vals[d + 1] = acc;
    }
    return vals;
}

// Decode all Data blocks of one dataset given their physical offsets.
std::vector<int64_t> decode_region(
        FILE* fp, const std::vector<int64_t>& offsets,
        int64_t region_start, int64_t region_end, uint32_t page, size_t n_datasets) {
    Bytes buf = strip_markers(
            read_at(fp, region_start, region_end - region_start), region_start, page);
    const size_t n = buf.size();
    std::vector<int64_t> result;
    for (int64_t phys : offsets) {
        std::vector<int64_t> vals =
                decode_data_block(buf, clean_pos(phys, region_start, page), n, n_datasets);
        result.insert(result.end(), vals.begin(), vals.end());
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool is_photons_file(const std::string& path) {
    FILE* fp = open_file(path, "rb");
    if (!fp) return false;
    FileCloser closer{fp};
    char buf[64];
    size_t got = std::fread(buf, 1, sizeof(buf), fp);
    std::string head(buf, got);
    return head.find(D7_MAGIC) != std::string::npos;
}

D7Header read_header(const std::string& path) {
    FILE* fp = open_file(path, "rb");
    if (!fp) throw std::runtime_error("Cannot open .photons file: " + path);
    FileCloser closer{fp};
    Bytes head = read_at(fp, 0, D7_DEFAULT_PAGE);
    if (head.substr(0, 64).find(D7_MAGIC) == std::string::npos) {
        throw std::runtime_error("Not a D7 .photons file (magic not found): " + path);
    }
    return parse_header(head);
}

std::map<std::string, std::string> read_attributes(const std::string& path) {
    D7Header header = read_header(path);
    uint32_t page = header.page_size;
    FILE* fp = open_file(path, "rb");
    if (!fp) throw std::runtime_error("Cannot open .photons file: " + path);
    FileCloser closer{fp};
    int64_t filesize = file_size(fp);

    int64_t index_off = read_epilogue(fp, filesize, page);
    Bytes idx = strip_markers(read_at(fp, index_off, filesize - index_off), index_off, page);
    size_t p = 0;
    read_varint(idx, p);
    uint64_t ln = read_varint(idx, p);
    Bytes body = idx.substr(p, ln);

    std::map<std::string, std::string> attrs;
    const size_t n = body.size();
    size_t i = 0;
    while (i < n) {
        unsigned char tag = at(body, i++);
        int f = tag >> 3;
        int wt = tag & 7;
        if (wt == 2) {
            uint64_t l = read_varint(body, i);
            Bytes ent = body.substr(i, l);
            i += l;
            if (f == 2) { // Index.attributes map entry
                std::string key, val;
                bool have_key = false;
                size_t k = 0;
                while (k < ent.size()) {
                    unsigned char etag = at(ent, k++);
                    if ((etag & 7) != 2) break;
                    uint64_t el = read_varint(ent, k);
                    std::string s = ent.substr(k, el);
                    k += el;
                    if ((etag >> 3) == 1) { key = s; have_key = true; }
                    else if ((etag >> 3) == 2) { val = s; }
                }
                if (have_key) attrs[key] = val;
            }
        } else if (wt == 0) {
            read_varint(body, i);
        } else {
            break;
        }
    }
    return attrs;
}

bool has_dual_tdc(const std::string& path) {
    D7Header header = read_header(path);
    bool start = false, stop = false;
    for (const auto& d : header.datasets) {
        if (d.name == "/start/time") start = true;
        if (d.name == "/stop/time") stop = true;
    }
    return start && stop;
}

std::map<std::string, std::vector<int64_t>> read_photons(
        const std::string& path, const std::vector<std::string>& wanted) {
    D7Header header = read_header(path);
    uint32_t page = header.page_size;
    size_t n_datasets = header.datasets.size();

    std::map<std::string, int> id_by_name;
    for (size_t i = 0; i < header.datasets.size(); ++i)
        id_by_name[header.datasets[i].name] = static_cast<int>(i);

    FILE* fp = open_file(path, "rb");
    if (!fp) throw std::runtime_error("Cannot open .photons file: " + path);
    FileCloser closer{fp};
    int64_t filesize = file_size(fp);

    int64_t index_off = read_epilogue(fp, filesize, page);
    std::map<int, std::vector<int64_t>> offsets =
            parse_index(fp, index_off, filesize, page, n_datasets);

    auto decode = [&](const std::string& full) -> std::vector<int64_t> {
        auto it = id_by_name.find(full);
        if (it == id_by_name.end()) return {};
        int did = it->second;
        auto oit = offsets.find(did);
        if (oit == offsets.end() || oit->second.empty()) return {};
        const std::vector<int64_t>& v = oit->second;
        int64_t start = *std::min_element(v.begin(), v.end());
        int64_t maxoff = *std::max_element(v.begin(), v.end());
        int64_t end = index_off;
        return decode_region(fp, v, start, end, page, n_datasets);
    };

    bool dual = id_by_name.count("/start/time") && id_by_name.count("/stop/time");

    std::map<std::string, std::vector<int64_t>> out;
    for (const std::string& name : wanted) {
        if (name == "dt" && dual) {
            std::vector<int64_t> start_t = decode("/start/time");
            std::vector<int64_t> stop_t = decode("/stop/time");
            if (!start_t.empty() && !stop_t.empty()) {
                size_t m = std::min(start_t.size(), stop_t.size());
                std::vector<int64_t> dt(m);
                for (size_t i = 0; i < m; ++i) dt[i] = stop_t[i] - start_t[i];
                out["dt"] = std::move(dt);
                continue;
            }
        }
        std::vector<int64_t> vals = decode("/photons/" + name);
        if (!vals.empty()) out[name] = std::move(vals);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t D7_PAYLOAD = D7_DEFAULT_PAGE - 2; // logical bytes per page

// Append a base-128 varint.
void put_varint(Bytes& out, uint64_t v) {
    while (true) {
        unsigned char b = v & 0x7f;
        v >>= 7;
        if (v) out.push_back(static_cast<char>(b | 0x80));
        else { out.push_back(static_cast<char>(b)); break; }
    }
}

// ZigZag encode: (n << 1) ^ (n >> 63)
inline uint64_t zigzag_encode(int64_t n) {
    return (static_cast<uint64_t>(n) << 1) ^ static_cast<uint64_t>(n >> 63);
}

void put_tag(Bytes& out, int field, int wiretype) {
    put_varint(out, (static_cast<uint64_t>(field) << 3) | wiretype);
}

void put_len_delim(Bytes& out, int field, const Bytes& payload) {
    put_tag(out, field, 2);
    put_varint(out, payload.size());
    out += payload;
}

Bytes make_dataset_info(const std::string& name, int type_code) {
    Bytes b;
    put_len_delim(b, 1, Bytes(name.begin(), name.end())); // name
    put_tag(b, 2, 0); put_varint(b, static_cast<uint64_t>(type_code)); // TypeCode
    return b;
}

Bytes make_header_msg(const std::vector<D7WriteDataset>& datasets) {
    Bytes b;
    put_len_delim(b, 1, Bytes(D7_MAGIC, D7_MAGIC + std::strlen(D7_MAGIC))); // signatue
    put_tag(b, 2, 0); put_varint(b, 1);  // version_major
    put_tag(b, 3, 0); put_varint(b, 0);  // version_minor
    put_tag(b, 4, 0); put_varint(b, 1);  // version_patch
    put_tag(b, 6, 0); put_varint(b, 512);           // index_step
    put_tag(b, 8, 0); put_varint(b, D7_DEFAULT_PAGE); // bytes_per_page
    for (const auto& d : datasets)
        put_len_delim(b, 7, make_dataset_info(d.name, d.type_code)); // toc
    return b;
}

Bytes make_data_msg(int dataset_id, const std::vector<int64_t>& values) {
    int64_t seed = values.empty() ? 0 : values[0];
    Bytes packed;
    for (size_t i = 1; i < values.size(); ++i)
        put_varint(packed, zigzag_encode(values[i] - values[i - 1]));
    Bytes b;
    put_tag(b, 1, 0); put_varint(b, static_cast<uint64_t>(dataset_id)); // dataset_id
    put_tag(b, 3, 0); put_varint(b, zigzag_encode(seed));               // seed (sint64)
    put_len_delim(b, 4, packed);                                        // integers (packed)
    return b;
}

Bytes make_data_info(int dataset_id, int64_t phys_offset) {
    Bytes b;
    put_tag(b, 1, 0); put_varint(b, static_cast<uint64_t>(dataset_id));
    put_tag(b, 2, 0); put_varint(b, static_cast<uint64_t>(phys_offset));
    return b;
}

Bytes make_index_msg(
        const std::vector<std::pair<int, int64_t>>& data_info,
        const std::map<std::string, std::string>& attributes) {
    Bytes b;
    for (const auto& di : data_info)
        put_len_delim(b, 1, make_data_info(di.first, di.second)); // data_info
    for (const auto& kv : attributes) {
        Bytes entry;
        put_len_delim(entry, 1, Bytes(kv.first.begin(), kv.first.end()));
        put_len_delim(entry, 2, Bytes(kv.second.begin(), kv.second.end()));
        put_len_delim(b, 2, entry); // attributes map entry
    }
    return b;
}

Bytes make_epilogue_msg(int64_t index_phys_offset) {
    Bytes b;
    put_tag(b, 1, 0); put_varint(b, static_cast<uint64_t>(index_phys_offset));
    static const std::string sig = "End of D7 Photons Data File";
    put_len_delim(b, 2, Bytes(sig.begin(), sig.end()));
    return b;
}

// Map a logical stream offset to its physical file offset (2 marker bytes at
// the start of every page).
int64_t phys_from_logical(int64_t L) {
    return (L / D7_PAYLOAD) * D7_DEFAULT_PAGE + 2 + (L % D7_PAYLOAD);
}

} // namespace

void write_photons(
        const std::string& path,
        const std::vector<D7WriteDataset>& datasets,
        const std::map<std::string, std::string>& attributes) {
    // Build the logical stream: Header, Data blocks, Index, Epilogue.
    Bytes logical;
    put_len_delim(logical, 1, make_header_msg(datasets)); // Header FileEntry

    std::vector<std::pair<int, int64_t>> data_info;
    for (size_t did = 0; did < datasets.size(); ++did) {
        int64_t off = static_cast<int64_t>(logical.size());
        put_len_delim(logical, 2,
                      make_data_msg(static_cast<int>(did), datasets[did].values));
        data_info.emplace_back(static_cast<int>(did), phys_from_logical(off));
    }

    int64_t index_off = static_cast<int64_t>(logical.size());
    int64_t index_phys = phys_from_logical(index_off);
    put_len_delim(logical, 3, make_index_msg(data_info, attributes)); // Index
    put_len_delim(logical, 4, make_epilogue_msg(index_phys));         // Epilogue

    // Paginate: prepend a 2-byte block header (low 14 bits = payload length) to
    // each 16382-byte logical chunk.
    Bytes physical;
    physical.reserve(logical.size() + 2 * (logical.size() / D7_PAYLOAD + 1));
    size_t pos = 0, n = logical.size();
    while (pos < n) {
        size_t len = std::min<size_t>(D7_PAYLOAD, n - pos);
        uint16_t block_header = static_cast<uint16_t>(len & 0x3FFF);
        physical.push_back(static_cast<char>(block_header & 0xFF));
        physical.push_back(static_cast<char>((block_header >> 8) & 0xFF));
        physical.append(logical, pos, len);
        pos += len;
    }

    FILE* fp = open_file(path, "wb");
    if (!fp) throw std::runtime_error("Cannot write .photons file: " + path);
    FileCloser closer{fp};
    if (std::fwrite(physical.data(), 1, physical.size(), fp) != physical.size())
        throw std::runtime_error("Error writing .photons file: " + path);
}

} // namespace photonscore
