//
// Created by tpeulen on 10/28/20.
//

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