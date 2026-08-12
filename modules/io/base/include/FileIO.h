// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_FILEIO_H
#define TTTRLIB_FILEIO_H

/*!
 * \file FileIO.h
 * \brief Opening files portably, with non-ASCII names.
 *
 * Split out of FileCheck.h because every format module needs it and nothing
 * else in FileCheck.h is relevant to one: a reader for a vendor format wants to
 * open a file and seek in it, not to know how tttrlib decides what a file is.
 * Keeping this at the io layer is what lets a format module depend on `io`
 * alone rather than on the photon-stream data model.
 *
 * FileCheck.h includes this header, so nothing that used these before has to
 * change.
 */

#include <cstdio>
#include <string>

#include "string_encoding.h"

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
 * @brief Move `from` onto `to`, replacing `to` if it is already there.
 *
 * POSIX rename() replaces the destination atomically. Windows' rename() does
 * not: it fails outright when the target exists, so every writer that composes
 * a temporary beside its target and renames it into place -- csv, the hdf5
 * table, the store -- could only ever create a file there, never replace one.
 *
 * @return true if the file now lives at `to`.
 */
bool replace_file(const std::string& from, const std::string& to);


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
/*!
 * \brief fopen for UTF-8 paths, Unicode-safe on Windows.
 *
 * \param report print a line to stderr when the open fails. True is right for
 *        a caller that was told to open this file; a probe that ASKS whether a
 *        path is one of its own passes false, because "no" is an answer rather
 *        than an error and printing it makes a silent check chatty.
 */
FILE* open_file(const std::string& filename, const char* mode, bool report = true);

#ifdef _WIN32
/*!
 * \brief UTF-8 path -> UTF-16, for the wide CRT and Win32 file calls.
 *
 * The narrow calls take the active code page, so they cannot name a file whose
 * path is outside it. Anything opening a descriptor by hand (a lock, a shared
 * open) needs this; open_file() already does it internally.
 */
std::wstring utf8_to_wide_path(const std::string& s);
#endif


#endif  // TTTRLIB_FILEIO_H
