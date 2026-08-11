// SPDX-License-Identifier: BSD-3-Clause
#include "FileIO.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

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

#ifdef _WIN32
bool replace_file(const std::string& from, const std::string& to) {
    // MOVEFILE_REPLACE_EXISTING is what makes this the atomic replace that
    // rename() already is everywhere else.
    const std::wstring wfrom = utf8_to_wide(from);
    const std::wstring wto = utf8_to_wide(to);
    return MoveFileExW(wfrom.c_str(), wto.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}
#else
bool replace_file(const std::string& from, const std::string& to) {
    return std::rename(from.c_str(), to.c_str()) == 0;
}
#endif
