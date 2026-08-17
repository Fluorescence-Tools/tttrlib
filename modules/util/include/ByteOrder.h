// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BYTEORDER_H
#define TTTRLIB_BYTEORDER_H

// Validation: A/B-TESTED 2026-08-17 -- SwapEndian<uint16/32/64> vs int.to_bytes round trips. test/python/test_ab_core_reference.py.
//   Register: okf/testing/algorithm-validation.md

/*!
 * \file ByteOrder.h
 * \brief Byte-order conversion.
 *
 * Lives in util rather than beside a header reader because more than one format
 * needs it -- the SM container writes big-endian, and any future big-endian
 * format will too -- and because it depends on nothing at all.
 */

#include <algorithm>
#include <array>
#include <cstdint>

/**
 * Swaps the endianness of a given value.
 *
 * This function takes a reference to a value of any type `T` and swaps its byte order
 * between little-endian and big-endian formats. It uses a union to access the raw bytes
 * of the value and reverses the byte order using `std::reverse_copy`.
 *
 * @tparam T The type of the value whose endianness is to be swapped. Must be trivially
 *            copyable and have a defined byte size.
 * @param val A reference to the value whose endianness is to be swapped. The value is
 *            modified in-place.
 *
 * Example:
 *
 * int32_t original = 0x12345678;
 * SwapEndian(original);
 * // original now contains 0x78563412
 */
template <typename T>
void SwapEndian(T &val) {
    union U {
        T val;
        std::array<std::uint8_t, sizeof(T)> raw;
    } src, dst;

    src.val = val;
    std::reverse_copy(src.raw.begin(), src.raw.end(), dst.raw.begin());
    val = dst.val;
}

#endif  // TTTRLIB_BYTEORDER_H
