#include "FileCheck.h"

// HDF5 file signature: "\x89HDF\r\n\x1A\n"
bool isHDF5File(const std::string& filename) {
    // Open the file in binary mode
    std::ifstream file(filename, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open the file.\n";
        return false;
    }

    // The HDF5 file signature is 8 bytes long
    const std::array<unsigned char, 8> hdf5_signature = {0x89, 'H', 'D', 'F', 0x0D, 0x0A, 0x1A, 0x0A};
    std::array<unsigned char, 8> file_signature = {0};

    // Read the first 8 bytes of the file
    file.read(reinterpret_cast<char*>(file_signature.data()), file_signature.size());

    // Check if we read 8 bytes
    if (!file) {
        std::cerr << "Error: Could not read the file signature.\n";
        return false;
    }

    // Compare the read bytes with the HDF5 signature
    return file_signature == hdf5_signature;
}


// Function to check if the file is an SM file
bool isSMFile(const std::string& filename) {
    uint64_t first_value;
    FILE* file = fopen(filename.c_str(), "rb");

    if (file == nullptr) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    // Read the first 64-bit (8 bytes) from the file
    size_t read_size = fread(&first_value, sizeof(first_value), 1, file);
    if (read_size != 1) {
        std::cerr << "Error reading the file" << std::endl;
        fclose(file);
        return false;
    }

    // Close the file
    fclose(file);

    // Compare the read value with 2
    if (first_value == 2) {
        return true;
    }

    return false;
}


// Function to check if the file is a PTU file
bool isPTUFile(const std::string& filename) {
    char Magic[8];
    FILE* file = fopen(filename.c_str(), "rb");

    if (file == nullptr) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    // Always rewind the file pointer to the beginning
    std::fseek(file, 0, SEEK_SET);

    // Read the first 8 bytes of the file (Magic)
    size_t read_size = fread(Magic, 1, sizeof(Magic), file);
    if (read_size != sizeof(Magic)) {
        std::cerr << "Error reading the Magic header" << std::endl;
        fclose(file);
        return false;
    }

    // Close the file
    fclose(file);

    // Check if the Magic header starts with "PQTTTR"
    if (strncmp(Magic, "PQTTTR", 6) == 0) {
        return true;  // This is a PTU file
    }

    return false;  // This is not a PTU file
}


// Function to check if the file is an HT3 file
bool isHT3File(const std::string& filename) {
    pq_ht3_Header_t ht3_header_begin;
    FILE* file = fopen(filename.c_str(), "rb");

    if (file == nullptr) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    // Always rewind the file pointer to the beginning
    std::fseek(file, 0, SEEK_SET);

    // Read the header of the HT3 file
    size_t read_size = fread(&ht3_header_begin, 1, sizeof(ht3_header_begin), file);
    if (read_size != sizeof(ht3_header_begin)) {
        std::cerr << "Error reading the HT3 header" << std::endl;
        fclose(file);
        return false;
    }

    // Close the file
    fclose(file);

    // Check if the FormatVersion is "1.0"
    if (strncmp(ht3_header_begin.FormatVersion, "1.0", 3) == 0) {
        return true;  // This is a valid HT3 file
    }

    return false;  // This is not an HT3 file
}

// Function to check if the file is a BH132 file
bool isBH132File(const std::string& filename) {
    bh_spc132_header_t rec;
    FILE* file = fopen(filename.c_str(), "rb");

    if (file == nullptr) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    // Always rewind the file pointer to the beginning
    std::fseek(file, 0, SEEK_SET);

    // Read the header of the BH132 file
    size_t read_size = fread(&rec, sizeof(rec), 1, file);
    if (read_size != 1) {
        std::cerr << "Error reading the BH132 header" << std::endl;
        fclose(file);
        return false;
    }

    // Close the file
    fclose(file);

    // Check if macro_time_clock is in the range and dataset is marked as invalid
    if (rec.bits.macro_time_clock > 0 && rec.bits.macro_time_clock < 10000 && rec.bits.invalid) {
        return true;  // This is a valid BH132 file
    }

    return false;  // This is not a BH132 file
}

/**
 * @brief Determines if the given file is a Carl Zeiss Confocor3 (CZ Confocor3) raw data file.
 *
 * This function reads the header of the file and checks for specific patterns
 * to determine if the file is of type CZ Confocor3. It performs the necessary checks
 * and returns true if the file type is identified, otherwise false.
 *
 * @param filename The name of the file to check.
 * @return True if the file is a CZ Confocor3 file, false otherwise.
 */
bool isCZConfocor3File(const std::string& filename) {
    std::FILE* file = std::fopen(filename.c_str(), "rb");
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    cz_confocor3_settings_t rec;
    std::fseek(file, 0, SEEK_SET);

    size_t read_size = std::fread(&rec, sizeof(rec), 1, file);
    std::fclose(file);

    if (read_size != 1) {
        std::cerr << "Error reading file header" << std::endl;
        return false;
    }

    float frequency_float = rec.bits.frequency;
    double mt_clk = 1.0 / frequency_float;

    // Validate frequency
    if (frequency_float <= 0 || mt_clk >= 4000) {
        return false; // Invalid or out-of-range frequency
    }

    // Validate measure_id fields
    for (int i = 0; i < 4; ++i) {
        if (rec.bits.measure_id[i] >= 4000) {
            return false; // measure_id is out of valid range
        }
    }

    // Validate measurement_position
    if (rec.bits.measurement_position >= 4000) {
        return false; // measurement_position is out of valid range
    }

    // Validate kinetic_index
    if (rec.bits.kinetic_index >= 4000) {
        return false; // kinetic_index is out of valid range
    }

    // Validate repetition_number
    if (rec.bits.repetition_number >= 4000) {
        return false; // repetition_number is out of valid range
    }

    // Validate channel
    if (rec.bits.channel >= 4000) {
        return false; // channel value is out of valid range
    }

    return true; // File is identified as a CZ Confocor3 file if all checks pass
}


/**
 * @brief Infers the type of a TTTR file based on its content.
 *
 * This function uses a series of checks to determine the type of a TTTR file.
 * It returns an integer representing the container identifier for the file type.
 *
 * @param filename The name of the file to infer the type.
 * @return An integer identifier for the file type, or -1 if the file type cannot be determined.
 */
int inferTTTRFileType(const char* fn) {
    // Convert const char* to std::string
    std::string filename(fn);

    // Define the lambda function for converting a string to lowercase
    auto to_lowercase = [](const std::string& str) -> std::string {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    };

    // Extract file extension
    std::string::size_type idx = filename.rfind('.');
    if (idx != std::string::npos) {
        std::string extension = to_lowercase(filename.substr(idx + 1));

        // Check based on file extension
        if (extension == "sm") {
            return SM_CONTAINER;
        } else if (extension == "spc") {
            // Verify if it's a BH file
            if (isBH132File(filename)) {
                return BH_SPC130_CONTAINER;  // Adjust if there are different types
            }
        } else if (extension == "ht3") {
            if (isHT3File(filename)) {
                return PQ_HT3_CONTAINER;
            }
        } else if (extension == "ptu") {
            if (isPTUFile(filename)) {
                return PQ_PTU_CONTAINER;
            }
        } else if (extension == "hdf5") {
            if (isHDF5File(filename)) {
                return PHOTON_HDF_CONTAINER;
            }
        } else if (extension == "raw") {
            if (isCZConfocor3File(filename)) {
                return CZ_CONFOCOR3_CONTAINER;
            }
        }
    }

    // Fallback to content-based checking if necessary or unknown type
    return -1;  // Or another value representing "unknown" or unsupported type
}