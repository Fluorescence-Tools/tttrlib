// SPDX-License-Identifier: BSD-3-Clause
#include "io_pto.h"

#include "io_store.h"
#include "TTTR.h"
#include "TTTRFormat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>

namespace tttrlib {
namespace io {

const char* const kPtoSidecarTag = "pto.sidecar_of";

namespace {

// --- element ids ------------------------------------------------------------
//
// Borrowed from Matroska wherever Matroska already means what PTO needs, with
// Matroska's own ids, so a generic EBML parser walks a .pto file and prints
// most of it with the right names. Only what is genuinely new gets a new id,
// and those live in 0x1E54xx, where Matroska assigns nothing.

const std::uint32_t kEBML            = 0x1A45DFA3;
const std::uint32_t kEBMLVersion     = 0x4286;
const std::uint32_t kEBMLReadVersion = 0x42F7;
const std::uint32_t kEBMLMaxIDLength = 0x42F2;
const std::uint32_t kEBMLMaxSizeLen  = 0x42F3;
const std::uint32_t kDocType         = 0x4282;
const std::uint32_t kDocTypeVersion  = 0x4287;
const std::uint32_t kDocTypeReadVer  = 0x4285;

const std::uint32_t kSegment      = 0x18538067;
const std::uint32_t kSeekHead     = 0x114D9B74;
const std::uint32_t kSeek         = 0x4DBB;
const std::uint32_t kSeekID       = 0x53AB;
const std::uint32_t kSeekPosition = 0x53AC;
const std::uint32_t kInfo         = 0x1549A966;
const std::uint32_t kSegmentUUID  = 0x73A4;
const std::uint32_t kTitle        = 0x7BA9;
const std::uint32_t kMuxingApp    = 0x4D80;
const std::uint32_t kWritingApp   = 0x5741;
const std::uint32_t kDateUTC      = 0x4461;
const std::uint32_t kAttachments  = 0x1941A469;
const std::uint32_t kAttachedFile = 0x61A7;
const std::uint32_t kFileDescr    = 0x467E;
const std::uint32_t kFileName     = 0x466E;
const std::uint32_t kFileMedia    = 0x4660;
const std::uint32_t kFileData     = 0x465C;
const std::uint32_t kFileUID      = 0x46AE;
const std::uint32_t kTags         = 0x1254C367;
const std::uint32_t kTag          = 0x7373;
const std::uint32_t kTargets      = 0x63C0;
const std::uint32_t kTagAttachUID = 0x63C6;
const std::uint32_t kSimpleTag    = 0x67C8;
const std::uint32_t kTagName      = 0x45A3;
const std::uint32_t kTagString    = 0x4487;
const std::uint32_t kTagBinary    = 0x4485;
const std::uint32_t kVoid         = 0xEC;
const std::uint32_t kCRC32        = 0xBF;

const std::uint32_t kPtoKind        = 0x1E54F001;
const std::uint32_t kPtoEncoding    = 0x1E54F002;
const std::uint32_t kPtoRowCount    = 0x1E54F003;
const std::uint32_t kPtoGeneration  = 0x1E54F010;
const std::uint32_t kPtoSeekUID     = 0x1E54F011;
const std::uint32_t kPtoTagIndex    = 0x1E54F020;
const std::uint32_t kPtoTagSrcType  = 0x1E54F021;
const std::uint32_t kPtoTagUInt     = 0x1E54F022;
const std::uint32_t kPtoTagInt      = 0x1E54F023;
const std::uint32_t kPtoTagFloat    = 0x1E54F024;
const std::uint32_t kPtoTagDate     = 0x1E54F025;
const std::uint32_t kPtoTagUID      = 0x1E54F026;
const std::uint32_t kPtoTagUIDs     = 0x1E54F027;
const std::uint32_t kPtoTagFloats   = 0x1E54F028;
const std::uint32_t kPtoTagInts     = 0x1E54F029;
const std::uint32_t kPtoAnnotations = 0x1E54F100;
const std::uint32_t kPtoAnnotation  = 0x1E54F101;
const std::uint32_t kPtoAnnTarget   = 0x1E54F102;
const std::uint32_t kPtoAnnFirstRow = 0x1E54F103;
const std::uint32_t kPtoAnnLastRow  = 0x1E54F104;
const std::uint32_t kPtoAnnText     = 0x1E54F105;
const std::uint32_t kPtoAnnAuthor   = 0x1E54F106;
const std::uint32_t kPtoAnnDate     = 0x1E54F107;

/// Every size that may have to grow later is written this wide from the start.
/// RFC 8794 permits an over-wide Data Size expressly so it can be overwritten,
/// and that permission is what makes in-place update possible at all.
const int kWideSize = 8;

/// Payload bytes each SeekHead is given, so it can be rewritten where it lies.
/// The commit protocol depends on that: a SeekHead that had to move could not
/// be the fallback for the move.
const std::uint64_t kSeekHeadReserve = 8192;

/// A Void needs one octet of id and one of size, so a gap of exactly one octet
/// can hold nothing at all and must never be left.
const std::uint64_t kMinVoid = 2;

// --- EBML primitives --------------------------------------------------------

int id_octets(std::uint32_t id) {
    if (id <= 0xFF) return 1;
    if (id <= 0xFFFF) return 2;
    if (id <= 0xFFFFFF) return 3;
    return 4;
}

/// Octets needed to hold `v` as a Data Size. All-ones is reserved for "unknown",
/// so a value that would encode as all-ones needs one octet more.
int size_octets(std::uint64_t v) {
    for (int n = 1; n <= 8; n++)
        if (v < ((1ULL << (7 * n)) - 1)) return n;
    return 8;
}

/// Octets a value occupies as an EBML integer: the fewest that hold it.
int uint_octets(std::uint64_t v) {
    int n = 0;
    while (v != 0) { n++; v >>= 8; }
    return n == 0 ? 1 : n;
}

std::uint32_t crc32_ebml(const unsigned char* p, std::size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

/// Builds an element tree in memory. Everything except a payload goes through
/// here; payloads are streamed straight to the file and never buffered.
struct Buf {
    std::vector<unsigned char> b;

    void raw(const void* p, std::size_t n) {
        const unsigned char* c = static_cast<const unsigned char*>(p);
        b.insert(b.end(), c, c + n);
    }
    void put_id(std::uint32_t id) {
        const int n = id_octets(id);
        for (int i = n - 1; i >= 0; i--) b.push_back((id >> (8 * i)) & 0xFF);
    }
    void put_size(std::uint64_t v, int octets = 0) {
        const int n = octets ? octets : size_octets(v);
        const std::uint64_t enc = v | (1ULL << (7 * n));
        for (int i = n - 1; i >= 0; i--) b.push_back((enc >> (8 * i)) & 0xFF);
    }
    void be(std::uint64_t v, int n) {
        for (int i = n - 1; i >= 0; i--) b.push_back((v >> (8 * i)) & 0xFF);
    }

    void uint_elem(std::uint32_t id, std::uint64_t v) {
        const int n = uint_octets(v);
        put_id(id); put_size(n); be(v, n);
    }
    void int_elem(std::uint32_t id, long long v) {
        int n = 1;
        while (n < 8) {
            const long long lo = -(1LL << (8 * n - 1)), hi = (1LL << (8 * n - 1)) - 1;
            if (v >= lo && v <= hi) break;
            n++;
        }
        put_id(id); put_size(n); be(static_cast<std::uint64_t>(v), n);
    }
    void float_elem(std::uint32_t id, double v) {
        std::uint64_t bits;
        std::memcpy(&bits, &v, 8);
        put_id(id); put_size(8); be(bits, 8);
    }
    void text_elem(std::uint32_t id, const std::string& s) {
        put_id(id); put_size(s.size()); raw(s.data(), s.size());
    }
    void bytes_elem(std::uint32_t id, const unsigned char* p, std::size_t n) {
        put_id(id); put_size(n); raw(p, n);
    }
    void master(std::uint32_t id, const Buf& child, int size_octets_ = 0) {
        put_id(id); put_size(child.b.size(), size_octets_);
        raw(child.b.data(), child.b.size());
    }
};

/// Walks what Buf wrote. Refuses to run off the end: a truncated element is a
/// damaged file, not licence to read whatever follows.
struct Cursor {
    const unsigned char* p = nullptr;
    std::size_t n = 0, i = 0;
    /// Where the element last returned begins, relative to `p`. What lets a
    /// caller locate its Data Size without assuming a width.
    std::size_t hdr = 0;

    bool done() const { return i >= n; }

    bool element(std::uint32_t* id, const unsigned char** data, std::uint64_t* size) {
        if (i >= n) return false;
        hdr = i;
        const unsigned char first = p[i];
        if (first == 0) return false;
        int len = 1;
        while (len <= 4 && !(first & (0x80 >> (len - 1)))) len++;
        if (len > 4 || i + len > n) return false;
        std::uint32_t v = 0;
        for (int k = 0; k < len; k++) v = (v << 8) | p[i + k];
        i += len;

        if (i >= n) return false;
        const unsigned char sf = p[i];
        if (sf == 0) return false;
        int slen = 1;
        while (slen <= 8 && !(sf & (0x80 >> (slen - 1)))) slen++;
        if (slen > 8 || i + slen > n) return false;
        std::uint64_t sz = sf & (0xFF >> slen);
        for (int k = 1; k < slen; k++) sz = (sz << 8) | p[i + k];
        i += slen;
        if (sz == ((1ULL << (7 * slen)) - 1)) return false;   // unknown size
        if (i + sz > n) return false;

        *id = v; *data = p + i; *size = sz;
        i += static_cast<std::size_t>(sz);
        return true;
    }
};

std::uint64_t get_uint(const unsigned char* p, std::uint64_t n) {
    std::uint64_t v = 0;
    for (std::uint64_t i = 0; i < n && i < 8; i++) v = (v << 8) | p[i];
    return v;
}

long long get_int(const unsigned char* p, std::uint64_t n) {
    if (n == 0) return 0;
    long long v = (p[0] & 0x80) ? -1 : 0;
    for (std::uint64_t i = 0; i < n && i < 8; i++)
        v = static_cast<long long>((static_cast<std::uint64_t>(v) << 8) | p[i]);
    return v;
}

double get_float(const unsigned char* p, std::uint64_t n) {
    if (n == 8) {
        const std::uint64_t bits = get_uint(p, 8);
        double d;
        std::memcpy(&d, &bits, 8);
        return d;
    }
    if (n == 4) {
        std::uint32_t bits = static_cast<std::uint32_t>(get_uint(p, 4));
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    return 0.0;
}

std::string get_text(const unsigned char* p, std::uint64_t n) {
    return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}

// --- the file ---------------------------------------------------------------

class File {
public:
    File() = default;
    ~File() { close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool open(const std::string& path, const char* mode) {
        close();
        f_ = std::fopen(path.c_str(), mode);
        return f_ != nullptr;
    }
    bool ok() const { return f_ != nullptr; }
    std::FILE* get() const { return f_; }
    void close() { if (f_) { std::fclose(f_); f_ = nullptr; } }

    bool seek(std::uint64_t off) {
        return std::fseek(f_, static_cast<long>(off), SEEK_SET) == 0;
    }
    std::uint64_t tell() { return static_cast<std::uint64_t>(std::ftell(f_)); }
    std::uint64_t length() {
        const std::uint64_t here = tell();
        std::fseek(f_, 0, SEEK_END);
        const std::uint64_t n = tell();
        seek(here);
        return n;
    }
    bool write(const void* p, std::size_t n) {
        return n == 0 || std::fwrite(p, 1, n, f_) == n;
    }
    bool read(void* p, std::size_t n) {
        return n == 0 || std::fread(p, 1, n, f_) == n;
    }
    bool at(std::uint64_t off, const void* p, std::size_t n) {
        return seek(off) && write(p, n);
    }
    void flush() { std::fflush(f_); }

private:
    std::FILE* f_ = nullptr;
};

std::uint64_t random_uid() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uint64_t v = 0;
    while (v == 0) v = rng();
    return v;
}

}  // namespace

// --- the model ---------------------------------------------------------------

struct PtoFile::Impl {
    File f;
    std::string path;
    std::string err;
    bool writable = false;
    bool dirty = false;

    std::uint64_t seg_data = 0;        ///< first byte of Segment's data
    std::uint64_t seg_size_at = 0;     ///< where Segment's size VINT lives
    std::uint64_t seg_bytes = 0;       ///< Segment's data length
    std::uint64_t head_at[2] = {0, 0}; ///< the two SeekHead element offsets
    std::uint64_t head_total = 0;      ///< bytes each SeekHead element occupies
    int live = 0;                      ///< which SeekHead is authoritative
    std::uint64_t generation = 0;

    // Metadata, held whole because it is kilobytes.
    std::vector<unsigned char> uuid;
    std::string title, muxing_app, writing_app;
    long long created = 0;
    std::vector<PtoTag> tags;
    std::vector<PtoAnnotation> notes;

    struct Slot {
        PtoObject meta;
        std::uint64_t elem_at = 0;      ///< the Attachments element header
        std::uint64_t elem_bytes = 0;   ///< how much of the file it occupies
        std::uint64_t seg_size_at = 0;  ///< Attachments' size VINT
        std::uint64_t att_size_at = 0;  ///< AttachedFile's size VINT
        std::uint64_t data_size_at = 0; ///< FileData's size VINT
        std::uint64_t slack_at = 0;     ///< a Void right after, or 0
        std::uint64_t slack_bytes = 0;
    };
    std::vector<Slot> slots;

    // Where the relocatable metadata elements currently sit, so they can be
    // rewritten in place when they still fit.
    std::uint64_t info_at = 0, info_bytes = 0;
    std::uint64_t tags_at = 0, tags_bytes = 0;
    std::uint64_t notes_at = 0, notes_bytes = 0;

    std::vector<std::pair<std::uint64_t, std::uint64_t> > freelist;

    bool fail(const std::string& why) { err = why; return false; }

    Slot* find(std::uint64_t uid) {
        for (Slot& s : slots) if (s.meta.uid == uid) return &s;
        return nullptr;
    }
    const Slot* find(std::uint64_t uid) const {
        for (const Slot& s : slots) if (s.meta.uid == uid) return &s;
        return nullptr;
    }

    // -- free space ------------------------------------------------------------

    void release(std::uint64_t off, std::uint64_t bytes) {
        if (bytes < kMinVoid) return;
        // The Void goes in the file as well as in the list: an element left
        // lying there would be found again by the next open, and if it still
        // carried its uid it would be found TWICE.
        write_void(off, bytes);
        freelist.push_back(std::make_pair(off, bytes));
        coalesce();
    }

    void coalesce() {
        std::sort(freelist.begin(), freelist.end());
        std::vector<std::pair<std::uint64_t, std::uint64_t> > out;
        for (std::size_t i = 0; i < freelist.size(); i++) {
            if (!out.empty() && out.back().first + out.back().second == freelist[i].first)
                out.back().second += freelist[i].second;
            else
                out.push_back(freelist[i]);
        }
        freelist.swap(out);
    }

    /// A run of `bytes` for a whole element. Reuses a hole where one fits --
    /// exactly, or with room left for a Void -- and grows the Segment otherwise.
    std::uint64_t allocate(std::uint64_t bytes) {
        for (std::size_t i = 0; i < freelist.size(); i++) {
            const std::uint64_t have = freelist[i].second;
            if (have != bytes && have < bytes + kMinVoid) continue;
            const std::uint64_t at = freelist[i].first;
            if (have == bytes) freelist.erase(freelist.begin() + i);
            else { freelist[i].first += bytes; freelist[i].second -= bytes; }
            return at;
        }
        const std::uint64_t at = seg_data + seg_bytes;
        seg_bytes += bytes;
        return at;
    }

    /// Payload octets of a Void that must occupy exactly `total` bytes. A wider
    /// size VINT means a shorter payload, and for some totals only one width
    /// works out at all.
    static std::uint64_t void_payload_for(std::uint64_t total) {
        for (int octets = 1; octets <= 8; octets++) {
            if (total < 1 + static_cast<std::uint64_t>(octets)) continue;
            const std::uint64_t payload = total - 1 - octets;
            if (size_octets(payload) <= octets) return payload;
        }
        return 0;
    }

    bool write_void(std::uint64_t at, std::uint64_t total) {
        if (total == 0) return true;
        if (total < kMinVoid) return fail("a gap of one octet cannot hold a Void");
        Buf v;
        v.put_id(kVoid);
        // The size VINT has to be chosen so id + size + payload lands exactly on
        // `total`: a wider VINT means a shorter payload, and for some totals only
        // one width works out.
        std::uint64_t payload = 0;
        int octets = 1;
        for (octets = 1; octets <= 8; octets++) {
            if (total < 1 + static_cast<std::uint64_t>(octets)) continue;
            payload = total - 1 - octets;
            if (size_octets(payload) <= octets) break;
        }
        if (octets > 8) return fail("cannot express a Void of that size");
        v.put_size(payload, octets);
        std::vector<unsigned char> zeros(static_cast<std::size_t>(payload), 0);
        return f.at(at, v.b.data(), v.b.size()) && f.write(zeros.data(), zeros.size());
    }

    // -- serialising the metadata ------------------------------------------------

    Buf build_info() const {
        Buf c;
        if (!uuid.empty()) c.bytes_elem(kSegmentUUID, uuid.data(), uuid.size());
        if (!title.empty()) c.text_elem(kTitle, title);
        if (!muxing_app.empty()) c.text_elem(kMuxingApp, muxing_app);
        if (!writing_app.empty()) c.text_elem(kWritingApp, writing_app);
        if (created != 0) { c.put_id(kDateUTC); c.put_size(8);
                            c.be(static_cast<std::uint64_t>(created), 8); }
        Buf e;
        e.master(kInfo, c);
        return e;
    }

    static void tag_value(Buf& s, const PtoTag& t) {
        switch (t.type) {
            case PtoType::Empty: break;
            case PtoType::UInt: s.uint_elem(kPtoTagUInt, t.u); break;
            case PtoType::Int: s.int_elem(kPtoTagInt, t.i); break;
            case PtoType::Float: s.float_elem(kPtoTagFloat, t.d); break;
            case PtoType::Date: s.put_id(kPtoTagDate); s.put_size(8);
                                s.be(static_cast<std::uint64_t>(t.i), 8); break;
            case PtoType::Text: s.text_elem(kTagString, t.text); break;
            case PtoType::Bytes: s.bytes_elem(kTagBinary, t.bytes.data(), t.bytes.size());
                                 break;
            case PtoType::UID: s.uint_elem(kPtoTagUID, t.u); break;
            case PtoType::UIDs: {
                Buf a;
                for (std::size_t i = 0; i < t.uids.size(); i++) a.be(t.uids[i], 8);
                s.bytes_elem(kPtoTagUIDs, a.b.data(), a.b.size());
                break;
            }
            case PtoType::Floats: {
                Buf a;
                for (std::size_t i = 0; i < t.floats.size(); i++) {
                    std::uint64_t bits;
                    std::memcpy(&bits, &t.floats[i], 8);
                    a.be(bits, 8);
                }
                s.bytes_elem(kPtoTagFloats, a.b.data(), a.b.size());
                break;
            }
            case PtoType::Ints: {
                Buf a;
                for (std::size_t i = 0; i < t.ints.size(); i++)
                    a.be(static_cast<std::uint64_t>(t.ints[i]), 8);
                s.bytes_elem(kPtoTagInts, a.b.data(), a.b.size());
                break;
            }
        }
    }

    Buf build_tags() const {
        Buf all;
        for (std::size_t k = 0; k < tags.size(); k++) {
            const PtoTag& t = tags[k];
            Buf targets;
            if (t.target != 0) targets.uint_elem(kTagAttachUID, t.target);
            Buf simple;
            simple.text_elem(kTagName, t.name);
            tag_value(simple, t);
            if (t.index != -1) simple.int_elem(kPtoTagIndex, t.index);
            if (t.source_type != 0) simple.uint_elem(kPtoTagSrcType, t.source_type);
            Buf one;
            one.master(kTargets, targets);
            one.master(kSimpleTag, simple);
            all.master(kTag, one);
        }
        Buf e;
        e.master(kTags, all);
        return e;
    }

    Buf build_notes() const {
        Buf all;
        for (std::size_t k = 0; k < notes.size(); k++) {
            const PtoAnnotation& a = notes[k];
            Buf one;
            if (a.target != 0) one.uint_elem(kPtoAnnTarget, a.target);
            if (a.last_row != 0) {
                one.uint_elem(kPtoAnnFirstRow, a.first_row);
                one.uint_elem(kPtoAnnLastRow, a.last_row);
            }
            if (!a.text.empty()) one.text_elem(kPtoAnnText, a.text);
            if (!a.author.empty()) one.text_elem(kPtoAnnAuthor, a.author);
            if (a.when != 0) { one.put_id(kPtoAnnDate); one.put_size(8);
                               one.be(static_cast<std::uint64_t>(a.when), 8); }
            all.master(kPtoAnnotation, one);
        }
        Buf e;
        e.master(kPtoAnnotations, all);
        return e;
    }

    /// Rewrite one of the three metadata elements, in place if it still fits.
    bool place(const Buf& e, std::uint64_t* at, std::uint64_t* bytes) {
        const std::uint64_t need = e.b.size();
        if (*at != 0 && (need == *bytes || need + kMinVoid <= *bytes)) {
            if (!f.at(*at, e.b.data(), need)) return fail("write failed");
            if (need < *bytes && !write_void(*at + need, *bytes - need)) return false;
            *bytes = need;
            return true;
        }
        if (*at != 0) release(*at, *bytes);
        const std::uint64_t to = allocate(need);
        if (!f.at(to, e.b.data(), need)) return fail("write failed");
        *at = to;
        *bytes = need;
        return true;
    }

    // -- the index ---------------------------------------------------------------

    Buf build_seekhead(std::uint64_t gen) const {
        Buf seeks;
        struct { std::uint32_t id; std::uint64_t at; } top[3] = {
            {kInfo, info_at}, {kTags, tags_at}, {kPtoAnnotations, notes_at}};
        for (int i = 0; i < 3; i++) {
            if (top[i].at == 0) continue;
            Buf s;
            Buf idb;
            idb.put_id(top[i].id);
            s.bytes_elem(kSeekID, idb.b.data(), idb.b.size());
            s.uint_elem(kSeekPosition, top[i].at - seg_data);
            seeks.master(kSeek, s);
        }
        for (std::size_t i = 0; i < slots.size(); i++) {
            Buf s;
            Buf idb;
            idb.put_id(kAttachedFile);
            s.bytes_elem(kSeekID, idb.b.data(), idb.b.size());
            s.uint_elem(kPtoSeekUID, slots[i].meta.uid);
            s.uint_elem(kSeekPosition, slots[i].elem_at - seg_data);
            seeks.master(kSeek, s);
        }

        Buf body;
        body.uint_elem(kPtoGeneration, gen);
        body.raw(seeks.b.data(), seeks.b.size());
        return body;
    }

    /*!
     * \brief Write one index where it lies, padded out to its reserved size.
     *
     * The CRC-32 covers every other child of the SeekHead, INCLUDING the Void
     * that pads it -- which is what RFC 8794 says ("all sibling data except
     * itself") and therefore what a generic EBML tool will check. Covering only
     * the useful part would have been self-consistent and wrong.
     */
    bool write_seekhead(int which, std::uint64_t gen) {
        const std::size_t crc_bytes = 1 + 1 + 4;      // id, size, the checksum
        Buf body = build_seekhead(gen);
        if (crc_bytes + body.b.size() + kMinVoid > kSeekHeadReserve &&
            crc_bytes + body.b.size() != kSeekHeadReserve)
            return fail("the index outgrew its reserved space; compact the file");

        const std::uint64_t pad = kSeekHeadReserve - crc_bytes - body.b.size();
        if (pad != 0) {
            Buf v;
            const std::uint64_t payload = void_payload_for(pad);
            v.put_id(kVoid);
            v.put_size(payload, static_cast<int>(pad - 1 - payload));
            v.b.resize(v.b.size() + static_cast<std::size_t>(payload), 0);
            body.raw(v.b.data(), v.b.size());
        }

        Buf e;
        e.put_id(kSeekHead);
        e.put_size(kSeekHeadReserve, kWideSize);
        const std::uint32_t crc = crc32_ebml(body.b.data(), body.b.size());
        const unsigned char le[4] = {static_cast<unsigned char>(crc & 0xFF),
                                     static_cast<unsigned char>((crc >> 8) & 0xFF),
                                     static_cast<unsigned char>((crc >> 16) & 0xFF),
                                     static_cast<unsigned char>((crc >> 24) & 0xFF)};
        e.bytes_elem(kCRC32, le, 4);
        e.raw(body.b.data(), body.b.size());
        return f.at(head_at[which], e.b.data(), e.b.size()) ? true : fail("write failed");
    }

    bool patch_segment_size() {
        Buf s;
        s.put_size(seg_bytes, kWideSize);
        return f.at(seg_size_at, s.b.data(), s.b.size());
    }
};

// --- construction ------------------------------------------------------------

PtoFile::PtoFile() : p_(new Impl) {}
PtoFile::~PtoFile() { delete p_; }

bool PtoFile::is_open() const { return p_->f.ok(); }
const std::string& PtoFile::filename() const { return p_->path; }
const std::string& PtoFile::error() const { return p_->err; }
void PtoFile::close() { p_->f.close(); }

bool PtoFile::create(const std::string& filename, const std::string& title) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.f.open(filename, "w+b")) return m.fail("cannot create " + filename);
    m.path = filename;
    m.writable = true;
    m.slots.clear();
    m.tags.clear();
    m.notes.clear();
    m.freelist.clear();
    m.title = title;
    m.muxing_app = "tttrlib";
    m.writing_app.clear();
    m.created = 0;
    m.generation = 0;
    m.live = 1;                       // so the first commit writes slot 0

    m.uuid.resize(16);
    {
        static std::mt19937_64 rng(std::random_device{}());
        for (int i = 0; i < 2; i++) {
            const std::uint64_t v = rng();
            std::memcpy(&m.uuid[i * 8], &v, 8);
        }
    }

    Buf head;
    {
        Buf c;
        c.uint_elem(kEBMLVersion, 1);
        c.uint_elem(kEBMLReadVersion, 1);
        c.uint_elem(kEBMLMaxIDLength, 4);
        c.uint_elem(kEBMLMaxSizeLen, 8);
        c.text_elem(kDocType, "pto");
        c.uint_elem(kDocTypeVersion, 1);
        c.uint_elem(kDocTypeReadVer, 1);
        head.master(kEBML, c);
    }
    if (!m.f.at(0, head.b.data(), head.b.size())) return m.fail("write failed");

    Buf seg;
    seg.put_id(kSegment);
    seg.put_size(0, kWideSize);
    m.seg_size_at = head.b.size() + id_octets(kSegment);
    if (!m.f.write(seg.b.data(), seg.b.size())) return m.fail("write failed");
    m.seg_data = m.seg_size_at + kWideSize;
    m.seg_bytes = 0;

    // The two indexes come first, each with room to be rewritten where it lies.
    const std::uint64_t total = id_octets(kSeekHead) + kWideSize + kSeekHeadReserve;
    m.head_total = total;
    for (int i = 0; i < 2; i++) {
        m.head_at[i] = m.seg_data + m.seg_bytes;
        m.seg_bytes += total;
        Buf e;
        e.put_id(kSeekHead);
        e.put_size(kSeekHeadReserve, kWideSize);
        if (!m.f.at(m.head_at[i], e.b.data(), e.b.size())) return m.fail("write failed");
        if (!m.write_void(m.head_at[i] + e.b.size(), kSeekHeadReserve)) return false;
    }
    // An empty but valid index, rather than a full commit: committing here
    // would place an Info element before the caller has set anything, and the
    // next commit would then have to move it and leave a hole behind. A file
    // that was just created should not already need compacting.
    if (!m.write_seekhead(0, 1)) return false;
    m.live = 0;
    m.generation = 1;
    if (!m.patch_segment_size()) return m.fail("write failed");
    m.f.flush();
    m.dirty = true;
    return true;
}

// --- reading a file back ------------------------------------------------------

namespace {

/// Read a whole element's payload, given where its header starts.
bool read_element(File& f, std::uint64_t at, std::uint32_t* id,
                  std::vector<unsigned char>* payload, std::uint64_t* total,
                  std::uint64_t* size_at) {
    unsigned char head[16];
    if (!f.seek(at) || !f.read(head, 1)) return false;
    int len = 1;
    while (len <= 4 && !(head[0] & (0x80 >> (len - 1)))) len++;
    if (len > 4) return false;
    if (len > 1 && !f.read(head + 1, len - 1)) return false;
    std::uint32_t v = 0;
    for (int k = 0; k < len; k++) v = (v << 8) | head[k];

    unsigned char sz[8];
    if (!f.read(sz, 1)) return false;
    int slen = 1;
    while (slen <= 8 && !(sz[0] & (0x80 >> (slen - 1)))) slen++;
    if (slen > 8) return false;
    if (slen > 1 && !f.read(sz + 1, slen - 1)) return false;
    std::uint64_t n = sz[0] & (0xFF >> slen);
    for (int k = 1; k < slen; k++) n = (n << 8) | sz[k];
    if (n == ((1ULL << (7 * slen)) - 1)) return false;

    *id = v;
    *total = len + slen + n;
    if (size_at) *size_at = at + len;
    if (payload != nullptr) {
        payload->resize(static_cast<std::size_t>(n));
        if (!f.read(payload->data(), payload->size())) return false;
    }
    return true;
}

}  // namespace

bool PtoFile::open(const std::string& filename, bool writable) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.f.open(filename, writable ? "r+b" : "rb"))
        return m.fail("cannot open " + filename);
    m.path = filename;
    m.writable = writable;
    m.slots.clear();
    m.tags.clear();
    m.notes.clear();
    m.freelist.clear();
    m.info_at = m.tags_at = m.notes_at = 0;
    m.info_bytes = m.tags_bytes = m.notes_bytes = 0;

    // EBML header, and the DocType that says this is ours.
    std::uint32_t id = 0;
    std::vector<unsigned char> payload;
    std::uint64_t total = 0;
    if (!read_element(m.f, 0, &id, &payload, &total, nullptr) || id != kEBML)
        return m.fail(filename + " is not an EBML file");
    {
        Cursor c{payload.data(), payload.size(), 0};
        std::uint32_t cid;
        const unsigned char* d;
        std::uint64_t n;
        std::string doctype;
        std::uint64_t read_version = 1;
        while (c.element(&cid, &d, &n)) {
            if (cid == kDocType) doctype = get_text(d, n);
            else if (cid == kDocTypeReadVer) read_version = get_uint(d, n);
        }
        if (doctype != "pto") return m.fail(filename + " is not a PTO file");
        if (read_version > 1)
            return m.fail(filename + " needs a newer PTO reader (DocTypeReadVersion "
                          + std::to_string(read_version) + ")");
    }

    std::uint64_t seg_at = total;
    if (!read_element(m.f, seg_at, &id, nullptr, &total, &m.seg_size_at) ||
        id != kSegment)
        return m.fail(filename + " has no Segment");
    m.seg_data = m.seg_size_at + kWideSize;
    m.seg_bytes = total - (m.seg_data - seg_at);

    // Walk the Segment's children. Sizes are in the file, so this needs a seek
    // per element and reads nothing but the small ones.
    struct Child { std::uint64_t at, total; std::uint32_t id; };
    std::vector<Child> children;
    for (std::uint64_t at = m.seg_data; at < m.seg_data + m.seg_bytes; ) {
        std::uint32_t cid;
        std::uint64_t ctotal;
        if (!read_element(m.f, at, &cid, nullptr, &ctotal, nullptr) || ctotal == 0)
            return m.fail(filename + " is damaged: an element at " +
                          std::to_string(at) + " could not be read");
        Child c; c.at = at; c.total = ctotal; c.id = cid;
        children.push_back(c);
        at += ctotal;
    }

    // The two indexes, and which of them is the truth.
    int found = 0;
    std::uint64_t best_gen = 0;
    int best = -1;
    std::vector<std::uint64_t> live_uids;
    bool have_live = false;
    for (std::size_t i = 0; i < children.size() && found < 2; i++) {
        if (children[i].id != kSeekHead) continue;
        m.head_at[found] = children[i].at;
        m.head_total = children[i].total;
        std::uint32_t hid;
        std::uint64_t htotal;
        std::vector<unsigned char> body;
        if (read_element(m.f, children[i].at, &hid, &body, &htotal, nullptr)) {
            Cursor c{body.data(), body.size(), 0};
            std::uint32_t cid;
            const unsigned char* d;
            std::uint64_t n;
            std::uint32_t stored = 0;
            bool has_crc = false;
            std::size_t after_crc = 0;
            std::uint64_t gen = 0;
            std::vector<std::uint64_t> uids;
            while (c.element(&cid, &d, &n)) {
                if (cid == kCRC32 && n == 4) {
                    stored = static_cast<std::uint32_t>(d[0]) |
                             (static_cast<std::uint32_t>(d[1]) << 8) |
                             (static_cast<std::uint32_t>(d[2]) << 16) |
                             (static_cast<std::uint32_t>(d[3]) << 24);
                    has_crc = true;
                    after_crc = c.i;
                } else if (cid == kPtoGeneration) {
                    gen = get_uint(d, n);
                } else if (cid == kSeek) {
                    Cursor s{d, static_cast<std::size_t>(n), 0};
                    std::uint32_t sid;
                    const unsigned char* sd;
                    std::uint64_t sn;
                    std::uint64_t uid = 0;
                    while (s.element(&sid, &sd, &sn))
                        if (sid == kPtoSeekUID) uid = get_uint(sd, sn);
                    if (uid != 0) uids.push_back(uid);
                }
            }
            // Everything after the CRC element, padding included: that is what
            // "all sibling data except itself" means, and what a generic EBML
            // tool will compute.
            const std::size_t body_end = body.size();
            if (has_crc && body_end >= after_crc) {
                const std::uint32_t got = crc32_ebml(body.data() + after_crc,
                                                     body_end - after_crc);
                if (got == stored && (best < 0 || gen > best_gen)) {
                    best = found;
                    best_gen = gen;
                    live_uids = uids;
                    have_live = true;
                }
            }
        }
        found++;
    }
    if (found < 2) return m.fail(filename + " has no index");
    m.live = best < 0 ? 0 : best;
    m.generation = best_gen;

    // Everything the live index does not vouch for is space to reuse: a
    // half-finished write from a session that died leaves valid elements that
    // were never committed.
    for (std::size_t i = 0; i < children.size(); i++) {
        const Child& c = children[i];
        if (c.id == kSeekHead) continue;
        if (c.id == kVoid) { m.freelist.push_back(std::make_pair(c.at, c.total)); continue; }

        std::uint32_t cid;
        std::vector<unsigned char> body;
        std::uint64_t ctotal, size_at;
        if (c.id == kAttachments) {
            if (!read_element(m.f, c.at, &cid, &body, &ctotal, &size_at)) continue;
            Impl::Slot s;
            s.elem_at = c.at;
            s.elem_bytes = c.total;
            s.seg_size_at = size_at;
            // Where the Attachments PAYLOAD starts -- the element header is
            // however many octets the id and the size took, which is not a
            // constant for a file somebody else wrote.
            const std::uint64_t atts_payload_at = c.at + c.total - body.size();
            Cursor outer{body.data(), body.size(), 0};
            std::uint32_t aid;
            const unsigned char* ad;
            std::uint64_t an;
            if (!outer.element(&aid, &ad, &an) || aid != kAttachedFile) continue;
            const std::uint64_t att_payload_at = atts_payload_at + (ad - body.data());
            s.att_size_at = atts_payload_at + outer.hdr + id_octets(kAttachedFile);
            Cursor in{ad, static_cast<std::size_t>(an), 0};
            std::uint32_t fid;
            const unsigned char* fd;
            std::uint64_t fn;
            while (in.element(&fid, &fd, &fn)) {
                switch (fid) {
                    case kFileUID: s.meta.uid = get_uint(fd, fn); break;
                    case kPtoKind: s.meta.kind = get_text(fd, fn); break;
                    case kPtoEncoding: s.meta.encoding = get_text(fd, fn); break;
                    case kFileName: s.meta.name = get_text(fd, fn); break;
                    case kFileMedia: s.meta.media_type = get_text(fd, fn); break;
                    case kFileDescr: s.meta.description = get_text(fd, fn); break;
                    case kPtoRowCount: s.meta.rows = get_uint(fd, fn); break;
                    case kFileData:
                        s.meta.offset = att_payload_at + (fd - ad);
                        s.meta.size = fn;
                        s.data_size_at = att_payload_at + in.hdr + id_octets(kFileData);
                        break;
                    default: break;
                }
            }
            const bool committed =
                    !have_live ||
                    std::find(live_uids.begin(), live_uids.end(), s.meta.uid) !=
                            live_uids.end();
            if (s.meta.uid == 0 || !committed) {
                m.freelist.push_back(std::make_pair(c.at, c.total));
                continue;
            }
            m.slots.push_back(s);
            continue;
        }

        if (!read_element(m.f, c.at, &cid, &body, &ctotal, nullptr)) continue;
        Cursor in{body.data(), body.size(), 0};
        std::uint32_t eid;
        const unsigned char* ed;
        std::uint64_t en;
        if (c.id == kInfo) {
            m.info_at = c.at; m.info_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid == kSegmentUUID) m.uuid.assign(ed, ed + en);
                else if (eid == kTitle) m.title = get_text(ed, en);
                else if (eid == kMuxingApp) m.muxing_app = get_text(ed, en);
                else if (eid == kWritingApp) m.writing_app = get_text(ed, en);
                else if (eid == kDateUTC) m.created = get_int(ed, en);
            }
        } else if (c.id == kTags) {
            m.tags_at = c.at; m.tags_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid != kTag) continue;
                PtoTag t;
                Cursor tc{ed, static_cast<std::size_t>(en), 0};
                std::uint32_t xid;
                const unsigned char* xd;
                std::uint64_t xn;
                while (tc.element(&xid, &xd, &xn)) {
                    if (xid == kTargets) {
                        Cursor g{xd, static_cast<std::size_t>(xn), 0};
                        std::uint32_t gid;
                        const unsigned char* gd;
                        std::uint64_t gn;
                        while (g.element(&gid, &gd, &gn))
                            if (gid == kTagAttachUID) t.target = get_uint(gd, gn);
                    } else if (xid == kSimpleTag) {
                        Cursor g{xd, static_cast<std::size_t>(xn), 0};
                        std::uint32_t gid;
                        const unsigned char* gd;
                        std::uint64_t gn;
                        while (g.element(&gid, &gd, &gn)) {
                            switch (gid) {
                                case kTagName: t.name = get_text(gd, gn); break;
                                case kPtoTagIndex: t.index = static_cast<int>(get_int(gd, gn)); break;
                                case kPtoTagSrcType: t.source_type =
                                        static_cast<std::uint32_t>(get_uint(gd, gn)); break;
                                case kPtoTagUInt: t.type = PtoType::UInt; t.u = get_uint(gd, gn); break;
                                case kPtoTagInt: t.type = PtoType::Int; t.i = get_int(gd, gn); break;
                                case kPtoTagFloat: t.type = PtoType::Float; t.d = get_float(gd, gn); break;
                                case kPtoTagDate: t.type = PtoType::Date; t.i = get_int(gd, gn); break;
                                case kTagString: t.type = PtoType::Text; t.text = get_text(gd, gn); break;
                                case kTagBinary: t.type = PtoType::Bytes;
                                                 t.bytes.assign(gd, gd + gn); break;
                                case kPtoTagUID: t.type = PtoType::UID; t.u = get_uint(gd, gn); break;
                                case kPtoTagUIDs: t.type = PtoType::UIDs;
                                    for (std::uint64_t k = 0; k + 8 <= gn; k += 8)
                                        t.uids.push_back(get_uint(gd + k, 8));
                                    break;
                                case kPtoTagFloats: t.type = PtoType::Floats;
                                    for (std::uint64_t k = 0; k + 8 <= gn; k += 8)
                                        t.floats.push_back(get_float(gd + k, 8));
                                    break;
                                case kPtoTagInts: t.type = PtoType::Ints;
                                    for (std::uint64_t k = 0; k + 8 <= gn; k += 8)
                                        t.ints.push_back(get_int(gd + k, 8));
                                    break;
                                default: break;
                            }
                        }
                    }
                }
                m.tags.push_back(t);
            }
        } else if (c.id == kPtoAnnotations) {
            m.notes_at = c.at; m.notes_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid != kPtoAnnotation) continue;
                PtoAnnotation a;
                Cursor ac{ed, static_cast<std::size_t>(en), 0};
                std::uint32_t xid;
                const unsigned char* xd;
                std::uint64_t xn;
                while (ac.element(&xid, &xd, &xn)) {
                    switch (xid) {
                        case kPtoAnnTarget: a.target = get_uint(xd, xn); break;
                        case kPtoAnnFirstRow: a.first_row = get_uint(xd, xn); break;
                        case kPtoAnnLastRow: a.last_row = get_uint(xd, xn); break;
                        case kPtoAnnText: a.text = get_text(xd, xn); break;
                        case kPtoAnnAuthor: a.author = get_text(xd, xn); break;
                        case kPtoAnnDate: a.when = get_int(xd, xn); break;
                        default: break;
                    }
                }
                m.notes.push_back(a);
            }
        } else {
            // Something a newer writer put here. Left where it is, untouched.
        }
    }

    // Anything past the end of the Segment is an abandoned write -- a session
    // that added an object and died before the commit that would have claimed
    // the bytes. They are reclaimed here rather than left to grow the file
    // forever, which needs the space to become a Void that the next walk can
    // step over.
    const std::uint64_t end = m.seg_data + m.seg_bytes;
    const std::uint64_t on_disk = m.f.length();
    if (writable && on_disk > end && on_disk - end >= kMinVoid) {
        const std::uint64_t stray = on_disk - end;
        if (m.write_void(end, stray)) {
            m.seg_bytes += stray;
            m.freelist.push_back(std::make_pair(end, stray));
        }
    }

    // A Void immediately after an object is that object's room to grow.
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        Impl::Slot& s = m.slots[i];
        const std::uint64_t after = s.elem_at + s.elem_bytes;
        for (std::size_t k = 0; k < m.freelist.size(); k++) {
            if (m.freelist[k].first != after) continue;
            s.slack_at = after;
            s.slack_bytes = m.freelist[k].second;
            m.freelist.erase(m.freelist.begin() + k);
            break;
        }
        s.meta.capacity = s.meta.size + s.slack_bytes;
    }
    m.coalesce();
    m.dirty = false;
    return true;
}

// --- objects -------------------------------------------------------------------

int PtoFile::n_objects() const { return static_cast<int>(p_->slots.size()); }

std::vector<PtoObject> PtoFile::objects() const {
    std::vector<PtoObject> out;
    out.reserve(p_->slots.size());
    for (std::size_t i = 0; i < p_->slots.size(); i++) out.push_back(p_->slots[i].meta);
    return out;
}

bool PtoFile::has(std::uint64_t uid) const { return p_->find(uid) != nullptr; }

PtoObject PtoFile::object(std::uint64_t uid) const {
    const Impl::Slot* s = p_->find(uid);
    if (s == nullptr) throw std::invalid_argument("no object with that uid");
    return s->meta;
}

std::uint64_t PtoFile::find(const std::string& name) const {
    for (std::size_t i = 0; i < p_->slots.size(); i++)
        if (p_->slots[i].meta.name == name) return p_->slots[i].meta.uid;
    return 0;
}

std::uint64_t PtoFile::add(const std::string& kind, const std::string& encoding,
                           const std::string& name, const unsigned char* data,
                           std::size_t n, std::uint64_t reserve) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) { m.fail("opened read-only"); return 0; }

    Impl::Slot s;
    s.meta.uid = random_uid();
    s.meta.kind = kind;
    s.meta.encoding = encoding;
    s.meta.name = name;
    s.meta.size = n;

    // Everything but the payload, so the header can be written and the payload
    // streamed after it -- a gigabyte never goes through a buffer.
    Buf head;
    head.uint_elem(kFileUID, s.meta.uid);
    head.text_elem(kPtoKind, kind);
    head.text_elem(kPtoEncoding, encoding);
    if (!name.empty()) head.text_elem(kFileName, name);

    const std::uint64_t att_payload = head.b.size() + id_octets(kFileData) + kWideSize + n;
    const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
    const std::uint64_t elem_total = id_octets(kAttachments) + kWideSize + att_total;

    if (reserve != 0 && reserve < kMinVoid) reserve = kMinVoid;
    const std::uint64_t at = m.allocate(elem_total + reserve);

    Buf prefix;
    prefix.put_id(kAttachments);
    prefix.put_size(att_total, kWideSize);
    prefix.put_id(kAttachedFile);
    prefix.put_size(att_payload, kWideSize);
    prefix.raw(head.b.data(), head.b.size());
    prefix.put_id(kFileData);
    prefix.put_size(n, kWideSize);

    s.elem_at = at;
    s.elem_bytes = elem_total;
    s.seg_size_at = at + id_octets(kAttachments);
    s.att_size_at = s.seg_size_at + kWideSize + id_octets(kAttachedFile);
    s.data_size_at = at + prefix.b.size() - kWideSize;
    s.meta.offset = at + prefix.b.size();

    if (!m.f.at(at, prefix.b.data(), prefix.b.size()) ||
        !m.f.write(data, n)) { m.fail("write failed"); return 0; }
    if (reserve != 0 && !m.write_void(at + elem_total, reserve)) return 0;
    s.slack_at = reserve ? at + elem_total : 0;
    s.slack_bytes = reserve;
    s.meta.capacity = n + reserve;

    m.slots.push_back(s);
    m.dirty = true;
    return s.meta.uid;
}

bool PtoFile::update(std::uint64_t uid, const unsigned char* data, std::size_t n) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");
    Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");

    // Patching a size where it lies only works if it was written wide enough to
    // hold a bigger number. A file from a writer that packed its sizes tightly
    // takes the relocating path rather than a corrupted one.
    const bool patchable =
            (s->meta.offset - s->data_size_at) == static_cast<std::uint64_t>(kWideSize);
    const std::uint64_t room = patchable ? s->meta.size + s->slack_bytes : 0;
    const std::uint64_t left = room >= n ? room - n : 0;
    if (n <= room && (left == 0 || left >= kMinVoid)) {
        const std::uint64_t grew = n > s->meta.size ? n - s->meta.size : 0;
        const std::uint64_t shrank = s->meta.size > n ? s->meta.size - n : 0;
        Buf sz;
        sz.put_size(n, kWideSize);
        Buf att;
        att.put_size(s->meta.size ? 0 : 0, kWideSize);   // placeholder, replaced below
        const std::uint64_t att_payload =
                (s->meta.offset - (s->att_size_at + kWideSize)) + n;
        const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
        Buf a, b;
        a.put_size(att_payload, kWideSize);
        b.put_size(att_total, kWideSize);

        if (!m.f.at(s->data_size_at, sz.b.data(), sz.b.size()) ||
            !m.f.at(s->att_size_at, a.b.data(), a.b.size()) ||
            !m.f.at(s->seg_size_at, b.b.data(), b.b.size()) ||
            !m.f.at(s->meta.offset, data, n))
            return m.fail("write failed");

        s->elem_bytes = id_octets(kAttachments) + kWideSize + att_total;
        s->meta.size = n;
        s->slack_at = left ? s->elem_at + s->elem_bytes : 0;
        s->slack_bytes = left;
        if (left && !m.write_void(s->slack_at, left)) return false;
        (void)grew; (void)shrank;
        m.dirty = true;
        return true;
    }

    // Too big for the hole it is in: write it somewhere else and let the old
    // space go. The uid survives, the offset does not.
    const PtoObject old = s->meta;
    const std::uint64_t old_at = s->elem_at;
    const std::uint64_t old_bytes = s->elem_bytes + s->slack_bytes;
    const std::uint64_t keep_uid = old.uid;

    for (std::size_t i = 0; i < m.slots.size(); i++)
        if (m.slots[i].meta.uid == uid) { m.slots.erase(m.slots.begin() + i); break; }
    m.release(old_at, old_bytes);

    const std::uint64_t made = add(old.kind, old.encoding, old.name, data, n,
                                   n / 8 + 64);
    if (made == 0) return false;
    Impl::Slot* fresh = m.find(made);
    fresh->meta.uid = keep_uid;
    fresh->meta.media_type = old.media_type;
    fresh->meta.description = old.description;
    fresh->meta.rows = old.rows;
    // The uid is inside the element, so it has to be rewritten there too.
    Buf u;
    u.uint_elem(kFileUID, keep_uid);
    const std::uint64_t uid_at = fresh->att_size_at + kWideSize;
    if (!m.f.at(uid_at, u.b.data(), u.b.size())) return m.fail("write failed");
    m.dirty = true;
    return true;
}

bool PtoFile::remove(std::uint64_t uid) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        if (m.slots[i].meta.uid != uid) continue;
        m.release(m.slots[i].elem_at, m.slots[i].elem_bytes + m.slots[i].slack_bytes);
        m.slots.erase(m.slots.begin() + i);
        m.dirty = true;
        return true;
    }
    return m.fail("no object with that uid");
}

std::vector<unsigned char> PtoFile::read(std::uint64_t uid) const {
    const Impl::Slot* s = p_->find(uid);
    if (s == nullptr) throw std::runtime_error("no object with that uid");
    std::vector<unsigned char> out(static_cast<std::size_t>(s->meta.size));
    if (!out.empty()) {
        if (!p_->f.seek(s->meta.offset) || !p_->f.read(out.data(), out.size()))
            throw std::runtime_error("could not read the payload of " +
                                     std::to_string(uid));
    }
    return out;
}

bool PtoFile::extract(std::uint64_t uid, const std::string& filename) const {
    Impl& m = *p_;
    m.err.clear();
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");

    File out;
    if (!out.open(filename, "wb")) return m.fail("cannot create " + filename);
    if (!m.f.seek(s->meta.offset)) return m.fail("cannot reach the payload");

    // In blocks, so an eight-gigabyte stream costs eight gigabytes of disk and
    // a few kilobytes of memory rather than both.
    std::vector<unsigned char> chunk(1u << 20);
    std::uint64_t left = s->meta.size;
    while (left > 0) {
        const std::size_t take =
                static_cast<std::size_t>(left < chunk.size() ? left : chunk.size());
        if (!m.f.read(chunk.data(), take) || !out.write(chunk.data(), take))
            return m.fail("could not copy the payload of " + std::to_string(uid));
        left -= take;
    }
    return true;
}

std::vector<std::string> PtoFile::disassemble(const std::string& directory) const {
    std::vector<std::string> written;
    std::vector<std::string> used;
    const std::string sep = directory.empty() ? "" : "/";
    for (std::size_t i = 0; i < p_->slots.size(); i++) {
        const PtoObject& o = p_->slots[i].meta;
        std::string name = o.name;
        if (name.empty()) name = std::to_string(o.uid);
        // A name is a label, and two objects may share one. The first keeps it.
        if (std::find(used.begin(), used.end(), name) != used.end())
            name = std::to_string(o.uid) + "-" + name;
        used.push_back(name);
        const std::string path = directory + sep + name;
        if (!extract(o.uid, path)) return std::vector<std::string>();
        written.push_back(path);
    }
    return written;
}

// --- metadata ---------------------------------------------------------------------

std::string PtoFile::title() const { return p_->title; }
void PtoFile::set_title(const std::string& s) { p_->title = s; p_->dirty = true; }
std::string PtoFile::writing_app() const { return p_->writing_app; }
void PtoFile::set_writing_app(const std::string& s) {
    p_->writing_app = s; p_->dirty = true;
}
std::vector<unsigned char> PtoFile::uuid() const { return p_->uuid; }
std::uint64_t PtoFile::generation() const { return p_->generation; }

std::vector<PtoTag> PtoFile::tags() const { return p_->tags; }

std::vector<PtoTag> PtoFile::tags_for(std::uint64_t uid) const {
    std::vector<PtoTag> out;
    for (std::size_t i = 0; i < p_->tags.size(); i++)
        if (p_->tags[i].target == uid) out.push_back(p_->tags[i]);
    return out;
}

void PtoFile::add_tag(const PtoTag& tag) { p_->tags.push_back(tag); p_->dirty = true; }
void PtoFile::set_tags(const std::vector<PtoTag>& tags) {
    p_->tags = tags; p_->dirty = true;
}
void PtoFile::clear_tags() { p_->tags.clear(); p_->dirty = true; }

std::vector<PtoAnnotation> PtoFile::annotations() const { return p_->notes; }
void PtoFile::add_annotation(const PtoAnnotation& note) {
    p_->notes.push_back(note); p_->dirty = true;
}
void PtoFile::clear_annotations() { p_->notes.clear(); p_->dirty = true; }

std::vector<PtoExtent> PtoFile::free_extents() const {
    std::vector<PtoExtent> out;
    out.reserve(p_->freelist.size());
    for (std::size_t i = 0; i < p_->freelist.size(); i++) {
        PtoExtent e;
        e.offset = p_->freelist[i].first;
        e.bytes = p_->freelist[i].second;
        out.push_back(e);
    }
    return out;
}

// --- commit -------------------------------------------------------------------------

bool PtoFile::commit() {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");

    Buf info = m.build_info();
    if (!m.place(info, &m.info_at, &m.info_bytes)) return false;
    Buf tags = m.build_tags();
    if (!m.tags.empty() || m.tags_at != 0)
        if (!m.place(tags, &m.tags_at, &m.tags_bytes)) return false;
    Buf notes = m.build_notes();
    if (!m.notes.empty() || m.notes_at != 0)
        if (!m.place(notes, &m.notes_at, &m.notes_bytes)) return false;

    if (!m.patch_segment_size()) return m.fail("write failed");
    m.f.flush();

    // The index that is not live, so a crash here falls back to the one that is.
    const int spare = 1 - m.live;
    if (!m.write_seekhead(spare, m.generation + 1)) return false;
    m.f.flush();

    m.live = spare;
    m.generation += 1;
    m.dirty = false;
    return true;
}

bool PtoFile::compact(const std::string& to) {
    Impl& m = *p_;
    m.err.clear();
    PtoFile out;
    if (!out.create(to, m.title)) return m.fail(out.error());
    out.set_writing_app(m.writing_app);
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        const PtoObject& o = m.slots[i].meta;
        std::vector<unsigned char> bytes = read(o.uid);
        const std::uint64_t made = out.add(o.kind, o.encoding, o.name,
                                           bytes.data(), bytes.size());
        if (made == 0) return m.fail(out.error());
        // Keep the identity: everything that refers to this object refers to it
        // by uid, and compaction is not supposed to be observable.
        Impl::Slot* s = out.p_->find(made);
        s->meta.uid = o.uid;
        s->meta.rows = o.rows;
        Buf u;
        u.uint_elem(kFileUID, o.uid);
        if (!out.p_->f.at(s->att_size_at + kWideSize, u.b.data(), u.b.size()))
            return m.fail("write failed");
    }
    out.set_tags(m.tags);
    for (std::size_t i = 0; i < m.notes.size(); i++) out.add_annotation(m.notes[i]);
    return out.commit() ? true : m.fail(out.error());
}

// --- probing --------------------------------------------------------------------

// --- tables ---------------------------------------------------------------------

std::uint64_t pto_add_store(PtoFile& file, const std::string& kind,
                            const std::string& name, const data::DataStore& store,
                            std::uint64_t reserve) {
    PtoFile::Impl& m = *file.p_;
    m.err.clear();
    if (!m.writable) { m.fail("opened read-only"); return 0; }

    PtoFile::Impl::Slot s;
    s.meta.uid = random_uid();
    s.meta.kind = kind;
    s.meta.encoding = "dstore";
    s.meta.name = name;
    s.meta.rows = store.n_rows();

    Buf head;
    head.uint_elem(kFileUID, s.meta.uid);
    head.text_elem(kPtoKind, kind);
    head.text_elem(kPtoEncoding, "dstore");
    if (!name.empty()) head.text_elem(kFileName, name);
    head.text_elem(kFileMedia, "application/x-dstore");
    if (s.meta.rows != 0) head.uint_elem(kPtoRowCount, s.meta.rows);

    // The size is not known until the store has been written, so this always
    // appends -- there is no hole to look for one that fits. The three sizes
    // are written wide and patched afterwards, which is what RFC 8794 permits
    // an over-wide Data Size for.
    const std::uint64_t at = m.seg_data + m.seg_bytes;
    Buf prefix;
    prefix.put_id(kAttachments);
    prefix.put_size(0, kWideSize);
    prefix.put_id(kAttachedFile);
    prefix.put_size(0, kWideSize);
    prefix.raw(head.b.data(), head.b.size());
    prefix.put_id(kFileData);
    prefix.put_size(0, kWideSize);

    if (!m.f.at(at, prefix.b.data(), prefix.b.size())) { m.fail("write failed"); return 0; }

    const std::uint64_t n = write_store_at(m.f.get(), store);
    if (n == 0) { m.fail("could not write the store into the container"); return 0; }

    const std::uint64_t att_payload = head.b.size() + id_octets(kFileData) + kWideSize + n;
    const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
    const std::uint64_t elem_total = id_octets(kAttachments) + kWideSize + att_total;

    s.elem_at = at;
    s.elem_bytes = elem_total;
    s.seg_size_at = at + id_octets(kAttachments);
    s.att_size_at = s.seg_size_at + kWideSize + id_octets(kAttachedFile);
    s.data_size_at = at + prefix.b.size() - kWideSize;
    s.meta.offset = at + prefix.b.size();
    s.meta.size = n;

    Buf a, b, c;
    a.put_size(att_total, kWideSize);
    b.put_size(att_payload, kWideSize);
    c.put_size(n, kWideSize);
    if (!m.f.at(s.seg_size_at, a.b.data(), a.b.size()) ||
        !m.f.at(s.att_size_at, b.b.data(), b.b.size()) ||
        !m.f.at(s.data_size_at, c.b.data(), c.b.size())) {
        m.fail("write failed");
        return 0;
    }

    if (reserve != 0 && reserve < kMinVoid) reserve = kMinVoid;
    m.seg_bytes += elem_total + reserve;
    if (reserve != 0 && !m.write_void(at + elem_total, reserve)) return 0;
    s.slack_at = reserve ? at + elem_total : 0;
    s.slack_bytes = reserve;
    s.meta.capacity = n + reserve;

    m.slots.push_back(s);
    m.dirty = true;
    return s.meta.uid;
}

bool pto_update_store(PtoFile& file, std::uint64_t uid, const data::DataStore& store) {
    // Unlike pto_add_store this buffers, because PtoFile::update has to know
    // the length before it can decide whether the new payload fits where the
    // old one was -- and fitting is the whole point of an update. A table being
    // recomputed is megabytes; a photon stream that is not should be added once
    // and left alone.
    std::FILE* tmp = std::tmpfile();
    if (tmp == nullptr) return file.p_->fail("no temporary file to serialise into");
    const std::uint64_t n = write_store_at(tmp, store);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(n));
    bool ok = n != 0 && std::fseek(tmp, 0, SEEK_SET) == 0 &&
              (bytes.empty() || std::fread(bytes.data(), 1, bytes.size(), tmp) == bytes.size());
    std::fclose(tmp);
    if (!ok) return file.p_->fail("could not serialise the store");
    return file.update(uid, bytes.data(), bytes.size());
}

void pto_mark_sidecar(PtoFile& file, std::uint64_t uid, std::uint64_t primary) {
    PtoTag t;
    t.name = kPtoSidecarTag;
    t.type = PtoType::UID;
    t.target = uid;
    t.u = primary;
    file.add_tag(t);
}

void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out) {
    const PtoFile::Impl& m = *file.p_;
    const PtoFile::Impl::Slot* s = m.find(uid);
    if (s == nullptr) throw std::runtime_error("no object with that uid");
    if (s->meta.encoding != "dstore")
        throw std::runtime_error("object " + std::to_string(uid) + " is encoded as '" +
                                 s->meta.encoding + "', not 'dstore'");
    const_cast<File&>(m.f).flush();
    read_store_into(out, m.path, s->meta.offset, s->meta.size);
}

// --- probing --------------------------------------------------------------------

bool is_pto_file(const std::string& filename) {
    File f;
    if (!f.open(filename, "rb")) return false;
    std::uint32_t id = 0;
    std::vector<unsigned char> payload;
    std::uint64_t total = 0;
    if (!read_element(f, 0, &id, &payload, &total, nullptr) || id != kEBML) return false;
    Cursor c{payload.data(), payload.size(), 0};
    std::uint32_t cid;
    const unsigned char* d;
    std::uint64_t n;
    while (c.element(&cid, &d, &n))
        if (cid == kDocType) return get_text(d, n) == "pto";
    return false;
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
        return true;
    }
    const int container = container_for(o.encoding);
    if (container < 0) return false;
    return out->read_embedded(path.c_str(), container, o.offset, o.size,
                              sidecar_text(file, o.uid)) != 0;
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
        // shift_macro_time: the next measurement continues after this one
        // rather than restarting at zero.
        out->append(&next, true, 0);
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
        f.can_read = true;
        f.can_write = false;
        IORegistry::add(f);
        IORegistry::set_reader("PTO", &read_pto_into_tttr, nullptr);
    }
};
const RegisterPto register_pto;

}  // namespace

}  // namespace io
}  // namespace tttrlib
