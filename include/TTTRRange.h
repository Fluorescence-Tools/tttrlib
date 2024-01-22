#ifndef TTTRLIB_TTTRRANGE_H
#define TTTRLIB_TTTRRANGE_H

#include <memory> /* std::shared_ptr */
#include <set>
#include <vector>
//#include<boost/container/flat_set.hpp>
#include "itlib/flat_set.hpp"

#include "TTTR.h"

/**
 * @brief Represents a range of TTTR indices.
 */
class TTTRRange {

protected:

    //boost::container::flat_set<int> _tttr_indices{};
    //std::flat_set<int> _tttr_indices{};

    /// Set of TTTR indices in the range.
    itlib::flat_set<int> _tttr_indices{};

public:

    /**
     * @brief Constructs a TTTRRange with the specified start and stop indices.
     *
     * @param start The start index of the range.
     * @param stop The stop index of the range.
     */
    TTTRRange(int start, int stop);

    /**
     * @brief Constructs a TTTRRange.
     *
     * If both `start` and `stop` are non-negative, inserts them into the `_tttr_indices` set.
     * If `other` is not nullptr, inserts its start and stop indices into the set.
     *
     * @param start The start index of the range.
     * @param stop The stop index of the range.
     * @param other Pointer to another TTTRRange (optional).
     */
    TTTRRange(int start=-1, int stop=-1, TTTRRange* other = nullptr){
        if((start >=0) && (stop >= 0)){
            _tttr_indices.insert(start);
            _tttr_indices.insert(stop);
        } else if(other != nullptr){
            _tttr_indices.insert(other->get_start());
            _tttr_indices.insert(other->get_stop());
        }
    }

    /**
     * @brief Gets the number of TTTR indices in the range.
     *
     * @return The size of the `tttr_indices` set.
     */
    virtual size_t size(){
        return _tttr_indices.size();
    }

    /**
     * @brief Copy constructor.
     *
     * @param p2 Reference to another TTTRRange to copy.
     */
    TTTRRange(const TTTRRange& p2);

    /**
     * @brief Gets the vector of TTTR indices assigned to the range.
     *
     * @return A vector containing the TTTR indices in the set.
     */
    std::vector<int> get_tttr_indices(){
        std::vector<int> v(_tttr_indices.begin(), _tttr_indices.end());
        return v;
    }

    /**
     * @brief Gets the start index of the TTTR range.
     *
     * @return The start index or -1 if the set is empty.
     */
    int get_start() const{
        if(!_tttr_indices.empty()){
            return *_tttr_indices.begin();
        } else{
            return -1;
        }
    }

    /**
     * @brief Gets the stop index of the TTTR range.
     *
     * @return The stop index or -1 if the set is empty.
     */
    int get_stop() const{
        if(!_tttr_indices.empty()){
            return *_tttr_indices.rbegin();
        } else{
            return -1;
        }
    }

    /**
     * @brief Gets a vector of the start and stop TTTR indices of the range.
     *
     * @return A vector containing the start and stop indices.
     */
    std::vector<int> get_start_stop(){
        return {
            get_start(),
            get_stop()
        };
    }

    /**
     * @brief Gets the stop time of the TTTR range.
     *
     * @param tttr Pointer to a TTTR object.
     * @return The stop time or 0 if tttr is nullptr or the set is empty.
     */
    unsigned long get_stop_time(TTTR* tttr) const{
        unsigned long time = 0;
        if(tttr!= nullptr){
            time = tttr->macro_times[*_tttr_indices.rbegin()];
        } else{
            std::cerr << "Access to TTTRRange::get_stop_time without TTTR object" << std::endl;
        }
        return time;
    }

    /**
     * @brief Gets the start time of the TTTR range.
     *
     * @param tttr Pointer to a TTTR object.
     * @return The start time or 0 if tttr is nullptr or the set is empty.
     */
    unsigned long get_start_time(TTTR* tttr) const{
        unsigned long start_time = 0;
        if(tttr!= nullptr){
            start_time = tttr->macro_times[*_tttr_indices.begin()];
        } else{
            std::cerr << "Access to TTTRRange::get_start_time without TTTR object" << std::endl;
        }
        return start_time;
    }

    /**
     * @brief Gets a vector of the start and stop times of the TTTR range.
     *
     * @param tttr Pointer to a TTTR object.
     * @return A vector containing the start and stop times.
     */
    std::vector<unsigned long> get_start_stop_time(TTTR* tttr){
        return {
                get_start_time(tttr),
                get_stop_time(tttr)
        };
    }

    /**
     * @brief Gets the duration between the start and stop times of the TTTR range.
     *
     * @param tttr Pointer to a TTTR object.
     * @return The duration or 0 if tttr is nullptr or the set is empty.
     */
    unsigned int get_duration(TTTR* tttr){
        return get_stop_time(tttr) - get_start_time(tttr);
    }

    /**
     * @brief Inserts an index into the TTTR index vector.
     *
     * @param idx The index to insert.
     */
    void insert(int idx){
        _tttr_indices.insert(idx);
    }

    /**
     * @brief Clears the TTTR index set.
     */
    void clear(){
        _tttr_indices.clear();
    }

    /**
     * @brief Strips TTTR indices from a range starting at tttr_indices[offset].
     *
     * The tttr_indices need to be sorted in ascending size.
     *
     * @param tttr_indices Vector of TTTR indices.
     * @param offset Offset index.
     * @return Updated offset index.
     */
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

    /**
     * @brief Computes the mean microtime in units of the microtime resolution.
     *
     * If there are fewer than the minimum number of photons in a TTTRRange,
     * the function returns zero.
     *
     * @param tttr_data Pointer to a TTTR object.
     * @param microtime_resolution Microtime resolution.
     * @param minimum_number_of_photons Minimum number of photons in a micro time.
     * @return The mean microtime.
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

    /**
     * @brief Gets the microtime histogram for the TTTRRange.
     *
     * @param tttr Pointer to a TTTR object.
     * @param histogram Pointer to the histogram array.
     * @param n_histogram Pointer to the size of the histogram array.
     * @param time Pointer to the time array.
     * @param n_time Pointer to the size of the time array.
     * @param micro_time_coarsening Microtime coarsening factor.
     */
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

    /**
     * @brief Computes the mean lifetime for the TTTRRange.
     *
     * If a TTTRRange has not enough photons, returns -1.
     *
     * By default, fluorescence lifetimes are computed in units of the micro time
     * if no dt is provided.
     *
     * @param tttr_data Pointer to a TTTR object.
     * @param minimum_number_of_photons Minimum number of photons in a micro time.
     * @param tttr_irf Pointer to a TTTR object of the IRF.
     * @param m0_irf Zero moment of the IRF (optional, default=1).
     * @param m1_irf First moment of the IRF (optional, default=1).
     * @param dt Time resolution of the micro time.
     * @param background Vector of background values (optional).
     * @param m0_bg Zero moment of the background (optional, default=0).
     * @param m1_bg First moment of the background (optional, default=0).
     * @param background_fraction Fraction of background pattern in data (if negative, no background).
     * @return The mean lifetime.
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

    /**
     * @brief Computes the mean lifetime for a set of TTTR indices.
     *
     * The average lifetimes are computed (not fitted) by the methods of
     * moments (Irvin Isenberg, 1973, Biophysical journal).
     *
     * If a TTTRRange has not enough photons, it is filled with zeros.
     *
     * By default, fluorescence lifetimes are computed in units of the micro time
     * if no dt is provided.
     *
     * @param tttr_indices Vector of TTTR indices.
     * @param tttr_data Pointer to a TTTR object.
     * @param minimum_number_of_photons Minimum number of photons in a micro time.
     * @param tttr_irf Pointer to a TTTR object of the IRF.
     * @param m0_irf Zero moment of the IRF (optional, default=1).
     * @param m1_irf First moment of the IRF (optional, default=1).
     * @param dt Time resolution of the micro time.
     * @param background Vector of background values (optional).
     * @param m0_bg Zero moment of the background (optional, default=0).
     * @param m1_bg First moment of the background (optional, default=0).
     * @param background_fraction Fraction of background pattern in data (if negative, no background).
     * @return The mean lifetime.
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

    /**
    * @brief Equality operator.
    *
    * @param other Another TTTRRange to compare.
    * @return True if the two TTTRRanges are equal, false otherwise.
    */
    bool operator==(const TTTRRange& other) const {
        return _tttr_indices == other._tttr_indices;
    }

    /**
     * @brief Inequality operator.
     *
     * @param other Another TTTRRange to compare.
     * @return True if the two TTTRRanges are not equal, false otherwise.
     */
    bool operator!=(const TTTRRange& other) const
    {
        return !operator==(other);
    }

    /**
     * @brief Compound assignment addition operator.
     *
     * @param rhs Another TTTRRange to add.
     * @return Reference to the modified TTTRRange.
     */
    TTTRRange& operator+=(const TTTRRange& rhs){
        for(auto &v: rhs._tttr_indices){
            _tttr_indices.insert(v);
        }
        return *this;
    }


};

#endif //TTTRLIB_TTTRRANGE_H
