#ifndef TTTRLIB_MASKEDTTTR_H
#define TTTRLIB_MASKEDTTTR_H

#include "TTTR.h"
#include <vector>

class TTTR;

class TTTRMask{

    friend class TTTR;

private:

    std::vector<bool> masked = {};

public:

    ~TTTRMask() = default;

    TTTRMask() = default;

    TTTRMask(TTTR* data);

    int size(){
        return masked.size();
    }

    void set_mask(std::vector<bool> mask){
        masked = mask;
    }

    std::vector<bool>& get_mask(){
        return masked;
    }

    void set_tttr(TTTR* tttr);

    /*!
     * Selects a subset of indices by a list of routing channel numbers.
     *
     * The returned set of indices will have routing channel numbers that are in
     * the list of the provided routing channel numbers.
     *
     * @param tttr pointer to TTTR object
     * @param routing_channels[int] routing channel numbers. A subset of this
     * array will be selected by the input.
     * @param n_routing_channels[int] length of the routing channel number array.
     */
    void select_channels(TTTR* tttr, signed char *routing_channels, int n_routing_channels, bool mask=true);

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

};


#endif //TTTRLIB_MASKEDTTTR_H
