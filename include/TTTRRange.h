#ifndef TTTRLIB_TTTRRANGE_H
#define TTTRLIB_TTTRRANGE_H

#include <algorithm> /* std::find */
#include <cstdlib>  /* std::malloc, std::free */
#include <cstring>  /* std::memcpy */
#include <memory>   /* std::shared_ptr */
#include <set>
#include <vector>
#include <cstdint>  /* uint16_t */
#include <boost/container/flat_set.hpp>
#include <boost/container/small_vector.hpp>

#include "TTTR.h"

/**
 * @brief Represents a range of TTTR indices.
 */
class TTTRRange {
    // Friend classes that need direct access to _tttr_indices
    friend class TTTRSelection;

public:
    /// Public typedef for derived classes that need the type
    /// Changed from flat_set to vector with sorted insertion
    /// Maintains sorted order via std::lower_bound in insert() method
    /// Note: small_vector causes segfaults with SWIG during cleanup, so using std::vector
    using indices_set = std::vector<int>;

protected:
    /// Protected accessor for derived classes
    const std::unique_ptr<indices_set>& get_indices_ptr() const {
        return _tttr_indices;
    }
    
    /// Protected accessor for derived classes
    std::unique_ptr<indices_set>& get_indices_ptr() {
        return _tttr_indices;
    }

private:
    /// Set of TTTR indices with lazy allocation
    /// Using unique_ptr to avoid 24-byte overhead for empty containers
    /// Only allocates when indices are actually inserted
    /// PRIVATE: Use public methods (insert, clear, get_tttr_indices) or protected get_indices_ptr()
    std::unique_ptr<indices_set> _tttr_indices;

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
            _tttr_indices = std::make_unique<indices_set>();
            _tttr_indices->push_back(start);
            if (stop != start) _tttr_indices->push_back(stop);
        } else if(other != nullptr){
            _tttr_indices = std::make_unique<indices_set>();
            int s = other->get_start();
            int e = other->get_stop();
            if (s >= 0) _tttr_indices->push_back(s);
            if (e >= 0 && e != s) _tttr_indices->push_back(e);
        }
    }

    /**
     * @brief Returns the size of the `tttr_indices` set.
     *
     * @return The size of the `tttr_indices` set.
     */
    virtual size_t size() const {
        return _tttr_indices ? _tttr_indices->size() : 0;
    }

    /**
     * @brief Copy constructor.
     *
     * @param p2 Reference to another TTTRRange to copy.
     */
    TTTRRange(const TTTRRange& p2);
    
    /**
     * @brief Copy assignment operator.
     *
     * @param other Reference to another TTTRRange to copy.
     * @return Reference to this object.
     */
    TTTRRange& operator=(const TTTRRange& other) {
        if (this != &other) {
            if (other._tttr_indices) {
                _tttr_indices = std::make_unique<indices_set>(*other._tttr_indices);
            } else {
                _tttr_indices.reset();
            }
        }
        return *this;
    }
    
    /**
     * @brief Move constructor.
     */
    TTTRRange(TTTRRange&&) noexcept = default;
    
    /**
     * @brief Move assignment operator.
     */
    TTTRRange& operator=(TTTRRange&&) noexcept = default;
    
    /**
     * @brief Virtual destructor.
     */
    virtual ~TTTRRange() = default;

    /**
     * @brief Gets the vector of TTTR indices assigned to the range.
     *
     * @return A vector containing the TTTR indices in the set.
     */
    virtual std::vector<int> get_tttr_indices() const;

    /**
     * @brief Gets TTTR indices as a raw pointer and size.
     * 
     * Allocates a copy of the internal data that Python can safely own and free.
     * The SWIG typemap will call free() on this pointer when the numpy array is garbage collected.
     *
     * @param output Pointer to receive the data pointer (int*)
     * @param n_output Pointer to receive the number of elements
     */
    virtual void get_tttr_indices(int** output, int* n_output) const {
        if (!_tttr_indices || _tttr_indices->empty()) {
            // For empty containers, allocate a dummy array that Python can own
            // Use malloc() because Python's SWIG typemap will call free()
            *output = static_cast<int*>(std::malloc(sizeof(int)));
            if (*output) (*output)[0] = 0;  // Initialize to avoid valgrind warnings
            *n_output = 0;  // But report size as 0
            return;
        }
        // Allocate a copy using malloc() so Python can safely free() it
        // This prevents double-free when both C++ destructor and Python GC clean up
        size_t n = _tttr_indices->size();
        *output = static_cast<int*>(std::malloc(n * sizeof(int)));
        if (*output) {
            std::memcpy(*output, &*_tttr_indices->begin(), n * sizeof(int));
        }
        *n_output = static_cast<int>(n);
    }

    /**
     * @brief Gets the start index of the TTTR range.
     *
     * @return The start index or -1 if the set is empty.
     */
    int get_start() const{
        if(_tttr_indices && !_tttr_indices->empty()){
            return *_tttr_indices->begin();
        }
        return -1;
    }

    /**
     * @brief Gets the stop index of the TTTR range.
     *
     * @return The stop index or -1 if the set is empty.
     */
    int get_stop() const{
        if(_tttr_indices && !_tttr_indices->empty()){
            return _tttr_indices->back();
        }
        return -1;
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
    unsigned long long get_stop_time(std::shared_ptr<TTTR> tttr) const{
        if(tttr!= nullptr){
            if(!_tttr_indices || _tttr_indices->empty()){
                return 0ULL;
            }
            return tttr->get_macro_time_at(_tttr_indices->back());
        } else{
            std::cerr << "Warning: TTTRRange::get_stop_time called without TTTR object" << std::endl;
        }
        return 0ULL;
    }

    /**
     * @brief Gets the start time of the TTTR range.
     *
     * @param tttr Pointer to a TTTR object.
     * @return The start time or 0 if tttr is nullptr or the set is empty.
     */
    unsigned long long get_start_time(std::shared_ptr<TTTR> tttr) const{
        if(tttr!= nullptr){
            if(!_tttr_indices || _tttr_indices->empty()){
                return 0ULL;
            }
            return tttr->get_macro_time_at(*_tttr_indices->begin());
        } else{
            std::cerr << "Warning: TTTRRange::get_start_time called without TTTR object" << std::endl;
        }
        return 0ULL;
    }

    /**
     * @brief Gets a vector of the start and stop times of the TTTR range.
     *
     * @param tttr Pointer to a TTTR object.
     * @return A vector containing the start and stop times.
     */
    std::vector<unsigned long long> get_start_stop_time(std::shared_ptr<TTTR> tttr){
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
    unsigned long long get_duration(std::shared_ptr<TTTR> tttr){
        return get_stop_time(tttr) - get_start_time(tttr);
    }

    /**
     * @brief Inserts an index into the TTTR index vector in sorted order.
     *
     * @param idx The index to insert.
     */
    void insert(int idx){
        if(!_tttr_indices){
            _tttr_indices = std::make_unique<indices_set>();
        }
        // Insert in sorted position to maintain order
        auto it = std::lower_bound(_tttr_indices->begin(), _tttr_indices->end(), idx);
        _tttr_indices->insert(it, idx);
    }

    /**
     * @brief Clears the TTTR index set.
     */
    void clear(){
        _tttr_indices.reset();  // Deallocate memory
    }
    
    /**
     * @brief Shrink vector capacity to fit actual size.
     * 
     * Call after filling is complete to eliminate capacity overhead.
     * Reduces memory from ~1.5-2x size to exactly size.
     */
    void shrink_to_fit(){
        if(_tttr_indices){
            _tttr_indices->shrink_to_fit();
        }
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
        if(!_tttr_indices || _tttr_indices->empty()) return offset;
        
        const int stop = _tttr_indices->back();
        for(; offset < static_cast<int>(tttr_indices.size()); ++offset){
            int v = tttr_indices[offset];
            if(v >= stop){
                break;
            }
            // For vector, use std::remove + erase idiom
            auto it = std::find(_tttr_indices->begin(), _tttr_indices->end(), v);
            if (it != _tttr_indices->end()) {
                _tttr_indices->erase(it);
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
            unsigned short micro_time_coarsening,
            int minlength
    ){
        auto v = get_tttr_indices();
        TTTR::compute_microtime_histogram(
            tttr.get(),
            histogram, n_histogram,
            time, n_time,
            micro_time_coarsening,
            &v, nullptr,
            minlength
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
        if(!_tttr_indices && !other._tttr_indices) return true;
        if(!_tttr_indices || !other._tttr_indices) return false;
        return *_tttr_indices == *other._tttr_indices;
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
        if(!rhs._tttr_indices) return *this;
        if(!_tttr_indices){
            _tttr_indices = std::make_unique<indices_set>();
        }
        // Reserve space to avoid reallocation during insert
        // This is critical for small_vector to prevent iterator invalidation
        _tttr_indices->reserve(_tttr_indices->size() + rhs._tttr_indices->size());
        // Bulk insert - safe because we pre-reserved space
        _tttr_indices->insert(_tttr_indices->end(), rhs._tttr_indices->begin(), rhs._tttr_indices->end());
        return *this;
    }


};

#endif //TTTRLIB_TTTRRANGE_H
