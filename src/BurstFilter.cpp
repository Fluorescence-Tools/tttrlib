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
    background_threshold(0.0),
    photon_mask(nullptr) {
}

BurstFilter::BurstFilter(std::shared_ptr<TTTR> data, std::shared_ptr<TTTRMask> mask) :
    tttr_data(data),
    min_photons_per_burst(10),
    window_size(10),
    window_time(1e-3),  // 1ms default
    background_time_window(10.0),  // 10s default
    background_threshold(0.0),
    photon_mask(mask) {
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

std::vector<double> BurstFilter::get_burst_properties(size_t burst_index) {
    std::vector<double> properties(5, 0.0); // [start_index, stop_index, size, duration_seconds, count_rate]
    
    size_t array_index = burst_index * 2;
    if (array_index + 1 >= bursts.size() || !tttr_data) {
        return properties;
    }
    
    int64_t start = bursts[array_index];
    int64_t stop = bursts[array_index + 1];
    properties[0] = static_cast<double>(start);
    properties[1] = static_cast<double>(stop);
    int64_t size = stop - start + 1;
    properties[2] = static_cast<double>(size);
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    double duration = (tttr_data->get_macro_time_at(stop) - tttr_data->get_macro_time_at(start)) * macro_time_resolution;
    properties[3] = duration;
    
    // Calculate count rate (photons per second)
    if (duration > 0) {
        properties[4] = static_cast<double>(size) / duration;
    } else {
        properties[4] = 0.0;
    }
    
    return properties;
}

std::vector<std::vector<double>> BurstFilter::get_all_burst_properties() {
    std::vector<std::vector<double>> all_properties;
    
    for (size_t i = 0; i < bursts.size(); i++) {
        all_properties.push_back(get_burst_properties(i));
    }
    
    return all_properties;
}

std::shared_ptr<TTTR> BurstFilter::get_burst_photons(const std::vector<int64_t>& selected_bursts) {
    if (!tttr_data) return nullptr;
    
    // Collect all photon indices from selected bursts
    std::vector<int> photon_indices;
    
    for (size_t i = 0; i < selected_bursts.size(); i += 2) {
        if (i + 1 < selected_bursts.size()) {
            int64_t start = selected_bursts[i];
            int64_t stop = selected_bursts[i + 1];
            
            for (int64_t idx = start; idx <= stop; idx++) {
                if (idx >= 0 && idx < static_cast<int64_t>(tttr_data->size())) {
                    photon_indices.push_back(static_cast<int>(idx));
                }
            }
        }
    }
    
    // Create new TTTR object with selected photons
    return tttr_data->select(photon_indices.data(), static_cast<int>(photon_indices.size()));
}

size_t BurstFilter::get_burst_count() const {
    return bursts.size() / 2;  // Each burst takes 2 elements in interleaved array
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

std::shared_ptr<TTTRMask> BurstFilter::create_burst_mask() {
    if (!tttr_data) return nullptr;
    
    auto mask = std::make_shared<TTTRMask>(tttr_data.get());
    
    // Mark all burst regions as selected (true)
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            
            for (int64_t idx = start; idx <= stop; ++idx) {
                if (idx >= 0 && idx < static_cast<int64_t>(tttr_data->size())) {
                    mask->masked[static_cast<size_t>(idx)] = 1;
                }
            }
        }
    }
    
    burst_mask = mask;
    return mask;
}

std::vector<int64_t> BurstFilter::apply_mask(const TTTRMask& mask) {
    std::vector<int64_t> filtered_bursts;
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            
            bool burst_overlaps = false;
            for (int64_t idx = start; idx <= stop && !burst_overlaps; ++idx) {
                if (idx >= 0 && idx < static_cast<int64_t>(mask.masked.size())) {
                    if (mask.masked[static_cast<size_t>(idx)]) {
                        burst_overlaps = true;
                    }
                }
            }
            
            if (burst_overlaps) {
                filtered_bursts.push_back(start);
                filtered_bursts.push_back(stop);
            }
        }
    }
    
    return filtered_bursts;
}

std::shared_ptr<TTTRMask> BurstFilter::get_burst_mask() const {
    return burst_mask;
}

void BurstFilter::set_burst_mask(std::shared_ptr<TTTRMask> mask) {
    burst_mask = mask;
}

void BurstFilter::set_photon_mask(std::shared_ptr<TTTRMask> mask) {
    photon_mask = mask;
}

std::shared_ptr<TTTRMask> BurstFilter::get_photon_mask() const {
    return photon_mask;
}

void BurstFilter::clear_photon_mask() {
    photon_mask = nullptr;
}

bool BurstFilter::has_photon_mask() const {
    return photon_mask != nullptr;
}

// Channel management methods

void BurstFilter::add_channel(std::shared_ptr<Channel> channel) {
    if (channel) {
        channels[channel->get_name()] = channel;
    }
}

std::shared_ptr<Channel> BurstFilter::get_channel(const std::string& name) const {
    auto it = channels.find(name);
    if (it != channels.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> BurstFilter::get_channel_names() const {
    std::vector<std::string> names;
    for (const auto& pair : channels) {
        names.push_back(pair.first);
    }
    return names;
}

size_t BurstFilter::get_channel_count() const {
    return channels.size();
}

void BurstFilter::remove_channel(const std::string& name) {
    channels.erase(name);
}

void BurstFilter::clear_channels() {
    channels.clear();
}

void BurstFilter::load_channels_from_json(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);
        channels.clear();
        
        if (j.is_array()) {
            for (const auto& channel_j : j) {
                auto channel = std::make_shared<Channel>();
                channel->from_json(channel_j);
                add_channel(channel);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading channels from JSON: " << e.what() << std::endl;
    }
}

std::string BurstFilter::get_channels_as_json() const {
    json j = json::array();
    for (const auto& pair : channels) {
        j.push_back(pair.second->to_json());
    }
    return j.dump();
}

std::map<std::string, int> BurstFilter::get_burst_channel_photons(int64_t burst_start, int64_t burst_stop) const {
    std::map<std::string, int> photon_counts;
    
    if (!tttr_data) {
        return photon_counts;
    }
    
    // Initialize all channels with 0 count
    for (const auto& pair : channels) {
        photon_counts[pair.first] = 0;
    }
    
    // Count photons for each channel
    for (int64_t i = burst_start; i <= burst_stop && i < static_cast<int64_t>(tttr_data->size()); ++i) {
        int rout = tttr_data->get_routing_channel_at(i);
        int microtime = tttr_data->get_micro_time_at(i);
        
        // Check each channel
        for (const auto& pair : channels) {
            const auto& channel = pair.second;
            const auto& components = channel->get_components();
            
            for (const auto& comp : components) {
                int comp_rout = std::get<0>(comp);
                int comp_mt_start = std::get<1>(comp);
                int comp_mt_stop = std::get<2>(comp);
                
                if (rout == comp_rout && microtime >= comp_mt_start && microtime <= comp_mt_stop) {
                    photon_counts[pair.first]++;
                    break;  // Don't count this photon for other components of same channel
                }
            }
        }
    }
    
    return photon_counts;
}

std::map<std::string, std::vector<int64_t>> BurstFilter::get_burst_channel_indices(int64_t burst_start, int64_t burst_stop) const {
    std::map<std::string, std::vector<int64_t>> indices_map;
    
    if (!tttr_data) {
        return indices_map;
    }
    
    // Initialize all channels with empty vectors
    for (const auto& pair : channels) {
        indices_map[pair.first] = std::vector<int64_t>();
    }
    
    // Collect indices for each channel
    for (int64_t i = burst_start; i <= burst_stop && i < static_cast<int64_t>(tttr_data->size()); ++i) {
        int rout = tttr_data->get_routing_channel_at(i);
        int microtime = tttr_data->get_micro_time_at(i);
        
        // Check each channel
        for (const auto& pair : channels) {
            const auto& channel = pair.second;
            const auto& components = channel->get_components();
            
            for (const auto& comp : components) {
                int comp_rout = std::get<0>(comp);
                int comp_mt_start = std::get<1>(comp);
                int comp_mt_stop = std::get<2>(comp);
                
                if (rout == comp_rout && microtime >= comp_mt_start && microtime <= comp_mt_stop) {
                    indices_map[pair.first].push_back(i);
                    break;  // Don't add this index for other components of same channel
                }
            }
        }
    }
    
    return indices_map;
}

void BurstFilter::reset_to_raw_bursts() {
    bursts = raw_bursts;
}

void BurstFilter::reapply_filters() {
    if (raw_bursts.empty()) {
        return; // No raw bursts to work with
    }
    
    // Start with raw bursts
    bursts = raw_bursts;
    
    // Reapply each filter in sequence without tracking new state
    for (const auto& filter_state : applied_filters) {
        switch (filter_state.type) {
            case FilterType::SIZE: {
                int min_size = static_cast<int>(filter_state.parameters.at("min_size"));
                int max_size = static_cast<int>(filter_state.parameters.at("max_size"));
                apply_size_filter(min_size, max_size);
                break;
            }
            case FilterType::DURATION: {
                double min_duration = filter_state.parameters.at("min_duration");
                double max_duration = filter_state.parameters.at("max_duration");
                apply_duration_filter(min_duration, max_duration);
                break;
            }
            case FilterType::BACKGROUND: {
                double max_background_ratio = filter_state.parameters.at("max_background_ratio");
                apply_background_filter(max_background_ratio);
                break;
            }
            case FilterType::MERGE: {
                int max_gap = static_cast<int>(filter_state.parameters.at("max_gap"));
                apply_merge_filter(max_gap);
                break;
            }
        }
    }
}

// Internal filter methods that don't track state
void BurstFilter::apply_size_filter(int min_size, int max_size) {
    std::vector<int64_t> filtered_bursts;
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            int64_t size = stop - start + 1;
            
            if (size >= min_size) {
                if (max_size < 0 || size <= max_size) {
                    filtered_bursts.push_back(start);
                    filtered_bursts.push_back(stop);
                }
            }
        }
    }
    
    bursts = std::move(filtered_bursts);
}

void BurstFilter::apply_duration_filter(double min_duration, double max_duration) {
    std::vector<int64_t> filtered_bursts;
    
    if (!tttr_data) return;
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            
            double duration = (tttr_data->get_macro_time_at(stop) - tttr_data->get_macro_time_at(start)) * macro_time_resolution;
            if (duration >= min_duration) {
                if (max_duration < 0 || duration <= max_duration) {
                    filtered_bursts.push_back(start);
                    filtered_bursts.push_back(stop);
                }
            }
        }
    }
    
    bursts = std::move(filtered_bursts);
}

void BurstFilter::apply_background_filter(double max_background_ratio) {
    // First estimate background if not already done
    if (background_rates.empty()) {
        estimate_background();
    }
    
    std::vector<int64_t> filtered_bursts;
    
    if (!tttr_data || background_rates.empty()) return;
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    double period_duration = background_time_window;
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            
            // Determine which background period this burst belongs to
            double burst_start_time = (tttr_data->get_macro_time_at(start) - tttr_data->get_macro_time_at(0)) * macro_time_resolution;
            int period_index = static_cast<int>(burst_start_time / period_duration);
            
            if (period_index >= 0 && period_index < static_cast<int>(background_rates.size())) {
                double background_rate = background_rates[period_index];
                int burst_size = static_cast<int>(stop - start + 1);
                double burst_duration = (tttr_data->get_macro_time_at(stop) - tttr_data->get_macro_time_at(start)) * macro_time_resolution;
                double expected_background = background_rate * burst_duration;
                
                // Calculate ratio of actual photons to expected background
                double ratio = expected_background > 0 ? burst_size / expected_background : 0;
                
                if (ratio <= max_background_ratio) {
                    filtered_bursts.push_back(start);
                    filtered_bursts.push_back(stop);
                }
            }
        }
    }
    
    bursts = std::move(filtered_bursts);
}

void BurstFilter::apply_merge_filter(int max_gap) {
    if (bursts.empty() || bursts.size() < 2) return;
    
    std::vector<int64_t> merged_bursts;
    
    // Start with first burst
    merged_bursts.push_back(bursts[0]);
    merged_bursts.push_back(bursts[1]);
    
    for (size_t i = 2; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t current_start = bursts[i];
            int64_t current_stop = bursts[i + 1];
            
            // Get last merged burst
            size_t last_idx = merged_bursts.size() - 2;
            int64_t last_start = merged_bursts[last_idx];
            int64_t last_stop = merged_bursts[last_idx + 1];
            
            // Check if current burst is close enough to merge
            if (current_start - last_stop - 1 <= max_gap) {
                // Merge bursts by extending the stop index
                merged_bursts[last_idx + 1] = std::max(last_stop, current_stop);
            } else {
                // Add as a new burst
                merged_bursts.push_back(current_start);
                merged_bursts.push_back(current_stop);
            }
        }
    }
    
    bursts = std::move(merged_bursts);
}

void BurstFilter::clear_filters() {
    bursts = raw_bursts;
    applied_filters.clear();
}

void BurstFilter::get_bursts(long long** output, int* n_output) {
    *n_output = bursts.size();
    if (*n_output > 0) {
        *output = (long long*)malloc(*n_output * sizeof(long long));
        std::copy(bursts.begin(), bursts.end(), *output);
    } else {
        *output = nullptr;
    }
}

void BurstFilter::find_bursts(long long** find_output, int* find_n_output) {
    // If photon mask is set, use masked photons only
    std::shared_ptr<TTTR> data_to_search = tttr_data;
    if (photon_mask) {
        std::vector<int> selected_indices = photon_mask->get_indices(true);  // true = selected (unmasked)
        data_to_search = tttr_data->select(selected_indices.data(), static_cast<int>(selected_indices.size()));
    }
    
    // Use the existing burst_search method from TTTR
    std::vector<long long> burst_indices = data_to_search->burst_search(
        min_photons_per_burst, 
        window_size, 
        window_time
    );
    
    // Store as interleaved array [s0,e0,s1,e1,...]
    raw_bursts.clear();
    raw_bursts.reserve(burst_indices.size());
    for (auto idx : burst_indices) {
        raw_bursts.push_back(static_cast<int64_t>(idx));
    }
    
    // Set current bursts to raw bursts and clear filter history
    bursts = raw_bursts;
    applied_filters.clear();
    
    // Return the bursts array
    *find_n_output = bursts.size();
    if (*find_n_output > 0) {
        *find_output = (long long*)malloc(*find_n_output * sizeof(long long));
        std::copy(bursts.begin(), bursts.end(), *find_output);
    } else {
        *find_output = nullptr;
    }
}

void BurstFilter::filter_by_size(int min_size, int max_size, long long** size_output, int* size_n_output) {
    std::vector<int64_t> filtered_bursts;
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            int64_t size = stop - start + 1;
            
            if (size >= min_size) {
                if (max_size < 0 || size <= max_size) {
                    filtered_bursts.push_back(start);
                    filtered_bursts.push_back(stop);
                }
            }
        }
    }
    
    // Update current bursts and track filter
    bursts = std::move(filtered_bursts);
    applied_filters.push_back({FilterType::SIZE, {{"min_size", static_cast<double>(min_size)}, {"max_size", static_cast<double>(max_size)}}});
    
    // Return the updated bursts
    *size_n_output = bursts.size();
    if (*size_n_output > 0) {
        *size_output = (long long*)malloc(*size_n_output * sizeof(long long));
        std::copy(bursts.begin(), bursts.end(), *size_output);
    } else {
        *size_output = nullptr;
    }
}

void BurstFilter::filter_by_duration(double min_duration, double max_duration, long long** duration_output, int* duration_n_output) {
    std::vector<int64_t> filtered_bursts;
    
    if (!tttr_data) {
        *duration_n_output = 0;
        *duration_output = nullptr;
        return;
    }
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            
            double duration = (tttr_data->get_macro_time_at(stop) - tttr_data->get_macro_time_at(start)) * macro_time_resolution;
            if (duration >= min_duration) {
                if (max_duration < 0 || duration <= max_duration) {
                    filtered_bursts.push_back(start);
                    filtered_bursts.push_back(stop);
                }
            }
        }
    }
    
    // Update current bursts and track filter
    bursts = std::move(filtered_bursts);
    applied_filters.push_back({FilterType::DURATION, {{"min_duration", min_duration}, {"max_duration", max_duration}}});
    
    // Return the updated bursts
    *duration_n_output = bursts.size();
    if (*duration_n_output > 0) {
        *duration_output = (long long*)malloc(*duration_n_output * sizeof(long long));
        std::copy(bursts.begin(), bursts.end(), *duration_output);
    } else {
        *duration_output = nullptr;
    }
}

void BurstFilter::filter_by_background(double max_background_ratio, long long** background_output, int* background_n_output) {
    // First estimate background if not already done
    if (background_rates.empty()) {
        estimate_background();
    }
    
    std::vector<int64_t> filtered_bursts;
    
    if (!tttr_data || background_rates.empty()) {
        *background_n_output = 0;
        *background_output = nullptr;
        return;
    }
    
    double macro_time_resolution = tttr_data->get_header()->get_macro_time_resolution();
    double period_duration = background_time_window;
    
    for (size_t i = 0; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t start = bursts[i];
            int64_t stop = bursts[i + 1];
            
            // Determine which background period this burst belongs to
            double burst_start_time = (tttr_data->get_macro_time_at(start) - tttr_data->get_macro_time_at(0)) * macro_time_resolution;
            int period_index = static_cast<int>(burst_start_time / period_duration);
            
            if (period_index >= 0 && period_index < static_cast<int>(background_rates.size())) {
                double background_rate = background_rates[period_index];
                int burst_size = static_cast<int>(stop - start + 1);
                double burst_duration = (tttr_data->get_macro_time_at(stop) - tttr_data->get_macro_time_at(start)) * macro_time_resolution;
                double expected_background = background_rate * burst_duration;
                
                // Calculate ratio of actual photons to expected background
                double ratio = expected_background > 0 ? burst_size / expected_background : 0;
                
                if (ratio <= max_background_ratio) {
                    filtered_bursts.push_back(start);
                    filtered_bursts.push_back(stop);
                }
            }
        }
    }
    
    // Update current bursts and track filter
    bursts = std::move(filtered_bursts);
    applied_filters.push_back({FilterType::BACKGROUND, {{"max_background_ratio", max_background_ratio}}});
    
    // Return the updated bursts
    *background_n_output = bursts.size();
    if (*background_n_output > 0) {
        *background_output = (long long*)malloc(*background_n_output * sizeof(long long));
        std::copy(bursts.begin(), bursts.end(), *background_output);
    } else {
        *background_output = nullptr;
    }
}

void BurstFilter::merge_bursts(int max_gap, long long** merge_output, int* merge_n_output) {
    if (bursts.empty() || bursts.size() < 2) {
        *merge_n_output = bursts.size();
        if (*merge_n_output > 0) {
            *merge_output = (long long*)malloc(*merge_n_output * sizeof(long long));
            std::copy(bursts.begin(), bursts.end(), *merge_output);
        } else {
            *merge_output = nullptr;
        }
        return;
    }
    
    std::vector<int64_t> merged_bursts;
    
    // Start with first burst
    merged_bursts.push_back(bursts[0]);
    merged_bursts.push_back(bursts[1]);
    
    for (size_t i = 2; i < bursts.size(); i += 2) {
        if (i + 1 < bursts.size()) {
            int64_t current_start = bursts[i];
            int64_t current_stop = bursts[i + 1];
            
            // Get last merged burst
            size_t last_idx = merged_bursts.size() - 2;
            int64_t last_start = merged_bursts[last_idx];
            int64_t last_stop = merged_bursts[last_idx + 1];
            
            // Check if current burst is close enough to merge
            if (current_start - last_stop - 1 <= max_gap) {
                // Merge bursts by extending the stop index
                merged_bursts[last_idx + 1] = std::max(last_stop, current_stop);
            } else {
                // Add as a new burst
                merged_bursts.push_back(current_start);
                merged_bursts.push_back(current_stop);
            }
        }
    }
    
    // Update current bursts and track filter
    bursts = std::move(merged_bursts);
    applied_filters.push_back({FilterType::MERGE, {{"max_gap", static_cast<double>(max_gap)}}});
    
    // Return the updated bursts
    *merge_n_output = bursts.size();
    if (*merge_n_output > 0) {
        *merge_output = (long long*)malloc(*merge_n_output * sizeof(long long));
        std::copy(bursts.begin(), bursts.end(), *merge_output);
    } else {
        *merge_output = nullptr;
    }
}

} // namespace tttrlib
