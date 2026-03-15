#ifndef TTTRLIB_STRING_ENCODING_H
#define TTTRLIB_STRING_ENCODING_H

#include <string>
#include <cstdint>

namespace tttrlib {

/**
 * @brief String encoding conversion utilities
 * 
 * Provides UTF-8 ↔ ISO-8859-1 (Latin-1) conversions without deprecated C++ features.
 */
namespace string_encoding {

/**
 * @brief Convert UTF-8 string to native system encoding (ISO-8859-1 on Windows/Linux)
 * @param utf8_str UTF-8 encoded string
 * @return Native encoded string
 */
inline std::string utf8_to_native(const std::string& utf8_str) {
    std::string result;
    for (size_t i = 0; i < utf8_str.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(utf8_str[i]);
        
        if (c < 0x80) {
            // Single byte ASCII character
            result += static_cast<char>(c);
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8_str.length()) {
            // Two-byte UTF-8 sequence
            unsigned char c2 = static_cast<unsigned char>(utf8_str[++i]);
            uint32_t codepoint = ((c & 0x1F) << 6) | (c2 & 0x3F);
            if (codepoint <= 0xFF) {
                result += static_cast<char>(codepoint);
            } else {
                result += '?';
            }
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8_str.length()) {
            // Three-byte UTF-8 sequence
            unsigned char c2 = static_cast<unsigned char>(utf8_str[++i]);
            unsigned char c3 = static_cast<unsigned char>(utf8_str[++i]);
            uint32_t codepoint = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
            if (codepoint <= 0xFF) {
                result += static_cast<char>(codepoint);
            } else {
                result += '?';
            }
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8_str.length()) {
            // Four-byte UTF-8 sequence (outside Latin-1 range)
            i += 3;
            result += '?';
        } else {
            // Invalid UTF-8 sequence, skip
            result += '?';
        }
    }
    return result;
}

/**
 * @brief Convert native system encoding (ISO-8859-1) to UTF-8
 * @param native_str Native encoded string
 * @return UTF-8 encoded string
 */
inline std::string native_to_utf8(const std::string& native_str) {
    std::string result;
    for (unsigned char c : native_str) {
        if (c < 0x80) {
            // ASCII character
            result += static_cast<char>(c);
        } else {
            // Latin-1 character (0x80-0xFF) -> UTF-8 two-byte sequence
            result += static_cast<char>(0xC0 | (c >> 6));
            result += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return result;
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
