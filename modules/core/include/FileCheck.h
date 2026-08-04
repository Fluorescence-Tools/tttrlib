// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_FILECHECK_H
#define TTTRLIB_FILECHECK_H

#include <iostream>
#include <fstream>
#include <array>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "string_encoding.h"

#include "TTTRHeader.h"

// ============================================================================
// Portable 64-bit file I/O macros
// On Windows, long is 32-bit even in 64-bit builds, so we need _fseeki64/_ftelli64
// On Unix, we use fseeko/ftello which use off_t (typically 64-bit)
// ============================================================================
#ifdef _WIN32
  #define fseek64(fp, off, whence) _fseeki64(fp, static_cast<__int64>(off), whence)
  #define ftell64(fp)              _ftelli64(fp)
#else
  #define fseek64(fp, off, whence) fseeko(fp, static_cast<off_t>(off), whence)
  #define ftell64(fp)              ftello(fp)
#endif

/**
 * @brief Converts a UTF-8 encoded string to the system's native encoding for file operations.
 * 
 * This utility function converts a string from UTF-8 encoding to the system's
 * native encoding to ensure proper handling of non-ASCII characters in filenames.
 * 
 * @param utf8_str The UTF-8 encoded string to convert.
 * @return The string converted to the system's native encoding.
 */
std::string utf8_to_native(const std::string& utf8_str);

/**
 * @brief Converts a string from the system's native encoding to UTF-8.
 * 
 * This utility function converts a string from the system's native encoding
 * to UTF-8 encoding to ensure proper handling of non-ASCII characters in filenames.
 * 
 * @param native_str The native encoded string to convert.
 * @return The string converted to UTF-8 encoding.
 */
std::string native_to_utf8(const std::string& native_str);

/**
 * @brief Opens a file with support for non-ASCII filenames.
 * 
 * This function handles filename encoding conversions needed for proper
 * handling of non-ASCII characters across different platforms.
 * 
 * @param filename The name of the file to open (UTF-8 encoded).
 * @param mode The file opening mode (e.g., "rb", "w", etc.).
 * @return A FILE pointer to the opened file, or nullptr if the file could not be opened.
 */
FILE* open_file(const std::string& filename, const char* mode);

/**
 * @brief Checks if the given file is an HDF5 file.
 *
 * This function opens the specified file and performs checks to determine if
 * it conforms to the HDF5 file format.
 *
 * @param filename The name of the file to check.
 * @return true if the file is an HDF5 file, false otherwise.
 */
bool isHDF5File(const std::string& filename);

/**
 * @brief Checks if the given file is an SM file.
 *
 * This function opens the specified file and reads the first 64 bits to determine
 * if the file is an SM file, specifically checking if the value equals 2.
 *
 * @param filename The name of the file to check.
 * @return true if the file is an SM file, false otherwise.
 */
bool isSMFile(const std::string& filename);

/**
 * @brief Checks if the given file is a PTU file.
 *
 * This function opens the specified file and reads the first 8 bytes (Magic) to
 * verify if the file is a PTU file by checking if it starts with "PQTTTR".
 *
 * @param filename The name of the file to check.
 * @return true if the file is a PTU file, false otherwise.
 */
bool isPTUFile(const std::string& filename);

/**
 * @brief Checks if the given file is a Photonscore ".photons" (D7) file.
 *
 * Reads the first bytes and looks for the "D7 Photons Data" signature that
 * begins a Photonscore LINCam file.
 *
 * @param filename The name of the file to check.
 * @return true if the file is a Photonscore ".photons" file, false otherwise.
 */
bool isPhotonsFile(const std::string& filename);

/**
 * @brief Checks if the given file is an HT3 file.
 *
 * This function opens the specified file and reads the header to check if the
 * file format version is "1.0", which is required for HT3 files.
 *
 * @param filename The name of the file to check.
 * @return true if the file is an HT3 file, false otherwise.
 */
bool isHT3File(const std::string& filename);

/**
 * @brief Checks if the given file is a BH132 file.
 *
 * This function opens the specified file and reads the header to determine if
 * it is a BH132 file by checking if the `macro_time_clock` is in the range (0, 10000)
 * and if the `invalid` flag is set to true.
 *
 * @param filename The name of the file to check.
 * @return true if the file is a BH132 file, false otherwise.
 */
bool isBH132File(const std::string& filename);

/**
 * @brief Checks if the given file is a Becker & Hickl SPC-QC file.
 *
 * SPC-QC files share the `.spc` extension with the classic SPC-130/600 files
 * and open with a header in the same word, but put flags where the classic
 * header keeps reserved bits. Two of those are checked here: the raw flag,
 * which Becker & Hickl document as always set ("QC .spc files are always raw"),
 * and a non-zero macro time clock.
 *
 * Because header flags alone are weak evidence, the start of the record stream
 * is checked as well: a macro time overflow record has bits 30-28 clear and
 * every remaining bit zero by definition. A classic SPC overflow record instead
 * sets MTOV (bit 30) and carries a 28 bit count, so it fails that test.
 *
 * @note Call this only after @ref isBH132File has declined the file: classic
 *       SPC files are recognised first, by their much smaller clock value.
 *
 * @param filename The name of the file to check.
 * @return true if the file is an SPC-QC file, false otherwise.
 */
bool isBHSPCQCFile(const std::string& filename);


/**
 * @brief Determines the type of the TTTR file based on its content.
 *
 * This function uses various type-checking functions to infer the type of the
 * given TTTR file. It performs checks in a specific order to identify the file type.
 * Returns a container identifier corresponding to the file type.
 *
 * @param fn The name of the file to check.
 * @return An integer representing the type of the TTTR file, or -1 if the type cannot be determined.
 */
int inferTTTRFileType(const char* fn);


/**
 * @brief Infers a TTTR container type from a filename extension only.
 *
 * Unlike @ref inferTTTRFileType, this function does not inspect the file
 * content (magic bytes) and therefore works for files that do not exist yet.
 * It is intended for the write path, where the target container is chosen from
 * the requested output filename. The extension is matched case-insensitively:
 *
 *  - `.ptu`         -> PQ_PTU_CONTAINER
 *  - `.ht3`         -> PQ_HT3_CONTAINER
 *  - `.spc`         -> BH_SPC130_CONTAINER (default SPC flavour)
 *  - `.hdf5`, `.h5` -> PHOTON_HDF_CONTAINER
 *  - `.raw`         -> CZ_CONFOCOR3_CONTAINER
 *  - `.sm`          -> SM_CONTAINER
 *
 * @param fn The output filename to inspect.
 * @return The inferred container type, or -1 if the extension is unknown.
 */
int inferTTTRContainerTypeFromExtension(const std::string& fn);


/**
 * @brief Returns the canonical filename extension for a container type.
 *
 * Used on the write path to decide whether a requested output extension is
 * consistent with an existing container type. All Becker & Hickl SPC flavours
 * (SPC-130, SPC-600/256, SPC-600/4096) share the extension `spc`, so this map
 * groups them into one family and lets a `.spc` round trip preserve the more
 * specific source container instead of collapsing it to SPC-130. The SPC-QC
 * modules also write `.spc` and belong to the same family.
 *
 * @param container_type A `*_CONTAINER` container id.
 * @return The canonical extension without a leading dot (e.g. "ptu", "spc"),
 *         or an empty string if the container type is unknown.
 */
std::string tttrContainerCanonicalExtension(int container_type);


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
bool isCZConfocor3File(const std::string& filename);

/**
 * @brief Returns a list of supported file extensions.
 * 
 * This function returns a vector containing all the file extensions (file endings)
 * that are supported by the library.
 * 
 * @return std::vector<std::string> A vector of supported file extensions.
 */
std::vector<std::string> get_supported_filetypes();


#endif //TTTRLIB_FILECHECK_H
