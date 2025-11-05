#include "BurstFeatureExtractor.h"
#include <cmath>

namespace tttrlib {
    BurstFeatureExtractor::BurstFeatureExtractor(BurstFilter& burst_filter)
        : burst_filter_(burst_filter) {
        compute_features();
    }
    
    void BurstFeatureExtractor::compute_features() {
        // Determine number of bursts from BurstFilter
        int n_bursts = static_cast<int>(burst_filter_.get_burst_count());
        burst_properties_.resize(n_bursts, std::vector<double>(5));
        
        // Iterate over bursts
        for (int i = 0; i < n_bursts; i++) {
            // per-burst properties
            auto props = burst_filter_.get_burst_properties(static_cast<size_t>(i));
            burst_properties_[i] = props;
            
            // channel photons for this burst using available API
            // props layout: [start_index, stop_index, size, duration_seconds, count_rate]
            int64_t start_idx = static_cast<int64_t>(props[0]);
            int64_t stop_idx  = static_cast<int64_t>(props[1]);
            auto ch_ph_map = burst_filter_.get_burst_channel_photons(start_idx, stop_idx);
            for (const auto& kv : ch_ph_map) {
                channel_photons_[kv.first].push_back(static_cast<double>(kv.second));
            }
            
            // Compute FRET efficiency if donor/acceptor present
            double n_d = 0.0;
            double n_a = 0.0;
            auto itD = channel_photons_.find("donor");
            if (itD != channel_photons_.end() && !itD->second.empty()) n_d = itD->second.back();
            auto itA = channel_photons_.find("acceptor");
            if (itA != channel_photons_.end() && !itA->second.empty()) n_a = itA->second.back();
            double total = n_d + n_a;
            double E = (total > 0.0) ? (n_a / total) : 0.0;
            fret_efficiencies_.push_back(E);
        }
    }
    
    std::vector<std::vector<double>> BurstFeatureExtractor::get_burst_properties() const {
        return burst_properties_;
    }
    
    std::map<std::string, std::vector<double>> BurstFeatureExtractor::get_burst_channel_photons() const {
        return channel_photons_;
    }
    
    std::map<std::string, std::vector<std::vector<int64_t>>> BurstFeatureExtractor::get_burst_channel_indices() const {
        // Optionally compute indices on demand from burst filter
        std::map<std::string, std::vector<std::vector<int64_t>>> out;
        for (size_t i = 0; i < burst_properties_.size(); ++i) {
            const auto &props = burst_properties_[i];
            int64_t start_idx = static_cast<int64_t>(props[0]);
            int64_t stop_idx  = static_cast<int64_t>(props[1]);
            auto ch_idx_map = burst_filter_.get_burst_channel_indices(start_idx, stop_idx);
            for (const auto &kv : ch_idx_map) {
                out[kv.first].push_back(kv.second);
            }
        }
        return out;
    }
    
    std::vector<double> BurstFeatureExtractor::get_fret_efficiencies() const {
        return fret_efficiencies_;
    }
}
