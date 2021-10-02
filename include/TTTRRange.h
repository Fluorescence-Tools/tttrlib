//
// Created by tpeulen on 10/28/20.
//

#ifndef TTTRLIB_TTTRRANGE_H
#define TTTRLIB_TTTRRANGE_H

#include <memory> /* std::shared_ptr */

#include "TTTR.h"


class TTTRRange {

private:

    // TODO: reduce memory footprint by using / refering to TTTR object
    std::shared_ptr<TTTR> tttr = nullptr;

public:

    /// The start index of the TTTRRange
    int _start = -1;

    /// The stop index of the TTTRRange
    int _stop = -1;

    /// The start time of the TTTRRange
    unsigned int _start_time = 0;

    /// The stop time of the TTTRRange
    unsigned int _stop_time = 0;

    std::vector<int> _tttr_indices = {};

    /*!
     *
     * @param start start index of the TTTRRange
     * @param stop stop index of the TTTRRange
     * @param start_time start time of the TTTRRange
     * @param stop_time stop time of the TTTRRange
     * @param pre_reserve is the number of tttr indices that is pre-allocated in
     * in memory upon creation of a TTTRRange object.
     */
    TTTRRange(
            size_t start = 0,
            size_t stop = 0,
            unsigned int start_time = 0,
            unsigned int stop_time = 0,
            TTTRRange *other = nullptr,
            int pre_reserve = 4
    );

    virtual size_t size(){
        return _tttr_indices.size();
    }

    /// Copy constructor
    TTTRRange(const TTTRRange& p2);

    /// A vector containing a set of TTTR indices that was assigned to the range
    const std::vector<int>&  get_tttr_indices(){
        return _tttr_indices;
    }

    /// A vector of the start and the stop TTTR index of the range
    std::vector<int> get_start_stop(){
        std::vector<int> v = {_start, _stop};
        return v;
    }

    /// A vector of the start and stop time
    std::vector<unsigned int> get_start_stop_time(){
        std::vector<unsigned int> v = {_start_time, _stop_time};
        return v;
    }

    /// The difference between the start and the stop time of a range
    long long get_duration(){
        return get_start_stop_time()[1] - get_start_stop_time()[0];
    }

    /// The start index of the TTTR range object
    void set_start(int start_value){
        _start = start_value;
    }

    /// The start index of the TTTR range object
    int get_start() const{
        return _start;
    }

    /// The stop index of the TTTR range object
    void set_stop(int stop_value){
        _stop = stop_value;
    }

    /// The stop index of the TTTR range object
    int get_stop() const{
        return _stop;
    }

    /// The stop time of the TTTR range object
    void set_stop_time(unsigned int stop_time_value){
        _stop_time = stop_time_value;
    }

    /// The stop time of the TTTR range object
    unsigned int get_stop_time() const{
        return _stop_time;
    }

    /// The start time of the TTTR range object
    void set_start_time(unsigned int start_time_value){
        _start_time = start_time_value;
    }

    /// The start time of the TTTR range object
    unsigned int get_start_time() const{
        return _start_time;
    }

    /// Append a index to the TTTR index vector
    void append(int v){
        _tttr_indices.emplace_back(v);
    }

    /// Clears the TTTR index vector
    void clear(){
        _tttr_indices.clear();
    }

    void shift_start_time(long time_shift=0){
        _start_time += time_shift;
        _stop_time += time_shift;
    }

    /*!
     * Update start, stop and the start and stop using the tttr_indices
     * attribute
     *
     * @param tttr_data [in] the TTTR dataset that is used to determine the start
     * and stop time by the TTTR macro time.
     * @param from_tttr_indices [in] if set to true (default is true) the start
     * stop indices and the start stop time are updated from the tttr_indices
     * attribute. Otherwise, the start stop times are updated from the tttr object
     * using the current start stop
     */
    void update(
            TTTR* tttr_data,
            bool from_tttr_indices=true
    ){
        if(from_tttr_indices){
            if(!_tttr_indices.empty()){
                _start = _tttr_indices[0];
                _stop = _tttr_indices[_tttr_indices.size() - 1];
                _start_time = tttr_data->macro_times[_start];
                _stop_time = tttr_data->macro_times[_stop];
            }
        } else{
            _start_time = tttr_data->macro_times[_start];
            _stop_time = tttr_data->macro_times[_stop];
        }
    }

    bool operator==(const TTTRRange& other) const
    {
        return  (_tttr_indices == other._tttr_indices) &&
                (_start == other._start) &&
                (_stop == other._stop) &&
                (_start_time == other._start_time) &&
                (_stop_time == other._stop_time);
    }

    bool operator!=(const TTTRRange& other) const
    {
        return !operator==(other);
    }

};

#include "TTTR.h"

#endif //TTTRLIB_TTTRRANGE_H
