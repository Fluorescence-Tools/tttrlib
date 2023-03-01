#ifndef TTTRLIB_TTTRRANGE_H
#define TTTRLIB_TTTRRANGE_H

#include <memory> /* std::shared_ptr */
#include <set>
#include <vector>
#include <algorithm> // set union
#include<boost/container/flat_set.hpp>

#include "TTTR.h"


class TTTRRange {

protected:

    boost::container::flat_set<int> _tttr_indices = {};

public:

    TTTRRange(int start, int stop);

    TTTRRange(int start=-1, int stop=-1, TTTRRange* other = nullptr){
        if((start >=0) && (stop >= 0)){
            _tttr_indices.insert(start);
            _tttr_indices.insert(stop);
        } else if(other != nullptr){
            _tttr_indices.insert(other->get_start());
            _tttr_indices.insert(other->get_stop());
        }
    }

    virtual size_t size(){
        return _tttr_indices.size();
    }

    /// Copy constructor
    TTTRRange(const TTTRRange& p2);

    /// A vector containing a set of TTTR indices that was assigned to the range
    std::vector<int> get_tttr_indices(){
        std::vector<int> v(_tttr_indices.begin(), _tttr_indices.end());
        return v;
    }

    /// The start index of the TTTR range object
    int get_start() const{
        if(!_tttr_indices.empty()){
            return *_tttr_indices.begin();
        } else{
            return -1;
        }
    }

    /// The stop index of the TTTR range object
    int get_stop() const{
        if(!_tttr_indices.empty()){
            return *_tttr_indices.rbegin();
        } else{
            return -1;
        }
    }

    /// A vector of the start and the stop TTTR index of the range
    std::vector<int> get_start_stop(){
        return {
            get_start(),
            get_stop()
        };
    }

    /// The stop time of the TTTR range object
    unsigned long get_stop_time(TTTR* tttr) const{
        unsigned long time = 0;
        if(tttr!= nullptr){
            time = tttr->macro_times[*_tttr_indices.rbegin()];
        } else{
            std::cerr << "Access to TTTRRange::get_stop_time without TTTR object" << std::endl;
        }
        return time;
    }

    /// The start time of the TTTR range object
    unsigned long get_start_time(TTTR* tttr) const{
        unsigned long start_time = 0;
        if(tttr!= nullptr){
            start_time = tttr->macro_times[*_tttr_indices.begin()];
        } else{
            std::cerr << "Access to TTTRRange::get_start_time without TTTR object" << std::endl;
        }
        return start_time;
    }

    /// A vector of the start and stop time
    std::vector<unsigned long> get_start_stop_time(TTTR* tttr){
        return {
                get_start_time(tttr),
                get_stop_time(tttr)
        };
    }

    /// The difference between the start and the stop time of a range
    unsigned int get_duration(TTTR* tttr){
        return get_stop_time(tttr) - get_start_time(tttr);
    }

    /// Append a index to the TTTR index vector
    void insert(int idx){
        _tttr_indices.insert(idx);
    }

    /// Clears the TTTR index vector
    void clear(){
        _tttr_indices.clear();
    }

    /// Strip tttr_indices from a range starting at tttr_indices[offset]
    /// the tttr_indices need to be sorted in ascending size
    int strip(const std::vector<int> &tttr_indices, int offset = 0){
        if(!_tttr_indices.empty()){
            for(; offset < tttr_indices.size(); offset++){
                int v = tttr_indices[offset];
                if(v >= *_tttr_indices.rbegin()){
                    break;
                } else {
                    _tttr_indices.erase(v);
                }
            }
        }
        return offset;
    }

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
        auto v = get_tttr_indices();
        return tttr_data->get_mean_microtime(
                &v,
                microtime_resolution,
                minimum_number_of_photons
        );
    }

    void get_microtime_histogram(
            std::shared_ptr<TTTR> tttr,
            double** histogram, int* n_histogram,
            double** time, int* n_time,
            unsigned short micro_time_coarsening
    ){
        auto v = get_tttr_indices();
        TTTR::compute_microtime_histogram(
            tttr.get(),
            histogram, n_histogram,
            time, n_time,
            micro_time_coarsening,
            &v
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
            double dt = 1.0,
            std::vector<double> *background = nullptr,
            double m0_bg = 0.0, double m1_bg = 0.0,
            double background_fraction = -1.0
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
     * @param background_fraction fraction of background pattern in data (if negative no background)
     */
    static double compute_mean_lifetime(
            std::vector<int> &tttr_indices,
            TTTR *tttr_data,
            int minimum_number_of_photons = 3,
            TTTR *tttr_irf = nullptr,
            double m0_irf = 1.0, double m1_irf = 1.0,
            double dt = 1.0,
            std::vector<double> *background = nullptr,
            double m0_bg = 0.0, double m1_bg = 0.0,
            double background_fraction = -1.0
    );

    bool operator==(const TTTRRange& other) const {
        return _tttr_indices == other._tttr_indices;
    }

    bool operator!=(const TTTRRange& other) const
    {
        return !operator==(other);
    }

    TTTRRange& operator+=(const TTTRRange& rhs){
        for(auto &v: rhs._tttr_indices){
            _tttr_indices.insert(v);
        }
        return *this;
    }


};

#endif //TTTRLIB_TTTRRANGE_H
