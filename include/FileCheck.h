#ifndef TTTRLIB_FILECHECK_H
#define TTTRLIB_FILECHECK_H

#include <iostream>
#include <fstream>
#include <array>

#include "TTTRHeader.h"

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


#endif //TTTRLIB_FILECHECK_H
