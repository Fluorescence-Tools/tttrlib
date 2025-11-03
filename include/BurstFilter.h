#ifndef TTTRLIB_BURSTFILTER_H
#define TTCTRLIB_BURSTFILTER_H

#include <vector>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "TTTR.h"

// Forward declaration for nlohmann::json
using json = nlohmann::json;

namespace tttrlib {

/**
 * @brief Burst analysis and filtering class for TTTR data
 * 
 * Provides a cleaner interface for burst analysis similar to FRETBursts,
 * with methods for background estimation, burst detection, and burst filtering.
 */
class BurstFilter {
private:
    std::shared_ptr<TTTR> tttr_data;
    
    // Burst search parameters
    int min_photons_per_burst;  // L parameter
    int window_size;            // m parameter
    double window_time;         // T parameter (in seconds)
    
    // Background estimation parameters
    double background_time_window;  // Time window for background estimation (seconds)
    double background_threshold;    // Minimum threshold for background photons
    
    // Burst data
    std::vector<std::pair<int64_t, int64_t>> bursts;  // Start/stop indices of detected bursts
    std::vector<double> background_rates;             // Background rates per time window
    
public:
    /**
     * @brief Constructor
     * @param data TTTR data to analyze
     */
    explicit BurstFilter(std::shared_ptr<TTTR> data);
    
    /**
     * @brief Set burst search parameters
     * @param min_photons Minimum number of photons per burst (L)
     * @param window_photons Number of consecutive photons for rate calculation (m)
     * @param window_time_max Maximum time window for rate calculation (T in seconds)
     */
    void set_burst_parameters(int min_photons, int window_photons, double window_time_max);
    
    /**
     * @brief Set background estimation parameters
     * @param time_window Time window for background estimation (seconds)
     * @param threshold Minimum threshold for background photons
     */
    void set_background_parameters(double time_window, double threshold = 0.0);
    
    /**
     * @brief Perform burst search on the TTTR data
     * @return Vector of burst start/stop indices
     */
    std::vector<std::pair<int64_t, int64_t>> find_bursts();
    
    /**
     * @brief Estimate background rates
     * @return Vector of background rates (photons per second)
     */
    std::vector<double> estimate_background();
    
    /**
     * @brief Filter bursts by size (number of photons)
     * @param min_size Minimum burst size
     * @param max_size Maximum burst size (negative for no limit)
     * @return Filtered burst indices
     */
    std::vector<std::pair<int64_t, int64_t>> filter_by_size(int min_size, int max_size = -1);
    
    /**
     * @brief Filter bursts by duration
     * @param min_duration Minimum burst duration (seconds)
     * @param max_duration Maximum burst duration (seconds, negative for no limit)
     * @return Filtered burst indices
     */
    std::vector<std::pair<int64_t, int64_t>> filter_by_duration(double min_duration, double max_duration = -1.0);
    
    /**
     * @brief Filter bursts by background rate
     * @param max_background_ratio Maximum ratio of burst photons to background
     * @return Filtered burst indices
     */
    std::vector<std::pair<int64_t, int64_t>> filter_by_background(double max_background_ratio);
    
    /**
     * @brief Get burst properties (size, duration, etc.)
     * @param burst_index Index of the burst
     * @return Vector containing [start_index, stop_index, size, duration_seconds]
     */
    std::vector<double> get_burst_properties(size_t burst_index);
    
    /**
     * @brief Get all burst properties as a matrix
     * @return Matrix where each row contains [start_index, stop_index, size, duration_seconds]
     */
    std::vector<std::vector<double>> get_all_burst_properties();
    
    /**
     * @brief Create TTTR object containing only photons from selected bursts
     * @param selected_bursts Vector of burst indices to include
     * @return TTTR object with photons from selected bursts
     */
    std::shared_ptr<TTTR> get_burst_photons(const std::vector<std::pair<int64_t, int64_t>>& selected_bursts);
    
    /**
     * @brief Merge bursts that are close together
     * @param max_gap Maximum gap between bursts to merge (in photons)
     * @return Merged burst indices
     */
    std::vector<std::pair<int64_t, int64_t>> merge_bursts(int max_gap = 5);
    
    /**
     * @brief Get the number of detected bursts
     * @return Number of bursts
     */
    size_t get_burst_count() const;
    
    /**
     * @brief Get detected bursts
     * @return Vector of burst start/stop indices
     */
    const std::vector<std::pair<int64_t, int64_t>>& get_bursts() const;
    
    /**
     * @brief Serialize burst parameters to JSON
     * @return JSON object containing burst parameters
     */
    json to_json() const;
    
    /**
     * @brief Load burst parameters from JSON
     * @param j JSON object containing burst parameters
     */
    void from_json(const json& j);
    
    /**
     * @brief Save burst parameters to JSON file
     * @param filename Path to JSON file
     */
    void save_parameters(const std::string& filename) const;
    
    /**
     * @brief Load burst parameters from JSON file
     * @param filename Path to JSON file
     */
    void load_parameters(const std::string& filename);
};

} // namespace tttrlib

#endif // TTCTRLIB_BURSTFILTER_H
