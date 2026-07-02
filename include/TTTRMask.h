// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_MASKEDTTTR_H
#define TTTRLIB_MASKEDTTTR_H

#include "TTTR.h"
#include "BitOps.h"
#include <vector>
#include <string>

class TTTR;

namespace tttrlib {
    class BurstFilter;
}

class TTTRMask{

    friend class TTTR;
    friend class tttrlib::BurstFilter;

private:

    /// Packed mask bits (64 events per word); bit i == 1 means event i is
    /// masked (excluded). Pad bits past masked_size are kept at 0.
    std::vector<uint64_t> masked_words;
    size_t masked_size = 0;

    /// Scratch buffer backing get_mask()'s numpy view (per-call snapshot).
    std::vector<uint8_t> byte_cache;

    bool get_bit(size_t i) const {
        return tttrlib::bitops::get_bit(masked_words, i);
    }

    void set_bit(size_t i, bool v) {
        tttrlib::bitops::set_bit(masked_words, i, v);
    }

    /// Resize to n bits. Preserves existing bits (like the previous
    /// vector::resize semantics) unless clear is true; pad bits stay 0.
    void resize_bits(size_t n, bool clear = false) {
        size_t nw = tttrlib::bitops::word_count(n);
        if (clear) {
            masked_words.assign(nw, 0);
        } else {
            masked_words.resize(nw, 0);
            if (!masked_words.empty()) {
                masked_words.back() &= tttrlib::bitops::tail_mask(n);
            }
        }
        masked_size = n;
    }

public:

    ~TTTRMask() = default;

    TTTRMask() = default;

    TTTRMask(TTTR* data);

    int size(){
        return static_cast<int>(masked_size);
    }

    void flip() {
        for (auto& w : masked_words) {
            w = ~w;
        }
        if (!masked_words.empty()) {
            masked_words.back() &= tttrlib::bitops::tail_mask(masked_size);
        }
    }

    void set_mask(std::vector<bool> mask){
        resize_bits(mask.size(), true);
        for (size_t i = 0; i < mask.size(); i++) {
            if (mask[i]) set_bit(i, true);
        }
    }

    std::vector<bool> get_mask_as_vector(){
        std::vector<bool> result(masked_size);
        for (size_t i = 0; i < masked_size; i++) {
            result[i] = get_bit(i);
        }
        return result;
    }

    /*!
     * @brief Get mask as byte array
     *
     * Returns a pointer to an internally cached unpacked copy of the mask
     * (values 0/1). The buffer stays valid until the next get_mask call or
     * until the mask is modified.
     *
     * @param output Pointer to unsigned char array (points to internal memory)
     * @param n_output Size of the output array
     */
    void get_mask(unsigned char** output, int* n_output){
        byte_cache.resize(masked_size);
        for (size_t i = 0; i < masked_size; i++) {
            byte_cache[i] = get_bit(i) ? 1 : 0;
        }
        *n_output = static_cast<int>(masked_size);
        *output = byte_cache.data();
    }

    /*!
     * @brief Set mask from byte array
     *
     * @param input Unsigned char array (nonzero values select)
     * @param n_input Size of input array
     */
    void set_mask(unsigned char* input, int n_input){
        resize_bits(static_cast<size_t>(n_input < 0 ? 0 : n_input), true);
        for (int i = 0; i < n_input; i++) {
            if (input[i]) set_bit(static_cast<size_t>(i), true);
        }
    }

    void set_tttr(TTTR* tttr);

    /*!
     * @brief Selects a subset of indices by a list of routing channel numbers.
     *
     * The returned set of indices will have routing channel numbers that are in
     * the list of the provided routing channel numbers.
     *
     * @param tttr Pointer to TTTR object.
     * @param routing_channels Array of routing channel numbers. A subset of this
     * array will be selected by the input.
     * @param n_routing_channels Length of the routing channel number array.
     * @param mask Default value if a channel is selected.
     */
    void select_channels(
            TTTR* tttr,
            signed char *routing_channels,
            int n_routing_channels,
            bool mask = false
    );

    /*!
     * Selects a subset of indices a count rate of a sliding time-window
     * @param tttr pointer to TTTR object
     * @param time_window time window size in units of seconds
     * @param n_ph_max maximum number of photons in time window
     * @param invert boolean used to invert selection
     */
    void select_count_rate(TTTR* tttr, double time_window, int n_ph_max, bool invert);

    /*!
     * Masks outside the provides micro time ranges
     * @param tttr
     * @param micro_time_ranges
     * @param mask
     */
    void select_microtime_ranges(TTTR* tttr,
            std::vector<std::pair<int,int>> micro_time_ranges =
                    std::vector<std::pair<int,int>>()
    );

    /*!
     *
     * @param selected if selected is true returns selected (unmasked)
     * indices otherwise masked indices are returned
     * @return
     */
    std::vector<int> get_indices(bool selected=true);

    std::vector<int> get_selected_ranges();

    /**
     * @brief Serialize TTTRMask to JSON string
     * @return JSON string containing TTTRMask data
     */
    std::string to_json() const;

    /**
     * @brief Load TTTRMask from JSON string
     * @param payload JSON string containing TTTRMask data
     */
    void from_json(const std::string& payload);

};


#endif //TTTRLIB_MASKEDTTTR_H
