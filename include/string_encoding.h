#ifndef TTTRLIB_STRING_ENCODING_H
#define TTTRLIB_STRING_ENCODING_H

#include <string>
#include <codecvt>

namespace tttrlib {

/**
 * @brief String encoding conversion utilities
 * 
 * Provides UTF-8 ↔ ISO-8859-1 (Latin-1) conversions using standard C++17 features.
 */
namespace string_encoding {

/**
 * @brief Convert UTF-8 string to native system encoding (ISO-8859-1 on Windows/Linux)
 * @param utf8_str UTF-8 encoded string
 * @return Native encoded string
 */
inline std::string utf8_to_native(const std::string& utf8_str) {
    try {
        // Use standard C++17 codecvt for UTF-8 to wide char conversion
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring wide = converter.from_bytes(utf8_str);
        
        // Convert wide char to ISO-8859-1 (Latin-1)
        std::string result;
        for (wchar_t wc : wide) {
            if (wc <= 0xFF) {
                result += static_cast<char>(wc);
            } else {
                // Character outside Latin-1 range, use replacement character
                result += '?';
            }
        }
        return result;
    } catch (const std::exception& e) {
        // On conversion error, return original string
        return utf8_str;
    }
}

/**
 * @brief Convert native system encoding (ISO-8859-1) to UTF-8
 * @param native_str Native encoded string
 * @return UTF-8 encoded string
 */
inline std::string native_to_utf8(const std::string& native_str) {
    try {
        // Convert ISO-8859-1 (Latin-1) to wide char
        std::wstring wide;
        for (unsigned char c : native_str) {
            wide += static_cast<wchar_t>(c);
        }
        
        // Convert wide char to UTF-8
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wide);
    } catch (const std::exception& e) {
        // On conversion error, return original string
        return native_str;
    }
}

/**
 * @brief Convert ISO-8859-1 string to UTF-8
 * @param iso_str ISO-8859-1 encoded string
 * @return UTF-8 encoded string
 */
inline std::string iso_8859_1_to_utf8(const std::string& iso_str) {
    return native_to_utf8(iso_str);
}

/**
 * @brief Convert UTF-8 string to ISO-8859-1
 * @param utf8_str UTF-8 encoded string
 * @return ISO-8859-1 encoded string
 */
inline std::string utf8_to_iso_8859_1(const std::string& utf8_str) {
    return utf8_to_native(utf8_str);
}

} // namespace string_encoding

} // namespace tttrlib

#endif // TTTRLIB_STRING_ENCODING_H
