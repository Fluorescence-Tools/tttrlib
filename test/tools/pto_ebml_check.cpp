// SPDX-License-Identifier: BSD-3-Clause
//
// Validate a .pto with libebml, and with nothing of ours.
//
// Every other check on the container's framing is written against tttrlib's own
// parser, which means a consistent misreading of RFC 8794 would pass all of
// them. This walks the same file with the reference implementation -- the one
// that has carried multi-gigabyte Matroska documents for twenty years -- and
// agrees or does not.
//
// It is a separate program rather than a test-suite dependency on purpose: PTO
// deliberately writes its own EBML and tttrlib must not link libebml. See
// test/tools/README.md for how to build and run it.
//
//   pto_ebml_check <file.pto> [--verbose] [--aligned]
//
// Exit 0 if the document parses, its DocType is "pto", and every element is
// self-describing, in range, and exactly fills its parent.
//
// Payload alignment is REPORTED but does not fail the run unless --aligned is
// given. The specification makes it a writer SHOULD, and a container written
// with `compact(tight=True)` drops it deliberately to squeeze out the padding;
// failing that file would be calling a conformant one broken.

#include <ebml/EbmlHead.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlMaster.h>
#include <ebml/EbmlBinary.h>
#include <ebml/EbmlString.h>
#include <ebml/EbmlUInteger.h>
#include <ebml/EbmlVoid.h>
#include <ebml/StdIOCallback.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace libebml;

namespace {

const std::uint32_t kSegment      = 0x18538067;
const std::uint32_t kAttachments  = 0x1941A469;
const std::uint32_t kAttachedFile = 0x61A7;
const std::uint32_t kFileData     = 0x465C;
const std::uint32_t kCues         = 0x1C53BB6B;
const std::uint32_t kSeekHead     = 0x114D9B74;

bool g_verbose = false;
bool g_require_aligned = false;
int g_problems = 0;

void problem(const std::string& what) {
    std::cerr << "  NOT OK: " << what << std::endl;
    g_problems++;
}

/// The element's ID as the plain number the specification writes it as.
std::uint32_t id_of(const EbmlElement& e) {
    return static_cast<std::uint32_t>(EbmlId(e).GetValue());
}

/*!
 * \brief Walk the byte range [`from`, `to`) using libebml's own VINT decoding.
 *
 * Not `EbmlStream::FindNextElement`, and the reason is worth stating: that
 * function needs a *semantic context*, and given one that does not list an ID
 * it scans forward a byte at a time looking for something it recognises. That
 * is the right behaviour for recovering a damaged Matroska stream and the wrong
 * instrument here -- it resynchronises instead of failing, so it would report
 * a plausible-looking tree for a file with no valid framing at all.
 *
 * What a generic parser actually is, is `EbmlId::FromBuffer` plus
 * `ReadCodedSizeValue`: the two functions that turn octets into an ID and a
 * Data Size. Both are libebml's, both are public, and between them they are the
 * whole of what PTO claims a reader needs. Everything below rides on them and
 * on nothing of tttrlib's.
 */
void walk(IOCallback& file, std::uint64_t from, std::uint64_t to, int depth,
          std::vector<std::uint64_t>* payload_offsets) {
    std::uint64_t at = from;
    while (at < to) {
        file.setFilePointer(static_cast<std::int64_t>(at), seek_beginning);

        // The ID: its width is the position of the first set bit of octet one.
        binary raw[8] = {0};
        if (file.read(raw, 1) != 1) { problem("truncated at " + std::to_string(at)); return; }
        unsigned id_len = 1;
        while (id_len <= 4 && !(raw[0] & (0x80 >> (id_len - 1)))) id_len++;
        if (id_len > 4) {
            problem("no valid element ID at " + std::to_string(at));
            return;
        }
        if (id_len > 1 && file.read(raw + 1, id_len - 1) != id_len - 1) {
            problem("truncated ID at " + std::to_string(at));
            return;
        }
        const std::uint32_t id = EbmlId::FromBuffer(raw, id_len);

        // The Data Size, decoded by libebml.
        binary size_raw[8] = {0};
        if (file.read(size_raw, 1) != 1) { problem("truncated size at " + std::to_string(at)); return; }
        unsigned size_len = 1;
        while (size_len <= 8 && !(size_raw[0] & (0x80 >> (size_len - 1)))) size_len++;
        if (size_len > 8) { problem("no valid Data Size at " + std::to_string(at)); return; }
        if (size_len > 1 && file.read(size_raw + 1, size_len - 1) != size_len - 1) {
            problem("truncated Data Size at " + std::to_string(at));
            return;
        }
        std::uint32_t width = size_len;
        std::uint64_t unknown = 0;
        const std::uint64_t size = ReadCodedSizeValue(size_raw, width, unknown);
        if (width == 0) { problem("libebml refused the Data Size at " + std::to_string(at)); return; }

        const std::uint64_t data_at = at + id_len + size_len;
        if (g_verbose) {
            std::cout << std::string(depth * 2, ' ') << "0x" << std::hex << id
                      << std::dec << "  at " << at << "  data " << data_at
                      << "  size " << size << std::endl;
        }
        // An unknown size is legal EBML for a live stream and is the one thing
        // PTO promises never to write: every size is known, so a reader that
        // understands nothing can still skip by it.
        if (unknown != 0 && size == unknown)
            problem("an element at " + std::to_string(at) + " has an unknown size");
        if (data_at + size > to)
            problem("an element at " + std::to_string(at) + " runs past its parent");

        if (id == kFileData) payload_offsets->push_back(data_at);
        if (id == kSegment || id == kAttachments || id == kAttachedFile ||
            id == kCues || id == kSeekHead)
            walk(file, data_at, data_at + size, depth + 1, payload_offsets);

        const std::uint64_t next = data_at + size;
        if (next <= at) { problem("no forward progress at " + std::to_string(at)); return; }
        at = next;
    }
    if (at != to)
        problem("the last element in [" + std::to_string(from) + ", " +
                std::to_string(to) + ") ends at " + std::to_string(at));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: pto_ebml_check <file.pto> [--verbose]" << std::endl;
        return 2;
    }
    const std::string path = argv[1];
    for (int i = 2; i < argc; i++) {
        const std::string flag = argv[i];
        if (flag == "--verbose") g_verbose = true;
        else if (flag == "--aligned") g_require_aligned = true;
        else { std::cerr << "unknown option " << flag << std::endl; return 2; }
    }

    try {
        StdIOCallback file(path.c_str(), MODE_READ);
        EbmlStream stream(file);

        // The EBML header, read as a header and not as bytes: this is where a
        // DocType that is not "pto", or a Max*Length this parser cannot
        // satisfy, is caught by somebody else's code.
        EbmlElement* head = stream.FindNextID(EBML_INFO(EbmlHead), 0xFFFFFFFFL);
        if (head == nullptr) {
            std::cerr << "  NOT OK: no EBML header" << std::endl;
            return 1;
        }
        int upper = 0;
        head->Read(stream, EBML_CONTEXT(head), upper, head, true);

        EbmlMaster* header = static_cast<EbmlMaster*>(head);
        std::string doctype;
        std::uint64_t doctype_version = 0, read_version = 0, max_id = 4, max_size = 8;
        for (unsigned i = 0; i < header->ListSize(); i++) {
            EbmlElement* e = (*header)[i];
            if (EbmlId(*e) == EBML_ID(EDocType))
                doctype = static_cast<EDocType*>(e)->GetValue();
            else if (EbmlId(*e) == EBML_ID(EDocTypeVersion))
                doctype_version = static_cast<EDocTypeVersion*>(e)->GetValue();
            else if (EbmlId(*e) == EBML_ID(EDocTypeReadVersion))
                read_version = static_cast<EDocTypeReadVersion*>(e)->GetValue();
            else if (EbmlId(*e) == EBML_ID(EMaxIdLength))
                max_id = static_cast<EMaxIdLength*>(e)->GetValue();
            else if (EbmlId(*e) == EBML_ID(EMaxSizeLength))
                max_size = static_cast<EMaxSizeLength*>(e)->GetValue();
        }
        std::cout << "  DocType " << doctype << ", version " << doctype_version
                  << ", read version " << read_version
                  << ", MaxIDLength " << max_id
                  << ", MaxSizeLength " << max_size << std::endl;
        if (doctype != "pto") problem("DocType is '" + doctype + "', not 'pto'");
        if (read_version != 1)
            problem("DocTypeReadVersion is " + std::to_string(read_version) +
                    ", so a 1.0 reader would refuse the file");
        delete head;

        // The whole file, as libebml's own IOCallback measures it.
        file.setFilePointer(0, seek_end);
        const std::uint64_t file_bytes = file.getFilePointer();
        file.setFilePointer(0, seek_beginning);

        std::vector<std::uint64_t> payloads;
        walk(file, 0, file_bytes, 0, &payloads);

        std::size_t unaligned = 0;
        for (std::size_t i = 0; i < payloads.size(); i++)
            if (payloads[i] % 8 != 0) unaligned++;
        std::cout << "  " << payloads.size() << " payload(s), " << unaligned
                  << " not 8-byte aligned" << std::endl;
        if (unaligned != 0 && g_require_aligned) {
            for (std::size_t i = 0; i < payloads.size(); i++)
                if (payloads[i] % 8 != 0)
                    problem("FileData at " + std::to_string(payloads[i]) +
                            " is not 8-byte aligned");
        }
        if (payloads.empty()) problem("no FileData found at all");
    } catch (const std::exception& e) {
        std::cerr << "  NOT OK: libebml refused the file: " << e.what() << std::endl;
        return 1;
    }

    if (g_problems != 0) {
        std::cerr << g_problems << " problem(s)" << std::endl;
        return 1;
    }
    std::cout << "  OK: libebml walked it" << std::endl;
    return 0;
}
