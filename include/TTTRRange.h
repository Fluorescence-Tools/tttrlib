#ifndef TTTRLIB_TTTRRANGE_H
#define TTTRLIB_TTTRRANGE_H

#include <memory> /* std::shared_ptr */

#include "TTTR.h"


class TTTRRange {

protected:

    TTTR* _tttr = nullptr;

public:

    /// The start index of the TTTRRange
    int _start = 0;

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
            int pre_reserve = 4,
            TTTR* tttr = nullptr
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
        _stop = std::max(v, _stop);
        _start = std::min(v, _start);
    }

    /// Clears the TTTR index vector
    void clear(){
        _tttr_indices.clear();
    }

    /// Strip tttr_indices from a range starting at tttr_indices[offset]
    /// the tttr_indices need to be sorted in ascending size
    int strip(const std::vector<int> &tttr_indices, int first=0);

    /*!
     * Computes to the mean micro time (in units of the micro channel resolution).
     *
     * If there are less then the minimum number of photons in a TTTRRange
     * the function returns zero.
     *
     * @param tttr_data[in] pointer to a TTTR object
     * @param minimum_number_of_photons[in] the minimum number of photons in a micro time
     */
    double get_mean_microtime(
            TTTR* tttr_data,
            double microtime_resolution = -1.0,
            int minimum_number_of_photons = 1
    ){
        return tttr_data->get_mean_microtime(&_tttr_indices, microtime_resolution, minimum_number_of_photons);
    }

    void get_microtime_histogram(
            double** histogram, int* n_histogram,
            double** time, int* n_time,
            unsigned short micro_time_coarsening
    ){
        TTTR::compute_microtime_histogram(
            _tttr,
            histogram, n_histogram,
            time, n_time,
            micro_time_coarsening,
            &_tttr_indices
        );
    }

    /*!
     *  Return the average lifetime
     *
     * If a TTTRRange has not enough photons return -1
     *
     * By default the fluorescence lifetimes are computed in units of the micro time
     * if no dt is provided.
     *
     * @param tttr_data[in] pointer to a TTTR object
     * @param tttr_irf[in] pointer to a TTTR object of the IRF
     * @param minimum_number_of_photons[in] the minimum number of photons in a micro time
     * @param m0_irf is the zero moment of the IRF (optional, default=1)
     * @param m1_irf is the first moment of the IRF (optional, default=1)
     * @param dt time resolution of the micro time
     */
    double get_mean_lifetime(
            TTTR *tttr_data,
            int minimum_number_of_photons = 3,
            TTTR *tttr_irf = nullptr,
            double m0_irf = 1.0, double m1_irf = 1.0,
            double dt = 1.0
    );

    /*!
     *  Compute the average lifetime for a set of TTTR indices
     *
     * The average lifetimes are computed (not fitted) by the methods of
     * moments (Irvin Isenberg, 1973, Biophysical journal). This approach
     * does not consider scattered light.
     *
     * If a TTTRRange has not enough photons it is filled with zeros.
     *
     * By default the fluorescence lifetimes are computed in units of the micro time
     * if no dt is provided.
     *
     * @param tttr_data[in] pointer to a TTTR object
     * @param tttr_irf[in] pointer to a TTTR object of the IRF
     * @param minimum_number_of_photons[in] the minimum number of photons in a micro time
     * @param m0_irf is the zero moment of the IRF (optional, default=1)
     * @param m1_irf is the first moment of the IRF (optional, default=1)
     * @param dt time resolution of the micro time
     */
    static double compute_mean_lifetime(
            std::vector<int> &tttr_indices,
            TTTR *tttr_data,
            int minimum_number_of_photons = 3,
            TTTR *tttr_irf = nullptr,
            double m0_irf = 1.0, double m1_irf = 1.0,
            double dt = 1.0
    );

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
        _tttr = tttr_data;
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

    TTTRRange& operator+=(const TTTRRange& rhs){
        _start = std::min(_start, rhs._start);
        _stop = std::max(_stop, rhs._stop);
        _start_time = std::min(_start_time, rhs._start_time);
        _stop_time = std::max(_stop_time, rhs._stop_time);
        _tttr_indices.insert(_tttr_indices.end(), rhs._tttr_indices.begin(), rhs._tttr_indices.end());
        return *this;
    }


};

#include "TTTR.h"

#endif //TTTRLIB_TTTRRANGE_H
