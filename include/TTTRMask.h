#ifndef TTTRLIB_MASKEDTTTR_H
#define TTTRLIB_MASKEDTTTR_H

#include "TTTR.h"
#include <vector>


class TTTRMask{

private:

    std::vector<bool> masked = {};

public:

    ~TTTRMask() = default;

    TTTRMask() = default;

    TTTRMask(TTTR* data);

    int size(){
        return masked.size();
    }

    void set_tttr(TTTR* tttr);

    /*!
     * Selects a subset of indices by a  list of routing channel numbers.
     *
     * The returned set of indices will have routing channel numbers that are in
     * the list of the provided routing channel numbers.
     *
     * @param routing_channels[int] array of routing channel numbers. A subset of this
     * array will be selected by the input.
     * @param n_routing_channels[int] the length of the routing channel number array.
     */
    void select_channels(
            TTTR* tttr,
            signed char *routing_channels, int n_routing_channels,
            bool mask=true
            );

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

    std::vector<int> get_indices();

};


#endif //TTTRLIB_MASKEDTTTR_H
