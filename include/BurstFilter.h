#ifndef TTTRLIB_BURSTFILTER_H
#define TTTRLIB_BURSTFILTER_H

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include "TTTR.h"
#include "TTTRMask.h"
#include "Channel.h"

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

    // Burst data - stored as interleaved arrays [s0,e0,s1,e1,...]
    std::vector<int64_t> bursts;      // Current filtered burst indices
    std::vector<int64_t> raw_bursts; // Original unfiltered burst indices
    std::vector<double> background_rates;             // Background rates per time window

    // Burst mask
    std::shared_ptr<TTTRMask> burst_mask;  // Optional mask for burst selection

    // Photon mask for pre-filtering
    std::shared_ptr<TTTRMask> photon_mask;  // Optional mask for photon pre-filtering (channel, microtime, etc)

    // Channels for burst analysis
    std::map<std::string, std::shared_ptr<Channel>> channels;  // Named channels for analysis

    // Filter state tracking
    enum class FilterType { SIZE, DURATION, BACKGROUND, MERGE };
    struct FilterState {
        FilterType type;
        std::map<std::string, double> parameters;
    };
    std::vector<FilterState> applied_filters;  // Track applied filters for reapplication

    // Internal filter methods that don't track state (used by reapply_filters)
    void apply_size_filter(int min_size, int max_size);
    void apply_duration_filter(double min_duration, double max_duration);
    void apply_background_filter(double max_background_ratio);
    void apply_merge_filter(int max_gap);

    // Helper methods for interleaved arrays
    static std::vector<int64_t> pairs_to_interleaved(const std::vector<std::pair<int64_t,int64_t>>& vp) {
        std::vector<int64_t> out;
        out.reserve(vp.size() * 2);
        for (const auto& p : vp) {
            out.push_back(p.first);
            out.push_back(p.second);
        }
        return out;
    }
    
    static std::vector<std::pair<int64_t,int64_t>> interleaved_to_pairs(const std::vector<int64_t>& vi) {
        std::vector<std::pair<int64_t,int64_t>> out;
        out.reserve(vi.size() / 2);
        for (size_t i = 0; i + 1 < vi.size(); i += 2) {
            out.emplace_back(vi[i], vi[i + 1]);
        }
        return out;
    }

public:
    /**
     * @brief Constructor
     * @param data TTTR data to analyze
     */
    explicit BurstFilter(std::shared_ptr<TTTR> data);

    /**
     * @brief Constructor with photon mask
     * @param data TTTR data to analyze
     * @param mask TTTRMask for pre-filtering photons
     */
    BurstFilter(std::shared_ptr<TTTR> data, std::shared_ptr<TTTRMask> mask);

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
     * @brief Estimate background rates
     * @return Vector of background rates (photons per second)
     */
    std::vector<double> estimate_background();

    /**
     * @brief Get burst properties (size, duration, count rate, etc.)
     * @param burst_index Index of the burst
     * @return Vector containing [start_index, stop_index, size, duration_seconds, count_rate_photons_per_second]
     */
    std::vector<double> get_burst_properties(size_t burst_index);
    
    /**
     * @brief Get all burst properties as a matrix
     * @return Matrix where each row contains [start_index, stop_index, size, duration_seconds, count_rate_photons_per_second]
     */
    std::vector<std::vector<double>> get_all_burst_properties();
    
    /**
     * @brief Create TTTR object containing only photons from selected bursts
     * @param selected_bursts Interleaved vector of burst indices [s0,e0,s1,e1,...]
     * @return TTTR object with photons from selected bursts
     */
    std::shared_ptr<TTTR> get_burst_photons(const std::vector<int64_t>& selected_bursts);
    
    /**
     * @brief Get the number of detected bursts
     * @return Number of bursts
     */
    size_t get_burst_count() const;
    
    /**
     * @brief Reset to raw (unfiltered) bursts
     * Restores the original burst state before any filtering was applied
     */
    void reset_to_raw_bursts();
    
    /**
     * @brief Reapply all filters with current parameters
     * Re-runs the filtering pipeline on raw bursts with updated parameters
     */
    void reapply_filters();
    
    /**
     * @brief Clear all applied filters
     * Resets to raw bursts and clears filter history
     */
    void clear_filters();
    
    /**
     * @brief Get detected bursts as NumPy-compatible arrays
     * @param output Output array pointer (allocated by NumPy)
     * @param n_output Number of elements in output array
     */
    void get_bursts(long long** output, int* n_output);
    
    /**
     * @brief Find bursts and return as NumPy-compatible arrays
     * @param output Output array pointer (allocated by NumPy)
     * @param n_output Number of elements in output array
     */
    void find_bursts(long long** find_output, int* find_n_output);
    
    /**
     * @brief Filter bursts by size and return as NumPy-compatible arrays
     * @param min_size Minimum burst size
     * @param max_size Maximum burst size (-1 for unlimited)
     * @param output Output array pointer (allocated by NumPy)
     * @param n_output Number of elements in output array
     */
    void filter_by_size(int min_size, int max_size, long long** size_output, int* size_n_output);
    
    /**
     * @brief Filter bursts by duration and return as NumPy-compatible arrays
     * @param min_duration Minimum burst duration in seconds
     * @param max_duration Maximum burst duration in seconds (-1.0 for unlimited)
     * @param output Output array pointer (allocated by NumPy)
     * @param n_output Number of elements in output array
     */
    void filter_by_duration(double min_duration, double max_duration, long long** duration_output, int* duration_n_output);
    
    /**
     * @brief Filter bursts by background ratio and return as NumPy-compatible arrays
     * @param max_background_ratio Maximum background ratio
     * @param output Output array pointer (allocated by NumPy)
     * @param n_output Number of elements in output array
     */
    void filter_by_background(double max_background_ratio, long long** background_output, int* background_n_output);
    
    /**
     * @brief Merge bursts and return as NumPy-compatible arrays
     * @param max_gap Maximum gap between bursts to merge
     * @param output Output array pointer (allocated by NumPy)
     * @param n_output Number of elements in output array
     */
    void merge_bursts(int max_gap, long long** merge_output, int* merge_n_output);
    
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
    
    /**
     * @brief Create a burst mask from detected bursts
     * @return TTTRMask with burst regions marked as true
     */
    std::shared_ptr<TTTRMask> create_burst_mask();
    
    /**
     * @brief Apply a TTTRMask to filter bursts
     * @param mask TTTRMask to apply
     * @return Interleaved vector of filtered burst indices that overlap with mask [s0,e0,s1,e1,...]
     */
    std::vector<int64_t> apply_mask(const TTTRMask& mask);
    
    /**
     * @brief Get the current burst mask
     * @return Shared pointer to the burst mask (may be nullptr if not set)
     */
    std::shared_ptr<TTTRMask> get_burst_mask() const;
    
    /**
     * @brief Set a burst mask
     * @param mask TTTRMask to set
     */
    void set_burst_mask(std::shared_ptr<TTTRMask> mask);
    
    /**
     * @brief Set photon mask for pre-filtering (channel, microtime, etc)
     * @param mask TTTRMask for photon pre-filtering
     */
    void set_photon_mask(std::shared_ptr<TTTRMask> mask);
    
    /**
     * @brief Get the current photon mask
     * @return Shared pointer to the photon mask (may be nullptr if not set)
     */
    std::shared_ptr<TTTRMask> get_photon_mask() const;
    
    /**
     * @brief Clear the photon mask
     */
    void clear_photon_mask();
    
    /**
     * @brief Check if photon mask is set
     * @return True if photon mask is active
     */
    bool has_photon_mask() const;
    
    // Channel management methods
    
    /**
     * @brief Add a channel definition
     * @param channel Shared pointer to Channel object
     */
    void add_channel(std::shared_ptr<Channel> channel);
    
    /**
     * @brief Get a channel by name
     * @param name Channel name
     * @return Shared pointer to Channel (nullptr if not found)
     */
    std::shared_ptr<Channel> get_channel(const std::string& name) const;
    
    /**
     * @brief Get all channel names
     * @return Vector of channel names
     */
    std::vector<std::string> get_channel_names() const;
    
    /**
     * @brief Get number of defined channels
     */
    size_t get_channel_count() const;
    
    /**
     * @brief Remove a channel by name
     */
    void remove_channel(const std::string& name);
    
    /**
     * @brief Clear all channels
     */
    void clear_channels();
    
    /**
     * @brief Load channels from JSON string
     * @param json_str JSON string containing channel definitions
     */
    void load_channels_from_json(const std::string& json_str);
    
    /**
     * @brief Get channels as JSON string
     * @return JSON string containing all channel definitions
     */
    std::string get_channels_as_json() const;
    
    /**
     * @brief Get photon counts for each channel in a burst
     * @param burst_start Start index of burst
     * @param burst_stop Stop index of burst
     * @return Map of channel name to photon count
     */
    std::map<std::string, int> get_burst_channel_photons(int64_t burst_start, int64_t burst_stop) const;
    
    /**
     * @brief Get photon indices for each channel in a burst
     * @param burst_start Start index of burst
     * @param burst_stop Stop index of burst
     * @return Map of channel name to vector of photon indices
     */
    std::map<std::string, std::vector<int64_t>> get_burst_channel_indices(int64_t burst_start, int64_t burst_stop) const;
};

} // namespace tttrlib

#endif // TTCTRLIB_BURSTFILTER_H
