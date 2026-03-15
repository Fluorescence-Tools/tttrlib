#ifndef TTTRLIB_MASKEDTTTR_H
#define TTTRLIB_MASKEDTTTR_H

#include "TTTR.h"
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

    std::vector<uint8_t> masked = {};  // Use uint8_t instead of bool for direct memory access

public:

    ~TTTRMask() = default;

    TTTRMask() = default;

    TTTRMask(TTTR* data);

    int size(){
        return static_cast<int>(masked.size());
    }

    void flip() {
        for (auto& m : masked) {
            m = m ? 0 : 1;
        }
    }

    void set_mask(std::vector<bool> mask){
        masked.resize(mask.size());
        for (size_t i = 0; i < mask.size(); i++) {
            masked[i] = mask[i] ? 1 : 0;
        }
    }

    std::vector<bool> get_mask_as_vector(){
        std::vector<bool> result(masked.size());
        for (size_t i = 0; i < masked.size(); i++) {
            result[i] = masked[i] != 0;
        }
        return result;
    }

    /*!
     * @brief Get mask as byte array
     * 
     * Returns pointer to internal memory directly - no allocation or copy needed.
     * 
     * @param output Pointer to unsigned char array (points to internal memory)
     * @param n_output Size of the output array
     */
    void get_mask(unsigned char** output, int* n_output){
        *n_output = static_cast<int>(masked.size());
        *output = masked.data();
    }

    /*!
     * @brief Set mask from byte array
     * 
     * @param input Unsigned char array (0 or 1 values)
     * @param n_input Size of input array
     */
    void set_mask(unsigned char* input, int n_input){
        masked.resize(n_input);
        std::memcpy(masked.data(), input, n_input);
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
