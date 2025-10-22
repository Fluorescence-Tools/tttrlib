#ifndef TTTRLIB_CLSMPIXEL_H
#define TTTRLIB_CLSMPIXEL_H

#include <utility>

#include "TTTR.h"
#include "TTTRRange.h"

class CLSMPixel : public TTTRRange{
    // Inherit directly from TTTRRange, not TTTRSelection
    // Pixels don't need selection flags (always dense, never inverted)
    // This saves 8 bytes per pixel (1 byte + 7 padding) = 26.4 MB for 3.3M pixels

    friend class CLSMLine;
    friend class CLSMImage;

public:

    virtual ~CLSMPixel() = default;

    CLSMPixel() = default;

    CLSMPixel(const CLSMPixel& p2) : TTTRRange(p2){}

    CLSMPixel& operator=(const CLSMPixel& other) = default;
    
    // Pixels are always dense (no need for flag)
    bool is_dense() const { return true; }
    bool is_inverted() const { return false; }

    /*!
     * \brief Get the memory usage of this pixel in bytes.
     *
     * @return Total memory usage in bytes.
     */
    size_t get_memory_usage_bytes() const {
        size_t total = sizeof(CLSMPixel);
        // With lazy allocation (unique_ptr):
        // - Empty: just the pointer (8 bytes)
        // - Non-empty: pointer + heap allocation
        auto& indices_ptr = get_indices_ptr();
        if (indices_ptr) {
            // Use actual capacity (after shrink_to_fit, capacity == size)
            total += indices_ptr->capacity() * sizeof(int);
        }
        return total;
    }

};

#endif //TTTRLIB_CLSMPIXEL_H
