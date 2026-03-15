#pragma once
#include <vector>
#include <map>
#include <string>
#include "BurstFilter.h"

namespace tttrlib {
    class BurstFeatureExtractor {
    public:
        // Non-const reference to allow use of BurstFilter getters that are non-const
        explicit BurstFeatureExtractor(BurstFilter& burst_filter);
        
        // Get burst properties (per-burst: [start, stop, size, duration, rate])
        std::vector<std::vector<double>> get_burst_properties() const;
        
        // Get channel-specific information aggregated per burst
        std::map<std::string, std::vector<double>> get_burst_channel_photons() const;
        std::map<std::string, std::vector<std::vector<int64_t>>> get_burst_channel_indices() const;
        
        // Get FRET efficiencies (acceptor / (donor + acceptor)) if channels exist
        std::vector<double> get_fret_efficiencies() const;
        
    private:
        BurstFilter& burst_filter_;
        void compute_features();
        
        // Cached features
        std::vector<std::vector<double>> burst_properties_;
        std::map<std::string, std::vector<double>> channel_photons_;
        std::map<std::string, std::vector<std::vector<int64_t>>> channel_indices_;
        std::vector<double> fret_efficiencies_;
    };
}
