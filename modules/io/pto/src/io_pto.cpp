// SPDX-License-Identifier: BSD-3-Clause
#include "io_pto.h"

#include "io_store.h"
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
#include <random>
#include <stdexcept>

// For the writer lock: an exclusive advisory lock on the container, taken
// before it is truncated, so two writers cannot both believe they own it.
#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  include <share.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace tttrlib {
namespace io {


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
/// FileUID is rewritten in place when an object moves, so it is always
/// written in eight octets: a narrower replacement would shorten the
/// element and shift every sibling after it.
const int kUidOctets = 8;
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
const std::uint32_t kCues         = 0x1C53BB6B;
const std::uint32_t kCuePoint     = 0xBB;

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
const std::uint32_t kPtoCueUID      = 0x1E54F030;
const std::uint32_t kPtoCueEvent    = 0x1E54F031;
const std::uint32_t kPtoCueOffset   = 0x1E54F032;
const std::uint32_t kPtoCueTime     = 0x1E54F033;
const std::uint32_t kPtoAnnotations = 0x1E54F100;
const std::uint32_t kPtoAnnotation  = 0x1E54F101;
const std::uint32_t kPtoAnnTarget   = 0x1E54F102;
const std::uint32_t kPtoAnnFirstRow = 0x1E54F103;
const std::uint32_t kPtoAnnLastRow  = 0x1E54F104;
const std::uint32_t kPtoAnnText     = 0x1E54F105;
const std::uint32_t kPtoAnnAuthor   = 0x1E54F106;
const std::uint32_t kPtoAnnDate     = 0x1E54F107;
const std::uint32_t kPtoBanner      = 0x1E54F040;

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

/*!
 * \brief Every payload starts on a multiple of this.
 *
 * EBML guarantees no alignment -- an element header is a variable number of
 * octets, so `FileData` would otherwise begin wherever the name and the encoding
 * strings happened to leave it. That is fine for bytes and wrong for everything
 * a payload actually is here: a PTU record stream is `uint32`, a `.dstore`
 * column is `double`, and a reader that wants to map the file and point at one
 * without copying needs the first byte on a boundary.
 *
 * Eight, not four, and it costs nothing to say eight: it satisfies the 32-bit
 * case as well, and it is the alignment `.dstore` already uses for its own
 * blobs -- those offsets are relative to the store, so the store has to start
 * 8-aligned for them to be 8-aligned in the file.
 *
 * A writer pads with `Void`, records nothing about having done so, and a reader
 * must still check rather than assume: the specification makes this a SHOULD,
 * and an alignment a conformant writer may omit is not one a reader may rely on.
 */
const std::uint64_t kPayloadAlign = 8;

/// Bytes of padding that put `at` on a \ref kPayloadAlign boundary, never
/// leaving a gap of one octet -- which is the one size a Void cannot be.
std::uint64_t align_pad(std::uint64_t at) {
    std::uint64_t pad = (kPayloadAlign - (at % kPayloadAlign)) % kPayloadAlign;
    if (pad != 0 && pad < kMinVoid) pad += kPayloadAlign;
    return pad;
}

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

/*!
 * \brief Bytes of element header in front of an object's payload.
 *
 * `head_bytes` is the AttachedFile's own children -- uid, kind, encoding, name
 * -- whose length varies with the strings. Everything else here is a fixed
 * width, which is what makes the payload's landing position computable before a
 * byte is written, and therefore what makes \ref align_pad possible.
 */
std::uint64_t header_before_payload(std::size_t head_bytes) {
    return id_octets(kAttachments) + kWideSize +
           id_octets(kAttachedFile) + kWideSize +
           head_bytes + id_octets(kFileData) + kWideSize;
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

    /*!
     * An unsigned integer written in a fixed number of octets, so the element
     * has the same length whatever the value is.
     *
     * Needed wherever an element is **rewritten in place**. `uint_elem` packs
     * to the smallest width that holds the value, which is right for a value
     * written once; it is wrong for one that is written again later, because a
     * smaller replacement leaves the tail of the old element behind and every
     * following child is then read from the wrong offset. That is a silent
     * corruption, not a failure: the next parse simply finds an empty
     * PtoEncoding.
     */
    void uint_elem_fixed(std::uint32_t id, std::uint64_t v, int n) {
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

#ifdef _WIN32
/*!
 * \brief Where the writer lock lives: one byte far past any real end of file.
 *
 * A Windows byte-range lock is mandatory -- it stops other processes *reading*
 * the locked range, not just writing it -- so locking byte 0 would shut out the
 * readers this lock exists to leave alone. A byte nothing will ever hold data
 * at makes it a pure flag. POSIX has no equivalent problem: `flock` takes no
 * range and readers never ask for the lock.
 */
const std::uint32_t kLockOffsetLo = 0;
const std::uint32_t kLockOffsetHi = 0x7FFFFFFF;
#endif

class File {
public:
    /// What \ref open_exclusive did about the lock.
    enum LockResult {
        kLockTaken,       ///< we hold it
        kLockBusy,        ///< someone else holds it; nothing was opened
        kLockUnsupported  ///< the filesystem has no locks; the file is open anyway
    };

    File() = default;
    ~File() { close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool open(const std::string& path, const char* mode) {
        close();
        f_ = std::fopen(path.c_str(), mode);
        return f_ != nullptr;
    }

    /*!
     * \brief Open for writing, holding an exclusive advisory lock.
     *
     * The lock is taken on the descriptor before the file is truncated, so a
     * `create` against a container someone else is writing fails without having
     * destroyed it -- which is why this cannot be `fopen("w+b")` plus a lock.
     *
     * \param create make the file if it is missing, and truncate it. False
     *        opens an existing file and fails if there is none.
     * \param why set on every return; see \ref LockResult.
     */
    bool open_exclusive(const std::string& path, bool create, LockResult* why) {
        close();
        *why = kLockTaken;
#ifdef _WIN32
        const int flags = _O_RDWR | _O_BINARY | (create ? (_O_CREAT) : 0);
        int fd = -1;
        if (::_sopen_s(&fd, path.c_str(), flags, _SH_DENYNO,
                       _S_IREAD | _S_IWRITE) != 0 || fd < 0)
            return false;
        HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
        if (h != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov;
            std::memset(&ov, 0, sizeof(ov));
            ov.Offset = kLockOffsetLo;
            ov.OffsetHigh = kLockOffsetHi;
            if (::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                             0, 1, 0, &ov)) {
                locked_ = true;
            } else {
                const DWORD e = ::GetLastError();
                if (e == ERROR_LOCK_VIOLATION || e == ERROR_SHARING_VIOLATION) {
                    ::_close(fd);
                    *why = kLockBusy;
                    return false;
                }
                *why = kLockUnsupported;
            }
        } else {
            *why = kLockUnsupported;
        }
        if (create && ::_chsize_s(fd, 0) != 0) { ::_close(fd); return false; }
        f_ = ::_fdopen(fd, "r+b");
#else
        const int flags = O_RDWR | (create ? O_CREAT : 0);
        int fd = ::open(path.c_str(), flags, 0666);
        if (fd < 0) return false;
        int rc = 0;
        do { rc = ::flock(fd, LOCK_EX | LOCK_NB); } while (rc != 0 && errno == EINTR);
        if (rc == 0) {
            locked_ = true;
        } else if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EACCES) {
            ::close(fd);
            *why = kLockBusy;
            return false;
        } else {
            // Some network mounts have no flock at all. Refusing to open there
            // would trade a rare race for a filesystem the library cannot use.
            *why = kLockUnsupported;
        }
        if (create && ::ftruncate(fd, 0) != 0) { ::close(fd); return false; }
        f_ = ::fdopen(fd, "r+b");
#endif
        if (f_ == nullptr) {
            unlock_fd(fd);
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
            locked_ = false;
            return false;
        }
        return true;
    }

    bool ok() const { return f_ != nullptr; }
    std::FILE* get() const { return f_; }
    void close() {
        if (f_) {
            if (locked_) {
#ifdef _WIN32
                unlock_fd(::_fileno(f_));
#else
                unlock_fd(::fileno(f_));
#endif
                locked_ = false;
            }
            std::fclose(f_);
            f_ = nullptr;
        }
    }

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
    static void unlock_fd(int fd) {
        if (fd < 0) return;
#ifdef _WIN32
        HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
        if (h == INVALID_HANDLE_VALUE) return;
        OVERLAPPED ov;
        std::memset(&ov, 0, sizeof(ov));
        ov.Offset = kLockOffsetLo;
        ov.OffsetHigh = kLockOffsetHi;
        ::UnlockFileEx(h, 0, 1, 0, &ov);
#else
        ::flock(fd, LOCK_UN);
#endif
    }

    std::FILE* f_ = nullptr;
    bool locked_ = false;
};

/*!
 * \brief A fresh object identity: 53 random bits in a 64-bit element, never zero.
 *
 * \par The storage is a uint64 and stays one
 * Nothing is narrowed on disk. RFC 8794 makes an unsigned integer element a
 * whole ``uint64``, libebml's ``EbmlUInteger`` stores one, and Matroska
 * constrains its UIDs with ``range: not 0`` and nothing else. A ``FileUID``
 * here is written in eight octets whatever its value -- it has to be, because
 * it is the one element rewritten in place when an object relocates, and a
 * narrower replacement would shift every sibling after it. Any conformant
 * reader gets a uint64 and must treat it as one.
 *
 * \par The value is bounded, and why that is now belt-and-braces
 * What is bounded is the number this writer *chooses*, to 2\f$^{53}\f$ - 1.
 *
 * It was load-bearing when it was introduced: a `Number` in JavaScript and a
 * `numeric` in R are both IEEE doubles, so a wider uid came back from either
 * binding as a *different number* -- one naming no object, with
 * `f.read(f.add_file(...), 0, 4)` failing on "no object with that uid".
 *
 * Both bindings have since been fixed at the binding, which is where the fix
 * belonged: JavaScript routes 64-bit scalars through `BigInt` (see
 * ext/js/jsarrays.i -- the arrays always did), and R carries a uid as a
 * character string, the only thing base R holds exactly without `bit64` (see
 * the SWIGR typemaps in ext/python/Pto.i). A container from another writer that
 * uses the whole range is therefore read correctly everywhere.
 *
 * The bound stays because it costs nothing observable -- 9x10\f$^{15}\f$
 * identities, with uniqueness inside a file guaranteed by \ref unused_uid
 * rather than left to chance -- and because a uid that fits a double is one
 * fewer thing to get wrong in the next binding.
 *
 */
std::uint64_t random_uid() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uint64_t v = 0;
    while (v == 0) v = rng() >> 11;      // 53 bits; see above
    return v;
}

/// A uid no object in `slots` already has. \see Impl::fresh_uid.
template <class Slots>
std::uint64_t unused_uid(const Slots& slots) {
    for (;;) {
        const std::uint64_t v = random_uid();
        bool taken = false;
        for (std::size_t i = 0; i < slots.size(); i++)
            if (slots[i].meta.uid == v) { taken = true; break; }
        if (!taken) return v;
    }
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

    /// One cue, plus the object it indexes. Flat rather than a map per uid:
    /// there are thousands of these at most and they are written as a flat list.
    struct CueEntry { std::uint64_t uid; PtoCue cue; };
    std::vector<CueEntry> cuepoints;

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
    std::uint64_t cues_at = 0, cues_bytes = 0;

    std::vector<std::pair<std::uint64_t, std::uint64_t> > freelist;

    /// Whether a payload is padded onto a \ref kPayloadAlign boundary. Always,
    /// except while \ref PtoFile::compact is writing a `tight` copy.
    bool align_payloads = true;

    bool fail(const std::string& why) { err = why; return false; }

    /// Fail out of \ref PtoFile::open or \ref PtoFile::create. Closing the file
    /// is the point: it drops the writer lock, so a container rejected halfway
    /// through parsing does not stay locked for the life of the process.
    bool fail_open(const std::string& why) { close_failed(); return fail(why); }
    /// The same, for a step that has already set \ref err.
    bool fail_open() { close_failed(); return false; }

    void close_failed() {
        f.close();
        writable = false;
        dirty = false;
    }

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

    /*!
     * \brief A run of `bytes` for a whole element.
     *
     * Reuses a hole where one fits -- exactly, or with room left for a Void --
     * and grows the Segment otherwise.
     *
     * Carving the front of a hole leaves a remainder that MUST be given its own
     * Void header before this returns. The hole is one Void covering the whole
     * span, and the caller is about to write its element over that header; the
     * remainder would then be the tail of the old element's bytes with nothing
     * saying so, and the next reader walking the Segment parses whatever
     * happens to be there. It reads as "an element could not be read" at an
     * offset in the middle of a healthy file, which is the *reader* reporting
     * damage that the writer did.
     */
    std::uint64_t allocate(std::uint64_t bytes) {
        for (std::size_t i = 0; i < freelist.size(); i++) {
            const std::uint64_t have = freelist[i].second;
            if (have != bytes && have < bytes + kMinVoid) continue;
            const std::uint64_t at = freelist[i].first;
            if (have == bytes) freelist.erase(freelist.begin() + i);
            else {
                freelist[i].first += bytes;
                freelist[i].second -= bytes;
                write_void(freelist[i].first, freelist[i].second);
            }
            return at;
        }
        const std::uint64_t at = seg_data + seg_bytes;
        seg_bytes += bytes;
        return at;
    }

    /*!
     * \brief \see allocate, for an element whose payload must land on a boundary.
     *
     * The padding cannot be decided before the address is, and the address
     * cannot be chosen without knowing the padding -- a hole big enough for the
     * element may not be big enough once its own alignment is paid for. So the
     * two are worked out together, per candidate, and exactly `*pad + bytes` is
     * claimed. Over-allocating a boundary's worth and giving the remainder back
     * as slack was the first attempt; it left every object trailing up to
     * sixteen bytes of Void that nothing ever wanted.
     *
     * \param align false packs it tight and leaves the payload wherever the
     *        header ends. \see PtoFile::compact.
     */
    std::uint64_t allocate_aligned(std::uint64_t bytes, std::uint64_t before_payload,
                                   bool align, std::uint64_t* pad) {
        *pad = 0;
        if (!align) return allocate(bytes);
        for (std::size_t i = 0; i < freelist.size(); i++) {
            const std::uint64_t at = freelist[i].first;
            const std::uint64_t p = align_pad(at + before_payload);
            const std::uint64_t need = p + bytes;
            const std::uint64_t have = freelist[i].second;
            if (have != need && have < need + kMinVoid) continue;
            if (have == need) freelist.erase(freelist.begin() + i);
            else {
                // \see allocate -- the remainder needs its own Void header, for
                // the same reason and with the same failure mode if it does not
                // get one.
                freelist[i].first += need;
                freelist[i].second -= need;
                write_void(freelist[i].first, freelist[i].second);
            }
            *pad = p;
            return at;
        }
        const std::uint64_t at = seg_data + seg_bytes;
        *pad = align_pad(at + before_payload);
        seg_bytes += *pad + bytes;
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

    /*!
     * \brief The cue table, as Matroska's `Cues` holding Matroska's `CuePoint`s.
     *
     * The element ID is Matroska's because the job is Matroska's: an index that
     * says where in a stream something is. What sits inside a CuePoint is ours,
     * since Matroska's cues address a timecode in a track and these address an
     * event ordinal in a payload.
     */
    Buf build_cuepoints() const {
        Buf all;
        for (std::size_t k = 0; k < cuepoints.size(); k++) {
            const CueEntry& c = cuepoints[k];
            Buf one;
            one.uint_elem(kPtoCueUID, c.uid);
            one.uint_elem(kPtoCueEvent, c.cue.event);
            one.uint_elem(kPtoCueOffset, c.cue.offset);
            if (c.cue.time != 0) one.uint_elem(kPtoCueTime, c.cue.time);
            all.master(kCuePoint, one);
        }
        Buf e;
        e.master(kCues, all);
        return e;
    }

    /// Forget an object's cues. Anything that moves or rewrites a payload has
    /// to: a cue into bytes that changed is worse than no cue at all.
    void drop_cues(std::uint64_t uid) {
        std::vector<CueEntry> keep;
        for (std::size_t k = 0; k < cuepoints.size(); k++)
            if (cuepoints[k].uid != uid) keep.push_back(cuepoints[k]);
        if (keep.size() != cuepoints.size()) { cuepoints.swap(keep); dirty = true; }
    }

    /*!
     * \brief Lay down one object: the header, then `n` payload bytes from `emit`.
     *
     * The one place an object is written. `emit` is handed the file positioned
     * at the payload and writes exactly `n` bytes -- from a caller's buffer for
     * \ref PtoFile::add, from another file for \ref PtoFile::add_file. The
     * header is written first precisely so the payload can be streamed after
     * it, and a gigabyte never goes through a buffer either way.
     *
     * \return the new object's uid, or 0.
     */
    std::uint64_t emit_object(const std::string& kind, const std::string& encoding,
                              const std::string& name, std::uint64_t n,
                              std::uint64_t reserve,
                              const std::function<bool(File&)>& emit) {
        err.clear();
        if (!writable) { fail("opened read-only"); return 0; }

        Slot s;
        s.meta.uid = unused_uid(slots);
        s.meta.kind = kind;
        s.meta.encoding = encoding;
        s.meta.name = name;
        s.meta.size = n;

        Buf head;
        head.uint_elem_fixed(kFileUID, s.meta.uid, kUidOctets);
        head.text_elem(kPtoKind, kind);
        head.text_elem(kPtoEncoding, encoding);
        if (!name.empty()) head.text_elem(kFileName, name);

        const std::uint64_t att_payload =
                head.b.size() + id_octets(kFileData) + kWideSize + n;
        const std::uint64_t att_total = id_octets(kAttachedFile) + kWideSize + att_payload;
        const std::uint64_t elem_total = id_octets(kAttachments) + kWideSize + att_total;

        if (reserve != 0 && reserve < kMinVoid) reserve = kMinVoid;

        const std::uint64_t before_payload = header_before_payload(head.b.size());
        std::uint64_t pad = 0;
        const std::uint64_t at_raw =
                allocate_aligned(elem_total + reserve, before_payload, align_payloads, &pad);
        const std::uint64_t at = at_raw + pad;
        if (pad != 0 && !write_void(at_raw, pad)) return 0;

        Buf prefix;
        prefix.put_id(kAttachments);
        prefix.put_size(att_total, kWideSize);
        prefix.put_id(kAttachedFile);
        prefix.put_size(att_payload, kWideSize);
        prefix.raw(head.b.data(), head.b.size());
        prefix.put_id(kFileData);
        prefix.put_size(n, kWideSize);
        // The arithmetic above and the bytes just built must agree, or the
        // payload lands somewhere other than where it was aligned to.
        if (prefix.b.size() != before_payload) {
            fail("internal: the object header is not the size it was computed to be");
            return 0;
        }
        s.elem_at = at;
        s.elem_bytes = elem_total;
        s.seg_size_at = at + id_octets(kAttachments);
        s.att_size_at = s.seg_size_at + kWideSize + id_octets(kAttachedFile);
        s.data_size_at = at + prefix.b.size() - kWideSize;
        s.meta.offset = at + prefix.b.size();

        if (!f.at(at, prefix.b.data(), prefix.b.size()) || !emit(f)) {
            fail(err.empty() ? "write failed" : err);
            return 0;
        }
        if (reserve != 0 && !write_void(at + elem_total, reserve)) return 0;
        s.slack_at = reserve ? at + elem_total : 0;
        s.slack_bytes = reserve;
        s.meta.capacity = n + reserve;

        slots.push_back(s);
        dirty = true;
        return s.meta.uid;
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
        struct { std::uint32_t id; std::uint64_t at; } top[4] = {
            {kInfo, info_at}, {kTags, tags_at}, {kPtoAnnotations, notes_at},
            {kCues, cues_at}};
        for (int i = 0; i < 4; i++) {
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
/// Releases the writer lock. Clearing `writable` with it keeps a later `update`
/// from taking the write path on a file that is no longer there.
void PtoFile::close() {
    p_->f.close();
    p_->writable = false;
    p_->dirty = false;
}

bool PtoFile::create(const std::string& filename, const std::string& title) {
    Impl& m = *p_;
    m.err.clear();
    File::LockResult lock = File::kLockTaken;
    if (!m.f.open_exclusive(filename, true, &lock)) {
        if (lock == File::kLockBusy)
            return m.fail(filename + " is open for writing elsewhere");
        return m.fail("cannot create " + filename);
    }
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
        // 2 since cues exist; the READ version stays 1, because a cue is an
        // element a 1.0 reader skips by size and is none the worse for missing.
        c.uint_elem(kDocTypeVersion, 2);
        c.uint_elem(kDocTypeReadVer, 1);
        head.master(kEBML, c);
    }
    if (!m.f.at(0, head.b.data(), head.b.size())) return m.fail_open("write failed");

    Buf seg;
    seg.put_id(kSegment);
    seg.put_size(0, kWideSize);
    m.seg_size_at = head.b.size() + id_octets(kSegment);
    if (!m.f.write(seg.b.data(), seg.b.size())) return m.fail_open("write failed");
    m.seg_data = m.seg_size_at + kWideSize;
    m.seg_bytes = 0;

    Buf banner;
    std::string banner_str = "pto\n"
                             "This is a .pto photon container (tttrlib PRD-020).\n"
                             "Get a reader: https://github.com/Fluorescence-Tools/tttrlib/releases\n"
                             "\n"
                             "=== HOW TO DECODE THIS BINARY (Linux & macOS / POSIX) ===\n"
                             "1. Framing: EBML Document (DocType \"pto\"). Header Magic: 0x1A45DFA3. Segment Magic: 0x18538067.\n"
                             "2. VINT Integer Decoding (1-8 bytes): First byte's leading zero count N determines VINT byte width (N+1).\n"
                             "   Mask highest 1-bit for sizes; preserve all bits for Element IDs.\n"
                             "3. Target Payload Elements:\n"
                             "   - AttachedFile (0x61A7): Container of one object.\n"
                             "   - FileUID (0x46AE): 64-bit uint object handle.\n"
                             "   - PtoKind (0x1E54F001): ASCII string (e.g. \"tttr.stream\").\n"
                             "   - FileData (0x465C): Binary payload (8-byte aligned on disk).\n"
                             "\n"
                             "=== EMBEDDED TTTR STREAM FORMAT DEFINITION (fmt) ===\n"
                             "Payload (PtoKind = \"tttr.stream\") starts with a binary header:\n"
                             "  - magic: \"PQTTTR\" or \"TTTR32\" (6 bytes ASCII)\n"
                             "  - version: Format version string (8 bytes)\n"
                             "  - record_type: uint32_t (0 = PTU T3, 1 = PTU T2, 2 = BH SPC-130)\n"
                             "  - macro_sync_rate: uint32_t (Laser sync rate in Hz)\n"
                             "  - macro_time_resolution: double (Macrotime clock period in seconds)\n"
                             "  - micro_time_resolution: double (TAC/TCSPC bin width in seconds)\n"
                             "  - number_of_records: uint64_t (Count of 32-bit photon records)\n"
                             "\n"
                             "Sequential 32-bit Record Layout (PTU T3, record_type = 0):\n"
                             "  - Bit 31     : Special event flag (1 = Special/Overflow, 0 = Photon)\n"
                             "  - Bits 30..25: Channel (6 bits, 0..63)\n"
                             "  - Bits 24..10: Microtime TAC Bins (15 bits, 0..32767)\n"
                             "  - Bits 9..0  : Macrotime Sync Ticks (10 bits, 0..1023)\n"
                             "  - Overflow Rule: If Special == 1 and Channel == 63, macro_offset += 1024.\n"
                             "  - Photon Time: (macro_offset + macrotime) * macro_time_resolution.\n"
                             "\n"
                             "=== ASCII C99 DECODER PSEUDOCODE ===\n"
                             "size_t read_vint(const uint8_t *b, uint64_t *v, int mask) {\n"
                             "    int n = 1; uint8_t m = 0x80;\n"
                             "    while ((b[0] & m) == 0) { m >>= 1; n++; }\n"
                             "    *v = mask ? (b[0] & ~m) : b[0];\n"
                             "    for (int i = 1; i < n; i++) *v = (*v << 8) | b[i];\n"
                             "    return n;\n"
                             "}\n"
                             "/* Walk Segment -> AttachedFile (0x61A7) -> FileData (0x465C) */\n";
    banner.text_elem(kPtoBanner, banner_str);
    if (!m.f.write(banner.b.data(), banner.b.size())) return m.fail_open("write failed");
    m.seg_bytes += banner.b.size();

    // The two indexes come first, each with room to be rewritten where it lies.
    const std::uint64_t total = id_octets(kSeekHead) + kWideSize + kSeekHeadReserve;
    m.head_total = total;
    for (int i = 0; i < 2; i++) {
        m.head_at[i] = m.seg_data + m.seg_bytes;
        m.seg_bytes += total;
        Buf e;
        e.put_id(kSeekHead);
        e.put_size(kSeekHeadReserve, kWideSize);
        if (!m.f.at(m.head_at[i], e.b.data(), e.b.size())) return m.fail_open("write failed");
        if (!m.write_void(m.head_at[i] + e.b.size(), kSeekHeadReserve)) return m.fail_open();
    }
    // An empty but valid index, rather than a full commit: committing here
    // would place an Info element before the caller has set anything, and the
    // next commit would then have to move it and leave a hole behind. A file
    // that was just created should not already need compacting.
    if (!m.write_seekhead(0, 1)) return m.fail_open();
    m.live = 0;
    m.generation = 1;
    if (!m.patch_segment_size()) return m.fail_open("write failed");
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

    // A Data Size is read out of the file, so it is whatever the file says --
    // including, in a corrupted or hostile one, a number far larger than the
    // file itself. Everything sized from it has to be bounded by what the file
    // can actually contain before a byte of it is believed: allocating first
    // and discovering the truncation on the read is how a one-byte corruption
    // becomes an out-of-memory kill rather than "this file is damaged".
    //
    // This is the rule libebml carries SafeReadIOCallback for, applied at the
    // only place PTO sizes an allocation from untrusted input.
    const std::uint64_t end = f.length();
    if (n > end || at > end - n || at + len + slen + n > end) return false;

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
    if (writable) {
        // A writer takes the container; a reader never does. A viewer open
        // while an analysis writes is the normal case, and the format already
        // has the reader seeing the pre-commit state.
        File::LockResult lock = File::kLockTaken;
        if (!m.f.open_exclusive(filename, false, &lock)) {
            if (lock == File::kLockBusy)
                return m.fail(filename + " is open for writing elsewhere");
            return m.fail("cannot open " + filename);
        }
    } else if (!m.f.open(filename, "rb")) {
        return m.fail("cannot open " + filename);
    }
    m.path = filename;
    m.writable = writable;
    m.slots.clear();
    m.tags.clear();
    m.notes.clear();
    m.freelist.clear();
    m.cuepoints.clear();
    m.info_at = m.tags_at = m.notes_at = m.cues_at = 0;
    m.info_bytes = m.tags_bytes = m.notes_bytes = m.cues_bytes = 0;

    // EBML header, and the DocType that says this is ours.
    // PRD-025 Part 6: Skip up to 4 leading non-EBML elements / 2 MB prefix.
    // Check offset 0 first, then scan for PTO_ID_EBML in Cosmopolitan APE binary prefix
    std::uint64_t ebml_offset = 0;
    std::uint32_t id = 0;
    std::vector<unsigned char> payload;
    std::uint64_t total = 0;
    bool found_ebml = false;

    if (read_element(m.f, 0, &id, &payload, &total, nullptr) && id == kEBML) {
        found_ebml = true;
        ebml_offset = 0;
    } else if (m.f.length() >= 12624 && read_element(m.f, 12624, &id, &payload, &total, nullptr) && id == kEBML) {
        found_ebml = true;
        ebml_offset = 12624;
    } else {
        std::uint64_t max_bytes = m.f.length() > 8388608 ? 8388608 : m.f.length();
        for (std::uint64_t off = 8; off + 4 <= max_bytes; off += 8) {
            if (read_element(m.f, off, &id, &payload, &total, nullptr) && id == kEBML) {
                found_ebml = true;
                ebml_offset = off;
                break;
            }
        }
        if (!found_ebml) {
            for (std::uint64_t off = 1; off + 4 <= max_bytes; off++) {
                if (read_element(m.f, off, &id, &payload, &total, nullptr) && id == kEBML) {
                    found_ebml = true;
                    ebml_offset = off;
                    break;
                }
            }
        }
    }

    if (!found_ebml)
        return m.fail_open(filename + " is not an EBML file");
    {
        Cursor c{payload.data(), payload.size(), 0};
        std::uint32_t cid;
        const unsigned char* d;
        std::uint64_t n;
        std::string doctype;
        std::uint64_t read_version = 1;
        std::uint64_t max_id = 4;
        std::uint64_t max_size = 8;
        while (c.element(&cid, &d, &n)) {
            if (cid == kDocType) doctype = get_text(d, n);
            else if (cid == kDocTypeReadVer) read_version = get_uint(d, n);
            else if (cid == kEBMLMaxIDLength) max_id = get_uint(d, n);
            else if (cid == kEBMLMaxSizeLen) max_size = get_uint(d, n);
        }
        if (doctype != "pto") return m.fail_open(filename + " is not a PTO file");
        if (read_version > 1)
            return m.fail_open(filename + " needs a newer PTO reader (DocTypeReadVersion "
                          + std::to_string(read_version) + ")");
        if (max_id > 4 || max_size > 8)
            return m.fail_open(filename + " declares EBMLMaxIDLength " +
                          std::to_string(max_id) + " / EBMLMaxSizeLength " +
                          std::to_string(max_size) + ", wider than this reader parses");
    }

    std::uint64_t seg_at = ebml_offset + total;
    if (!read_element(m.f, seg_at, &id, nullptr, &total, &m.seg_size_at) ||
        id != kSegment)
        return m.fail_open(filename + " has no Segment");
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
            return m.fail_open(filename + " is damaged: an element at " +
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
    if (found < 2) return m.fail_open(filename + " has no index");
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
        } else if (c.id == kCues) {
            m.cues_at = c.at; m.cues_bytes = c.total;
            while (in.element(&eid, &ed, &en)) {
                if (eid != kCuePoint) continue;
                Impl::CueEntry e;
                e.uid = 0;
                Cursor cc{ed, static_cast<std::size_t>(en), 0};
                std::uint32_t xid;
                const unsigned char* xd;
                std::uint64_t xn;
                while (cc.element(&xid, &xd, &xn)) {
                    switch (xid) {
                        case kPtoCueUID: e.uid = get_uint(xd, xn); break;
                        case kPtoCueEvent: e.cue.event = get_uint(xd, xn); break;
                        case kPtoCueOffset: e.cue.offset = get_uint(xd, xn); break;
                        case kPtoCueTime: e.cue.time = get_uint(xd, xn); break;
                        default: break;
                    }
                }
                if (e.uid != 0) m.cuepoints.push_back(e);
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
    return p_->emit_object(kind, encoding, name, n, reserve,
                           [data, n](File& f) { return f.write(data, n); });
}

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

std::uint64_t PtoFile::add_file(const std::string& kind, const std::string& encoding,
                                const std::string& name, const std::string& path,
                                std::uint64_t reserve) {
    Impl& m = *p_;
    m.err.clear();

    std::error_code ec;
    const std::uintmax_t n =
            std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if (ec) { m.fail("cannot size " + path + ": " + ec.message()); return 0; }

    File in;
    if (!in.open(path, "rb")) { m.fail("cannot open " + path); return 0; }

    // The only difference from add: where the bytes come from. In blocks, so
    // embedding a four-gigabyte instrument file costs a megabyte of memory.
    return m.emit_object(kind, encoding, name, n, reserve, [&](File& out) {
        std::vector<unsigned char> chunk(1u << 20);
        std::uint64_t left = n;
        while (left > 0) {
            const std::size_t take =
                    static_cast<std::size_t>(left < chunk.size() ? left : chunk.size());
            if (!in.read(chunk.data(), take)) return m.fail("could not read " + path);
            if (!out.write(chunk.data(), take)) return m.fail("write failed");
            left -= take;
        }
        return true;
    });
}

bool PtoFile::update(std::uint64_t uid, const unsigned char* data, std::size_t n) {
    Impl& m = *p_;
    m.err.clear();
    if (!m.writable) return m.fail("opened read-only");
    Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");
    // Whatever the old cues pointed at is about to stop being there.
    m.drop_cues(uid);

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
    u.uint_elem_fixed(kFileUID, keep_uid, kUidOctets);
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
        m.drop_cues(uid);
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

std::size_t PtoFile::read_at(std::uint64_t uid, std::uint64_t at,
                             void* into, std::size_t n) const {
    Impl& m = *p_;
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr || at >= s->meta.size || n == 0) return 0;
    // Short at the end rather than an error, because that is what a read of a
    // file does and a payload is a file that happens to live inside another.
    const std::uint64_t left = s->meta.size - at;
    const std::size_t take = n < left ? n : static_cast<std::size_t>(left);
    m.f.flush();
    if (!m.f.seek(s->meta.offset + at) || !m.f.read(into, take)) return 0;
    return take;
}

bool PtoFile::stream(std::uint64_t uid,
                     const std::function<bool(const void*, std::size_t)>& sink) const {
    Impl& m = *p_;
    m.err.clear();
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr) return m.fail("no object with that uid");
    m.f.flush();
    if (!m.f.seek(s->meta.offset)) return m.fail("cannot reach the payload");

    // In blocks, so an eight-gigabyte stream costs a megabyte of memory
    // whatever the sink does with it.
    std::vector<unsigned char> chunk(1u << 20);
    std::uint64_t left = s->meta.size;
    while (left > 0) {
        const std::size_t take =
                static_cast<std::size_t>(left < chunk.size() ? left : chunk.size());
        if (!m.f.read(chunk.data(), take))
            return m.fail("could not read the payload of " + std::to_string(uid));
        if (!sink(chunk.data(), take)) return false;
        left -= take;
    }
    return true;
}

std::vector<unsigned char> PtoFile::read(std::uint64_t uid, std::uint64_t at,
                                         std::size_t n) const {
    if (p_->find(uid) == nullptr) throw std::runtime_error("no object with that uid");
    std::vector<unsigned char> out(n);
    out.resize(read_at(uid, at, out.empty() ? nullptr : out.data(), n));
    return out;
}

bool PtoFile::extract(std::uint64_t uid, const std::string& filename) const {
    Impl& m = *p_;
    m.err.clear();
    if (m.find(uid) == nullptr) return m.fail("no object with that uid");

    File out;
    if (!out.open(filename, "wb")) return m.fail("cannot create " + filename);
    return stream(uid, [&](const void* block, std::size_t n) {
        if (out.write(block, n)) return true;
        return m.fail("could not copy the payload of " + std::to_string(uid));
    });
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

std::vector<PtoCue> PtoFile::cues(std::uint64_t uid) const {
    std::vector<PtoCue> out;
    for (std::size_t i = 0; i < p_->cuepoints.size(); i++)
        if (p_->cuepoints[i].uid == uid) out.push_back(p_->cuepoints[i].cue);
    std::sort(out.begin(), out.end(),
              [](const PtoCue& a, const PtoCue& b) { return a.event < b.event; });
    return out;
}

void PtoFile::clear_cues(std::uint64_t uid) { p_->drop_cues(uid); }

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
    Buf cues = m.build_cuepoints();
    if (!m.cuepoints.empty() || m.cues_at != 0)
        if (!m.place(cues, &m.cues_at, &m.cues_bytes)) return false;

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

bool PtoFile::compact(const std::string& to, bool tight, double reserve) {
    Impl& m = *p_;
    m.err.clear();
    if (reserve < 0.0) return m.fail("a negative reserve is not a fraction");
    PtoFile out;
    if (!out.create(to, m.title)) return m.fail(out.error());
    out.set_writing_app(m.writing_app);
    out.p_->align_payloads = !tight;
    for (std::size_t i = 0; i < m.slots.size(); i++) {
        const PtoObject& o = m.slots[i].meta;
        const std::uint64_t room =
                static_cast<std::uint64_t>(static_cast<double>(o.size) * reserve);
        // Streamed, not read: this is the one operation that touches every
        // payload in the file, so materialising them would make compacting an
        // eight-gigabyte container need eight gigabytes of memory -- for the
        // job whose entire purpose is to make the file smaller.
        bool copied = true;
        const std::uint64_t made = out.p_->emit_object(
                o.kind, o.encoding, o.name, o.size, room,
                [&](File& dst) {
                    copied = stream(o.uid, [&](const void* block, std::size_t n) {
                        return dst.write(block, n);
                    });
                    return copied;
                });
        // A failed copy already left its reason in this file's error, since
        // `stream` reads from here; a failed write left it in the new one's.
        if (made == 0) return copied ? m.fail(out.error()) : false;
        // Keep the identity: everything that refers to this object refers to it
        // by uid, and compaction is not supposed to be observable.
        Impl::Slot* s = out.p_->find(made);
        s->meta.uid = o.uid;
        s->meta.rows = o.rows;
        Buf u;
        u.uint_elem_fixed(kFileUID, o.uid, kUidOctets);
        if (!out.p_->f.at(s->att_size_at + kWideSize, u.b.data(), u.b.size()))
            return m.fail("write failed");
    }
    out.set_tags(m.tags);
    for (std::size_t i = 0; i < m.notes.size(); i++) out.add_annotation(m.notes[i]);
    // Cues survive: they address a byte offset INTO a payload, and compaction
    // moves payloads without changing one of them.
    out.p_->cuepoints = m.cuepoints;
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
    s.meta.uid = unused_uid(m.slots);
    s.meta.kind = kind;
    s.meta.encoding = "dstore";
    s.meta.name = name;
    s.meta.rows = store.n_rows();

    Buf head;
    head.uint_elem_fixed(kFileUID, s.meta.uid, kUidOctets);
    head.text_elem(kPtoKind, kind);
    head.text_elem(kPtoEncoding, "dstore");
    if (!name.empty()) head.text_elem(kFileName, name);
    head.text_elem(kFileMedia, "application/x-dstore");
    if (s.meta.rows != 0) head.uint_elem(kPtoRowCount, s.meta.rows);

    // The size is not known until the store has been written, so this always
    // appends -- there is no hole to look for one that fits. The three sizes
    // are written wide and patched afterwards, which is what RFC 8794 permits
    // an over-wide Data Size for.
    //
    // The payload is aligned the same way \ref PtoFile::emit_object aligns one,
    // and it matters more here than anywhere: a .dstore's own blob offsets are
    // 8-aligned RELATIVE TO THE STORE, so they are only 8-aligned in the file
    // if the store itself begins on a boundary.
    const std::uint64_t before_payload = header_before_payload(head.b.size());
    const std::uint64_t at_raw = m.seg_data + m.seg_bytes;
    const std::uint64_t pad =
            m.align_payloads ? align_pad(at_raw + before_payload) : 0;
    if (pad != 0) {
        if (!m.write_void(at_raw, pad)) return 0;
        m.seg_bytes += pad;
    }
    const std::uint64_t at = at_raw + pad;

    Buf prefix;
    prefix.put_id(kAttachments);
    prefix.put_size(0, kWideSize);
    prefix.put_id(kAttachedFile);
    prefix.put_size(0, kWideSize);
    prefix.raw(head.b.data(), head.b.size());
    prefix.put_id(kFileData);
    prefix.put_size(0, kWideSize);
    if (prefix.b.size() != before_payload) {
        m.fail("internal: the object header is not the size it was computed to be");
        return 0;
    }

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

PtoObject pto_store_region(const PtoFile& file, std::uint64_t uid) {
    const PtoFile::Impl& m = *file.p_;
    const PtoFile::Impl::Slot* s = m.find(uid);
    if (s == nullptr) throw std::runtime_error("no object with that uid");
    if (s->meta.encoding != "dstore")
        throw std::runtime_error("object " + std::to_string(uid) + " is encoded as '" +
                                 s->meta.encoding + "', not 'dstore'");
    // Not incidental: a store added in this session may still be in the stdio
    // buffer, and the store reader opens the path again rather than sharing
    // this handle.
    const_cast<File&>(m.f).flush();
    return s->meta;
}

void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out) {
    const PtoObject o = pto_store_region(file, uid);
    read_store_into(out, file.filename(), o.offset, o.size);
}

void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out,
                    const std::vector<std::string>& columns) {
    const PtoObject o = pto_store_region(file, uid);
    read_store_into(out, file.filename(), o.offset, o.size, columns);
}

void pto_read_store(const PtoFile& file, std::uint64_t uid, data::DataStore& out,
                    const std::vector<std::string>& columns,
                    std::uint64_t first_row, std::uint64_t n_rows) {
    const PtoObject o = pto_store_region(file, uid);
    read_store_into(out, file.filename(), o.offset, o.size, columns, first_row, n_rows);
}

std::vector<std::string> pto_store_columns(const PtoFile& file, std::uint64_t uid,
                                           const std::string& group) {
    try {
        const PtoObject o = pto_store_region(file, uid);
        return store_columns(file.filename(), o.offset, o.size, group);
    } catch (const std::exception&) {
        return std::vector<std::string>();
    }
}

std::vector<std::string> pto_store_groups(const PtoFile& file, std::uint64_t uid) {
    try {
        const PtoObject o = pto_store_region(file, uid);
        return store_groups(file.filename(), o.offset, o.size);
    } catch (const std::exception&) {
        return std::vector<std::string>();
    }
}

// --- probing --------------------------------------------------------------------

bool is_pto_file(const std::string& filename) {
    File f;
    if (!f.open(filename, "rb")) return false;

    auto check_at = [&](std::uint64_t off) -> bool {
        std::uint32_t id = 0;
        std::vector<unsigned char> payload;
        std::uint64_t total = 0;
        if (!read_element(f, off, &id, &payload, &total, nullptr)) return false;
        if (id == kEBML) {
            Cursor c{payload.data(), payload.size(), 0};
            std::uint32_t cid;
            const unsigned char* d;
            std::uint64_t n;
            while (c.element(&cid, &d, &n))
                if (cid == kDocType) return get_text(d, n) == "pto";
        }
        return false;
    };

    if (check_at(0)) return true;
    if (f.length() >= 12624 && check_at(12624)) return true;
    std::uint64_t max_bytes = f.length() > 8388608 ? 8388608 : f.length();
    for (std::uint64_t off = 8; off + 4 <= max_bytes; off += 8) {
        if (check_at(off)) return true;
    }
    for (std::uint64_t off = 1; off + 4 <= max_bytes; off++) {
        if (check_at(off)) return true;
    }

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

}  // namespace

// --- cues, and positioning in a photon stream ---------------------------------

std::uint64_t PtoFile::build_cues(std::uint64_t uid, std::uint64_t every_n_events) {
    Impl& m = *p_;
    m.err.clear();
    const Impl::Slot* s = m.find(uid);
    if (s == nullptr) { m.fail("no object with that uid"); return 0; }
    if (every_n_events == 0) { m.fail("a cue spacing of zero indexes nothing"); return 0; }
    const int container = container_for(s->meta.encoding);
    if (container < 0) {
        m.fail("object " + std::to_string(uid) + " is encoded as '" + s->meta.encoding +
               "', which is not a record stream this build can index");
        return 0;
    }
    const std::uint64_t payload_at = s->meta.offset, payload_bytes = s->meta.size;
    m.f.flush();

    // The header says how wide a record is and how to decode one; nothing else
    // is read here, and no events are materialised.
    TTTR probe;
    if (!probe.open_embedded(m.path.c_str(), container, payload_at, payload_bytes,
                             sidecar_text(*this, uid))) {
        m.fail("could not read the header of object " + std::to_string(uid));
        return 0;
    }
    const std::size_t width = probe.get_header()->get_bytes_per_record();
    const std::uint64_t records_at = probe.get_records_begin();
    const std::uint64_t n_records = probe.n_records_in_file;
    const int record_type = probe.get_tttr_record_type();
    if (width == 0 || n_records == 0) { m.fail("the object holds no records"); return 0; }

    File in;
    if (!in.open(m.path, "rb") || !in.seek(records_at))
        { m.fail("cannot reach the records of " + std::to_string(uid)); return 0; }

    const std::size_t kChunk = 1u << 16;
    std::vector<signed char> raw(kChunk * width);
    std::vector<unsigned long long> macro(kChunk);
    std::vector<unsigned short> micro(kChunk);
    std::vector<signed char> chan(kChunk), type(kChunk);

    std::vector<Impl::CueEntry> made;
    std::uint64_t overflow = 0, events = 0, next_cue = 0, done = 0;
    while (done < n_records) {
        const std::size_t take =
                static_cast<std::size_t>(n_records - done < kChunk ? n_records - done : kChunk);
        if (!in.read(raw.data(), take * width)) break;

        // Whole chunk first: if no cue falls in it, the per-record walk below is
        // wasted work, and on a 10^9-event stream at one cue per 10^6 that is
        // the overwhelming majority of chunks.
        const std::uint64_t overflow_before = overflow;
        std::size_t valid = 0;
        if (!dispatch_process_records_batch(record_type, raw.data(), take, width,
                                            overflow, macro.data(), micro.data(),
                                            chan.data(), type.data(), valid)) {
            m.fail("this build cannot decode record type " + std::to_string(record_type));
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
                Impl::CueEntry e;
                e.uid = uid;
                e.cue.event = events;
                e.cue.offset = records_at + (done + r) * width - payload_at;
                e.cue.time = macro[0];
                made.push_back(e);
                next_cue += every_n_events;
            }
            events++;
        }
        done += take;
    }

    m.drop_cues(uid);
    for (std::size_t i = 0; i < made.size(); i++) m.cuepoints.push_back(made[i]);
    m.dirty = true;
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

namespace {

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
        f.can_write = false;
        IORegistry::add(f);
        IORegistry::set_reader("PTO", &read_pto_into_tttr, nullptr);
    }
};
const RegisterPto register_pto;

}  // namespace

}  // namespace io
}  // namespace tttrlib
