#include "FileCheck.h"


// ------------------------- UTF helpers -------------------------

// Convert UTF-8 -> "native" single-byte encoding via Boost.Locale (useful on POSIX).
// On Windows we prefer UTF-16 + _wfopen for filenames; these helpers remain for text conversions.
std::string utf8_to_native(const std::string& utf8_str) {
    try {
        // Use the user's environment locale ("") and fail hard on invalid sequences.
        static boost::locale::generator gen;
        static std::locale loc = gen(""); // system locale
        return boost::locale::conv::from_utf<char>(utf8_str, loc, boost::locale::conv::stop);
    } catch (const std::exception& e) {
        std::cerr << "Error converting from UTF-8: " << e.what() << std::endl;
        return utf8_str;
    }
}

std::string native_to_utf8(const std::string& native_str) {
    try {
        static boost::locale::generator gen;
        static std::locale loc = gen(""); // system locale
        return boost::locale::conv::to_utf<char>(native_str, loc, boost::locale::conv::stop);
    } catch (const std::exception& e) {
        std::cerr << "Error converting to UTF-8: " << e.what() << std::endl;
        return native_str;
    }
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
        "raw"    // Carl Zeiss Confocor3 raw data
    };
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

    if (read_size != 1) {
        std::cerr << "Error reading the file: " << filename << std::endl;
        return false;
    }
    return (first_value == 2);
}

// Function to check if the file is a PTU file
bool isPTUFile(const std::string& filename) {
    char Magic[8] = {};
    FILE* file = open_file(filename, "rb");
    if (!file) return false;

    std::rewind(file);
    size_t read_size = std::fread(Magic, 1, sizeof(Magic), file);

    if (read_size != sizeof(Magic)) {
        std::cerr << "Error reading the Magic header: " << filename << std::endl;
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
    return (std::strncmp(ht3_header_begin.FormatVersion, "1.0", 3) == 0);
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

    float  frequency_float = rec.bits.frequency;
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
int inferTTTRFileType(const char* fn) {
    std::string filename(fn ? fn : "");

    auto to_lowercase = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    auto dot = filename.rfind('.');
    if (dot != std::string::npos) {
        std::string extension = to_lowercase(filename.substr(dot + 1));

        if (extension == "sm") {
            return SM_CONTAINER;

        } else if (extension == "spc") {
            if (isBH132File(filename)) {
                return BH_SPC130_CONTAINER;
            }

        } else if (extension == "ht3") {
            if (isHT3File(filename)) {
                return PQ_HT3_CONTAINER;
            }

        } else if (extension == "ptu") {
            if (isPTUFile(filename)) {
                return PQ_PTU_CONTAINER;
            }

        } else if (extension == "hdf5" || extension == "h5") {
            if (isHDF5File(filename)) {
                return PHOTON_HDF_CONTAINER;
            }

        } else if (extension == "raw") {
            if (isCZConfocor3File(filename)) {
                return CZ_CONFOCOR3_CONTAINER;
            }
        }
    }

    // Unknown/unsupported
    return -1;
}
