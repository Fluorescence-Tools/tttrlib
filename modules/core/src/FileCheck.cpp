// SPDX-License-Identifier: BSD-3-Clause
#include "FileCheck.h"
#include "TTTRFormat.h"

#include <mutex>


// ------------------------- UTF helpers -------------------------

// Convert UTF-8 -> "native" single-byte encoding (ISO-8859-1).
// On Windows we prefer UTF-16 + _wfopen for filenames; these helpers remain for text conversions.
std::string utf8_to_native(const std::string& utf8_str) {
    return tttrlib::string_encoding::utf8_to_native(utf8_str);
}

std::string native_to_utf8(const std::string& native_str) {
    return tttrlib::string_encoding::native_to_utf8(native_str);
}

// ---------------------- Unicode-safe fopen ----------------------

#ifdef _WIN32
#  include <windows.h>
#  include <fcntl.h>

// UTF-8 -> UTF-16 helper
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

FILE* open_file(const std::string& filename, const char* mode) {
    std::wstring wfilename = utf8_to_wide(filename);
    std::wstring wmode     = utf8_to_wide(std::string(mode ? mode : "rb"));
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (_wfopen_s(&file, wfilename.c_str(), wmode.c_str()) != 0) {
        file = nullptr;
    }
#else
    file = _wfopen(wfilename.c_str(), wmode.c_str());
#endif
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
    }
    return file;
}
#else
// POSIX: fopen handles UTF-8 paths in modern locales.
FILE* open_file(const std::string& filename, const char* mode) {
    FILE* file = std::fopen(filename.c_str(), mode);
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
    }
    return file;
}
#endif

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
    uint64_t first_value = 0;
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    std::rewind(file);
    size_t read_size = std::fread(&first_value, sizeof(first_value), 1, file);
    std::fclose(file);

    // A short read means "not this format", not an error. These predicates are
    // asked speculatively -- detection now probes every sniffer when the
    // extension does not resolve -- so a message here would print once per
    // format for every file that is simply something else.
    if (read_size != 1) return false;
    return (first_value == 2);
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
 * "SM" is absent on purpose. isSMFile() exists, but inferTTTRFileType() never
 * called it: a ".sm" file was accepted on its extension alone. Registering it
 * would reject files that load today.
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
    });
}

}  // namespace

int inferTTTRFileType(const char* fn) {
    ensure_sniffers();
    return tttrlib::IORegistry::infer_container_type(fn ? fn : "");
}

int inferTTTRContainerTypeFromExtension(const std::string& fn) {
    return tttrlib::IORegistry::container_type_from_extension(fn);
}

std::string tttrContainerCanonicalExtension(int container_type) {
    const auto* f = tttrlib::IORegistry::by_container_type(container_type);
    return f ? f->write_extension() : std::string();
}
