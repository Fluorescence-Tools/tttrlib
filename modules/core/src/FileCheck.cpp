// SPDX-License-Identifier: BSD-3-Clause
#include "FileCheck.h"
#include "TTTRFormat.h"
#include "PluginHost.h"
#include "io_fl.h"

#include <mutex>


// ---------------------- Utility / File type ----------------------

std::vector<std::string> get_supported_filetypes() {
    return {
        "ptu",   // PicoQuant Unified TTTR
        "ht3",   // PicoQuant HydraHarp T3
        "pt3",   // PicoQuant PicoHarp T3
        "spc",   // Becker & Hickl FIFO
        "sm",    // Single Molecule format
        "h5",    // HDF5 format
        "hdf5",  // HDF5 format (alternative extension)
        "raw",   // Carl Zeiss Confocor3 raw data
        "photons" // Photonscore LINCam D7 container
    };
}

// Function to check if the file is a Photonscore ".photons" (D7) file
bool isPhotonsFile(const std::string& filename) {
    char buf[64] = {};
    FILE* file = open_file(filename, "rb");
    if (!file) return false;
    std::rewind(file);
    size_t read_size = std::fread(buf, 1, sizeof(buf), file);
    std::fclose(file);
    // The "D7 Photons Data" signature appears at the start of the header, after
    // the 2-byte block header of the first page.
    std::string head(buf, read_size);
    return head.find("D7 Photons Data") != std::string::npos;
}

// HDF5 file signature: "\x89HDF\r\n\x1A\n"
bool isHDF5File(const std::string& filename) {
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    const std::array<unsigned char, 8> hdf5_signature = {0x89, 'H', 'D', 'F', 0x0D, 0x0A, 0x1A, 0x0A};
    std::array<unsigned char, 8> file_signature{};

    std::rewind(file);
    size_t read_size = std::fread(file_signature.data(), 1, file_signature.size(), file);
    std::fclose(file);

    if (read_size != file_signature.size()) return false;
    return file_signature == hdf5_signature;
}

// Function to check if the file is an SM file
bool isSMFile(const std::string& filename) {
    // The SM header is BIG-endian and begins with a uint32 version of 2,
    // followed by two length-prefixed strings ("comment", then "simple").
    //
    // This used to read a native-endian uint64 and compare it to 2, which is
    // wrong twice over: the field is 32 bits, and the file is big-endian. On a
    // little-endian machine the first eight bytes of a real SM file read as
    // 33554432, so the predicate rejected every genuine .sm file -- which is
    // why detection accepted ".sm" on the extension alone and never called it.
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    auto read_be32 = [&](uint32_t& out) -> bool {
        unsigned char b[4];
        if (std::fread(b, 1, 4, file) != 4) return false;
        out = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
              (uint32_t(b[2]) << 8)  |  uint32_t(b[3]);
        return true;
    };

    // A counted string: a big-endian length, then that many bytes, which must
    // be printable. Two of them in a row is what makes this a real check rather
    // than a one-word coincidence.
    auto skip_counted_string = [&](bool require_printable) -> bool {
        uint32_t n = 0;
        if (!read_be32(n)) return false;
        if (n > 4096) return false;                 // implausible for a header field
        for (uint32_t i = 0; i < n; ++i) {
            const int c = std::fgetc(file);
            if (c == EOF) return false;
            if (require_printable && (c < 0x20 || c > 0x7E)) return false;
        }
        return true;
    };

    std::rewind(file);
    uint32_t version = 0;
    const bool ok = read_be32(version) && version == 2 &&
                    skip_counted_string(false) &&   // comment, often empty
                    skip_counted_string(true);      // e.g. "Simple"
    std::fclose(file);
    return ok;
}

// Function to check if the file is a PTU file
bool isPTUFile(const std::string& filename) {
    char Magic[8] = {};
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    std::rewind(file);
    size_t read_size = std::fread(Magic, 1, sizeof(Magic), file);

    if (read_size != sizeof(Magic)) {   // too short to be a PTU: not an error
        std::fclose(file);
        return false;
    }

    bool ok = (std::strncmp(Magic, "PQTTTR", 6) == 0);
    std::fclose(file);
    return ok;
}

// Function to check if the file is an HT3 file
bool isHT3File(const std::string& filename) {
    pq_ht3_Header_t ht3_header_begin{};
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    std::rewind(file);
    size_t read_size = std::fread(&ht3_header_begin, 1, sizeof(ht3_header_begin), file);
    std::fclose(file);

    if (read_size != sizeof(ht3_header_begin)) return false;
    // HydraHarp 1 files carry FormatVersion "1.0"; HydraHarp 2 (and the
    // HHT3v2 record stream) carry "2.0". Both are valid HT3 containers.
    return (std::strncmp(ht3_header_begin.FormatVersion, "1.0", 3) == 0) ||
           (std::strncmp(ht3_header_begin.FormatVersion, "2.0", 3) == 0);
}

// Function to check if the file is a BH132 file
bool isBH132File(const std::string& filename) {
    bh_spc132_header_t rec{};
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    std::rewind(file);
    size_t read_size = std::fread(&rec, sizeof(rec), 1, file);
    std::fclose(file);

    if (read_size != 1) return false;

    double time_res = static_cast<double>(rec.bits.macro_time_clock);
    return (time_res > 0.0 && time_res < 500.0);
}

// Function to check if the file is a Becker & Hickl SPC-QC file
bool isBHSPCQCFile(const std::string& filename) {
    bh_spcqc_header_t head{};
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    std::rewind(file);
    if (std::fread(&head, sizeof(head), 1, file) != 1) {
        std::fclose(file);
        return false;
    }

    // "QC .spc files are always raw (not processed)", so the raw flag is set on
    // every one of them. A clock of zero is not a QC file either.
    if (!head.bits.raw || head.bits.macro_time_clock == 0) {
        std::fclose(file);
        return false;
    }

    // Structural check of the record stream: a macro time overflow has bits
    // 30-28 clear and every remaining bit zero by definition, on both the
    // QC-x04 and the QC-x06 layout. A classic SPC overflow record instead sets
    // MTOV (bit 30) and carries a count, so it fails this test. Markers and GAP
    // records set bits 30-28 and are skipped here -- they carry payload legally.
    uint32_t records[1024];
    size_t n = std::fread(records, sizeof(uint32_t), 1024, file);
    std::fclose(file);
    if (n == 0) return false;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t rec = records[i];
        if ((rec & 0xF0000000u) == 0x80000000u && rec != 0x80000000u) return false;
    }
    return true;
}

/**
 * @brief Determines if the given file is a Carl Zeiss Confocor3 raw data file.
 */
bool isCZConfocor3File(const std::string& filename) {
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    cz_confocor3_settings_t rec{};
    std::rewind(file);
    size_t read_size = std::fread(&rec, sizeof(rec), 1, file);
    std::fclose(file);

    if (read_size != 1) return false;

    // A ConfoCor3 raw file opens with an ASCII banner:
    //   "Carl Zeiss ConfoCor3 - raw data file - version 3.000 - Channel 1"
    // The settings struct overlays that text -- `channel` is an ASCII digit, and
    // read_cz_confocor3_header() subtracts 48 from it -- so the range checks
    // below are really being applied to characters. They reject genuine files:
    // every ConfoCor3 file in the test data was undetected until this check
    // existed. Match the banner, which is what actually identifies the format.
    static const char kMagic[] = "Carl Zeiss ConfoCor3";
    if (sizeof(rec) >= sizeof(kMagic) - 1 &&
        std::memcmp(&rec, kMagic, sizeof(kMagic) - 1) == 0) {
        return true;
    }

    // Fall through to the original structural heuristics, so anything that was
    // recognised before still is.

    float frequency_float = static_cast<float>(rec.bits.frequency);
    double mt_clk = (frequency_float != 0.0f) ? (1.0 / frequency_float) : 0.0;

    if (frequency_float <= 0 || mt_clk >= 4000) return false;

    for (int i = 0; i < 4; ++i) {
        if (rec.bits.measure_id[i] >= 4000) return false;
    }
    if (rec.bits.measurement_position >= 4000) return false;
    if (rec.bits.kinetic_index >= 4000)        return false;
    if (rec.bits.repetition_number >= 4000)    return false;
    if (rec.bits.channel >= 4000)              return false;

    return true;
}

// FLIM LABS: the magic distinguishes the two time taggers from each other and
// from the three analysis formats that share the envelope. The decoder owns the
// check because it is the same envelope parse the reader already does -- and
// because "is this a time tagger" and "can this be read" must not be able to
// disagree.
bool isFlimLabsSTT1File(const std::string& filename) {
    return tttrlib::io::flimlabs_flavour(filename) == tttrlib::io::FLIMLABS_STT1;
}

bool isFlimLabsITT1File(const std::string& filename) {
    return tttrlib::io::flimlabs_flavour(filename) == tttrlib::io::FLIMLABS_ITT1;
}

/**
 * @brief Infers the type of a TTTR file based on its content.
 */
namespace {

/*!
 * \brief Hand the content sniffers down to the format table, once.
 *
 * The predicates above are public API and belong with the readers; the table is
 * a layer beneath them and must not reach up. So the direction is inverted:
 * this layer registers what it can do. On first use rather than from a static
 * initialiser -- an unreferenced initialiser is exactly what the linker drops
 * out of libtttrlib_static.a, which has already bitten this project once.
 *
 * "SM" is registered now. It was not, because isSMFile() rejected every real
 * .sm file -- it compared a native-endian uint64 against 2 where the format has
 * a big-endian uint32 -- so wiring it up would have broken files that loaded.
 * With the predicate fixed, ".sm" is checked like every other format.
 */
void ensure_sniffers() {
    static std::once_flag once;
    std::call_once(once, [] {
        using tttrlib::IORegistry;
        IORegistry::set_sniffer("PTU",         &isPTUFile);
        IORegistry::set_sniffer("HT3",         &isHT3File);
        IORegistry::set_sniffer("SPC-130",     &isBH132File);
        IORegistry::set_sniffer("SPC-QC",      &isBHSPCQCFile);
        IORegistry::set_sniffer("PHOTON-HDF5", &isHDF5File);
        IORegistry::set_sniffer("CZ-RAW",      &isCZConfocor3File);
        IORegistry::set_sniffer("PHOTONS",     &isPhotonsFile);
        IORegistry::set_sniffer("SM",          &isSMFile);
        IORegistry::set_sniffer("FLIMLABS-STT1", &isFlimLabsSTT1File);
        IORegistry::set_sniffer("FLIMLABS-ITT1", &isFlimLabsITT1File);
    });
}

}  // namespace

int inferTTTRFileType(const char* fn) {
    ensure_sniffers();
    // A plugin format that can identify itself has to be in the table before
    // anything is asked of it, and this is one of the three doors every path
    // that could need a plugin comes through.
    tttrlib::PluginHost::ensure_loaded();
    return tttrlib::IORegistry::infer_container_type(fn ? fn : "");
}

int inferTTTRContainerTypeFromExtension(const std::string& fn) {
    return tttrlib::IORegistry::container_type_from_extension(fn);
}

std::string tttrContainerCanonicalExtension(int container_type) {
    const auto* f = tttrlib::IORegistry::by_container_type(container_type);
    return f ? f->write_extension() : std::string();
}
