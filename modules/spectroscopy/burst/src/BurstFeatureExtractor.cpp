// SPDX-License-Identifier: BSD-3-Clause
#include "BurstFeatureExtractor.h"
#include "Registry.h"
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

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kBurstFeaturesEntry = R"JSON({
  "name": "burst_features",
  "label": "Per-burst features (sizes, durations, proximity ratio, FRET efficiency)",
  "summary": "Reduces each burst to its table row: photon counts per stream, duration, mean arrival time, mean micro time, proximity ratio and FRET efficiency with corrections.",
  "description": "The reduction from photon indices to a burst table. For every burst and every registered detector stream it counts photons, measures the duration and mean macro/micro time, and computes the proximity ratio and the corrected FRET efficiency (gamma, direct excitation, crosstalk, background) of the MFD literature. It is the C++ of ChiSurf's `generate_burst_dataframe` and produces the same cells; `tttr sm` writes its output. Streams are named -- `Duration (<detector>) (ms)` -- so any detector setup produces the same table shape.",
  "operation_type": "analysis",
  "params_schema": {
    "type": "object",
    "properties": {
      "gamma": {
        "type": "number",
        "title": "gamma",
        "default": 1.0
      },
      "beta": {
        "type": "number",
        "title": "beta (direct excitation)",
        "default": 0.0
      },
      "alpha": {
        "type": "number",
        "title": "alpha (crosstalk)",
        "default": 0.0
      },
      "background": {
        "type": "number",
        "title": "background rate per stream"
      }
    }
  },
  "inputs": {
    "required": [
      "tttr_photon_stream",
      "burst_selection"
    ]
  },
  "outputs": {
    "columns": [
      "Number of Photons (<detector>)",
      "Duration (<detector>) (ms)",
      "Mean Macro Time (<detector>) (ms)",
      "Mean Microtime (<detector>) (ns)",
      "Proximity Ratio",
      "FRET Efficiency"
    ]
  },
  "row_grain": "burst",
  "references": [
    {
      "type": "journal",
      "authors": "Eggeling, C., Berger, S., Brand, L., Fries, J. R., Schaffer, J., Volkmer, A., Seidel, C. A. M.",
      "title": "Data registration and selective single-molecule analysis using multi-parameter fluorescence detection",
      "journal": "J Biotechnol",
      "year": 2001,
      "volume": "86",
      "pages": "163-180"
    },
    {
      "type": "journal",
      "authors": "Sisamakis, E., Valeri, A., Kalinin, S., Rothwell, P. J., Seidel, C. A. M.",
      "title": "Accurate single-molecule FRET studies using multiparameter fluorescence detection",
      "journal": "Methods Enzymol",
      "year": 2010,
      "volume": "475",
      "pages": "455-514"
    }
  ],
  "api": [
    "BurstFeatureExtractor",
    "BurstFeature"
  ],
  "can_replay": true
})JSON";
bool register_burstfeatureextractor_entries() {
    tttrlib::register_algorithm_json("burst", "burst_features", kBurstFeaturesEntry);
    return true;
}
const bool kBurstFeatureExtractorRegistered = register_burstfeatureextractor_entries();
}  // namespace
