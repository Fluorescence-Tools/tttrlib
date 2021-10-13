#include "TTTRRange.h"

TTTRRange::TTTRRange(const TTTRRange& p2){
    _start = p2._start;
    _stop = p2._stop;
    _start_time = p2._start_time;
    _stop_time = p2._stop_time;
    _tttr_indices.reserve(p2._tttr_indices.size());
    for(auto &v: p2._tttr_indices){
        _tttr_indices.emplace_back(v);
    }
}

TTTRRange::TTTRRange(
        size_t start,
        size_t stop,
        unsigned int start_time,
        unsigned int stop_time,
        TTTRRange* other,
        int pre_reserve
) {
    if(other != nullptr){
        this->_start = other->_start;
        this->_stop = other->_stop;
        this->_start_time = other->_start_time;
        this->_stop_time = other->_stop_time;
    } else {
        this->_start = start;
        this->_stop = stop;
        this->_start_time = start_time;
        this->_stop_time = stop_time;
    }
    _tttr_indices.reserve(pre_reserve);
}


double TTTRRange::compute_mean_lifetime(
        std::vector<int> &tttr_indices,
        TTTR *tttr_data,
        int minimum_number_of_photons,
        TTTR *tttr_irf,
        double m0_irf,
        double m1_irf,
        double dt
) {
    return TTTR::compute_mean_lifetime(
            tttr_data, tttr_irf,
            m0_irf, m1_irf,
            &tttr_indices,
            dt, minimum_number_of_photons
    );
}


double TTTRRange::get_mean_lifetime(
        TTTR *tttr_data,
        int minimum_number_of_photons,
        TTTR *tttr_irf,
        double m0_irf, double m1_irf,
        double dt
        ) {
    return TTTRRange::compute_mean_lifetime(
            _tttr_indices,
            tttr_data,
            minimum_number_of_photons,
            tttr_irf,
            m0_irf, m1_irf,
            dt
    );
}