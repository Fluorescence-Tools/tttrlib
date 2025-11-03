#include "BurstFilter.h"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>

namespace tttrlib {

BurstFilter::BurstFilter(std::shared_ptr<TTTR> data) :
    tttr_data(data),
    min_photons_per_burst(10),
    window_size(10),
    window_time(1e-3),  // 1ms default
    background_time_window(10.0),  // 10s default
    background_threshold(0.0) {
}

void BurstFilter::set_burst_parameters(int min_photons, int window_photons, double window_time_max) {
    min_photons_per_burst = min_photons;
    window_size = window_photons;
    window_time = window_time_max;
}

void BurstFilter::set_background_parameters(double time_window, double threshold) {
    background_time_window = time_window;
    background_threshold = threshold;
}

std::vector<std::pair<int64_t, int64_t>> BurstFilter::find_bursts() {
    // Use the existing burst_search method from TTTR
    std::vector<long long> burst_indices = tttr_data->burst_search(
        min_photons_per_burst, 
        window_size, 
        window_time
    );
    
    // Convert interleaved indices to pairs
    bursts.clear();
    for (size_t i = 0; i < burst_indices.size(); i += 2) {
        if (i + 1 < burst_indices.size()) {
            bursts.emplace_back(burst_indices[i], burst_indices[i + 1]);
        }
    }
    
    return bursts;
}

std::vector<double> BurstFilter::estimate_background() {
    background_rates.clear();
    
    if (!tttr_data || tttr_data->size() == 0) {
        return background_rates;
    }
    
    // Get macro times
    size_t n_events = tttr_data->size();
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    
    // Calculate number of background periods
    double total_time = (tttr_data->get_macro_time_at(n_events - 1) - tttr_data->get_macro_time_at(0)) * macro_time_resolution;
    int n_periods = static_cast<int>(total_time / background_time_window);
    if (n_periods == 0) n_periods = 1;
    
    double period_time = total_time / n_periods;
    
    background_rates.resize(n_periods, 0.0);
    
    // Calculate background rate for each period
    for (int period = 0; period < n_periods; period++) {
        double period_start = period * period_time;
        double period_end = (period + 1) * period_time;
        
        // Count photons in this period
        int photon_count = 0;
        for (size_t i = 0; i < n_events; i++) {
            double photon_time = (tttr_data->get_macro_time_at(i) - tttr_data->get_macro_time_at(0)) * macro_time_resolution;
            if (photon_time >= period_start && photon_time < period_end) {
                photon_count++;
            }
        }
        
        // Calculate rate (photons per second)
        background_rates[period] = photon_count / period_time;
    }
    
    return background_rates;
}

std::vector<std::pair<int64_t, int64_t>> BurstFilter::filter_by_size(int min_size, int max_size) {
    std::vector<std::pair<int64_t, int64_t>> filtered_bursts;
    
    for (const auto& burst : bursts) {
        int64_t size = burst.second - burst.first + 1;
        if (size >= min_size) {
            if (max_size < 0 || size <= max_size) {
                filtered_bursts.push_back(burst);
            }
        }
    }
    
    return filtered_bursts;
}

std::vector<std::pair<int64_t, int64_t>> BurstFilter::filter_by_duration(double min_duration, double max_duration) {
    std::vector<std::pair<int64_t, int64_t>> filtered_bursts;
    
    if (!tttr_data) return filtered_bursts;
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    
    for (const auto& burst : bursts) {
        double duration = (tttr_data->get_macro_time_at(burst.second) - tttr_data->get_macro_time_at(burst.first)) * macro_time_resolution;
        if (duration >= min_duration) {
            if (max_duration < 0 || duration <= max_duration) {
                filtered_bursts.push_back(burst);
            }
        }
    }
    
    return filtered_bursts;
}

std::vector<std::pair<int64_t, int64_t>> BurstFilter::filter_by_background(double max_background_ratio) {
    // First estimate background if not already done
    if (background_rates.empty()) {
        estimate_background();
    }
    
    std::vector<std::pair<int64_t, int64_t>> filtered_bursts;
    
    if (!tttr_data || background_rates.empty()) return filtered_bursts;
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    double period_duration = background_time_window;
    
    for (const auto& burst : bursts) {
        // Determine which background period this burst belongs to
        double burst_start_time = (tttr_data->get_macro_time_at(burst.first) - tttr_data->get_macro_time_at(0)) * macro_time_resolution;
        int period_index = static_cast<int>(burst_start_time / period_duration);
        
        if (period_index >= 0 && period_index < static_cast<int>(background_rates.size())) {
            double background_rate = background_rates[period_index];
            int burst_size = static_cast<int>(burst.second - burst.first + 1);
            double burst_duration = (tttr_data->get_macro_time_at(burst.second) - tttr_data->get_macro_time_at(burst.first)) * macro_time_resolution;
            double expected_background = background_rate * burst_duration;
            
            // Calculate ratio of actual photons to expected background
            double ratio = expected_background > 0 ? burst_size / expected_background : 0;
            
            if (ratio <= max_background_ratio) {
                filtered_bursts.push_back(burst);
            }
        }
    }
    
    return filtered_bursts;
}

std::vector<double> BurstFilter::get_burst_properties(size_t burst_index) {
    std::vector<double> properties(4, 0.0); // [start_index, stop_index, size, duration_seconds]
    
    if (burst_index >= bursts.size() || !tttr_data) {
        return properties;
    }
    
    const auto& burst = bursts[burst_index];
    properties[0] = static_cast<double>(burst.first);
    properties[1] = static_cast<double>(burst.second);
    properties[2] = static_cast<double>(burst.second - burst.first + 1);
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    properties[3] = (tttr_data->get_macro_time_at(burst.second) - tttr_data->get_macro_time_at(burst.first)) * macro_time_resolution;
    
    return properties;
}

std::vector<std::vector<double>> BurstFilter::get_all_burst_properties() {
    std::vector<std::vector<double>> all_properties;
    
    for (size_t i = 0; i < bursts.size(); i++) {
        all_properties.push_back(get_burst_properties(i));
    }
    
    return all_properties;
}

std::shared_ptr<TTTR> BurstFilter::get_burst_photons(const std::vector<std::pair<int64_t, int64_t>>& selected_bursts) {
    if (!tttr_data) return nullptr;
    
    // Collect all photon indices from selected bursts
    std::vector<int> photon_indices;
    
    for (const auto& burst : selected_bursts) {
        for (int64_t i = burst.first; i <= burst.second; i++) {
            if (i >= 0 && i < static_cast<int64_t>(tttr_data->size())) {
                photon_indices.push_back(static_cast<int>(i));
            }
        }
    }
    
    // Create new TTTR object with selected photons
    return tttr_data->select(photon_indices.data(), static_cast<int>(photon_indices.size()));
}

std::vector<std::pair<int64_t, int64_t>> BurstFilter::merge_bursts(int max_gap) {
    if (bursts.empty()) return {};
    
    std::vector<std::pair<int64_t, int64_t>> merged_bursts;
    merged_bursts.push_back(bursts[0]);
    
    for (size_t i = 1; i < bursts.size(); i++) {
        const auto& current_burst = bursts[i];
        auto& last_merged = merged_bursts.back();
        
        // Check if current burst is close enough to merge
        if (current_burst.first - last_merged.second - 1 <= max_gap) {
            // Merge bursts by extending the stop index
            last_merged.second = std::max(last_merged.second, current_burst.second);
        } else {
            // Add as a new burst
            merged_bursts.push_back(current_burst);
        }
    }
    
    // Update the bursts member
    bursts = merged_bursts;
    return bursts;
}

size_t BurstFilter::get_burst_count() const {
    return bursts.size();
}

const std::vector<std::pair<int64_t, int64_t>>& BurstFilter::get_bursts() const {
    return bursts;
}

json BurstFilter::to_json() const {
    json j;
    j["burst_parameters"] = {
        {"min_photons_per_burst", min_photons_per_burst},
        {"window_size", window_size},
        {"window_time", window_time}
    };
    j["background_parameters"] = {
        {"background_time_window", background_time_window},
        {"background_threshold", background_threshold}
    };
    return j;
}

void BurstFilter::from_json(const json& j) {
    if (j.contains("burst_parameters")) {
        auto burst_params = j.at("burst_parameters");
        if (burst_params.contains("min_photons_per_burst")) {
            min_photons_per_burst = burst_params.at("min_photons_per_burst");
        }
        if (burst_params.contains("window_size")) {
            window_size = burst_params.at("window_size");
        }
        if (burst_params.contains("window_time")) {
            window_time = burst_params.at("window_time");
        }
    }
    
    if (j.contains("background_parameters")) {
        auto bg_params = j.at("background_parameters");
        if (bg_params.contains("background_time_window")) {
            background_time_window = bg_params.at("background_time_window");
        }
        if (bg_params.contains("background_threshold")) {
            background_threshold = bg_params.at("background_threshold");
        }
    }
}

void BurstFilter::save_parameters(const std::string& filename) const {
    std::ofstream file(filename);
    if (file.is_open()) {
        json j = to_json();
        file << j.dump(4);  // Pretty print with 4 spaces indentation
        file.close();
    }
}

void BurstFilter::load_parameters(const std::string& filename) {
    std::ifstream file(filename);
    if (file.is_open()) {
        json j;
        file >> j;
        from_json(j);
        file.close();
    }
}

} // namespace tttrlib
