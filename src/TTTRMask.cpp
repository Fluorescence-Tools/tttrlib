#include "TTTRMask.h"
#include "info.h"

#ifdef _OPENMP
#include <omp.h>
#endif

void TTTRMask::set_tttr(TTTR* tttr){
    masked.resize(tttr->size(), false);
}

TTTRMask::TTTRMask(TTTR* tttr){
    set_tttr(tttr);
}

void TTTRMask::select_channels(
        TTTR* tttr,
        signed char *routing_channels, int n_routing_channels,
        bool mask
) {
    set_tttr(tttr);
    
    // Build lookup table for O(1) channel checking
    // Routing channels are typically in range [-128, 127] or [0, 255]
    constexpr int LOOKUP_SIZE = 256;
    bool channel_lookup[LOOKUP_SIZE] = {false};
    
    for (int i = 0; i < n_routing_channels; i++) {
        // Handle signed char by offsetting to [0, 255]
        unsigned char ch_idx = static_cast<unsigned char>(routing_channels[i]);
        channel_lookup[ch_idx] = true;
    }
    
    int n = static_cast<int>(tttr->size());
    signed char* channels = tttr->routing_channels;
    
    // Use OpenMP for large datasets
    bool use_openmp = tttrlib::cpu_features::get_openmp_enabled();
    
#ifdef _OPENMP
    if (use_openmp && n > 100000) {
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < n; j++) {
            unsigned char ch_idx = static_cast<unsigned char>(channels[j]);
            if (channel_lookup[ch_idx]) {
                masked[j] = mask;
            }
        }
    } else
#endif
    {
        // Serial version for small datasets or when OpenMP disabled
        for (int j = 0; j < n; j++) {
            unsigned char ch_idx = static_cast<unsigned char>(channels[j]);
            if (channel_lookup[ch_idx]) {
                masked[j] = mask;
            }
        }
    }
}

void TTTRMask::select_microtime_ranges(
        TTTR* tttr,
        std::vector<std::pair<int, int>> micro_time_ranges
) {
    set_tttr(tttr);
    
    if (micro_time_ranges.empty()) {
        return;  // No ranges to filter
    }
    
    int n = static_cast<int>(tttr->size());
    unsigned short* micro_times = tttr->micro_times;
    
    // Build bitmap for O(1) lookup (micro times are 16-bit: 0-65535)
    constexpr int MICROTIME_MAX = 65536;
    bool micro_time_valid[MICROTIME_MAX];
    std::memset(micro_time_valid, 0, MICROTIME_MAX);
    
    // Mark valid ranges in bitmap using memset for contiguous ranges
    for (const auto& r : micro_time_ranges) {
        int start = std::max(0, r.first + 1);  // Exclusive lower bound
        int end = std::min(MICROTIME_MAX - 1, r.second - 1);  // Exclusive upper bound
        if (end >= start) {
            std::memset(&micro_time_valid[start], 1, end - start + 1);
        }
    }
    
    bool use_openmp = tttrlib::cpu_features::get_openmp_enabled();
    
#ifdef _OPENMP
    if (use_openmp && n > 100000) {
        #pragma omp parallel for schedule(static)
        for(int i = 0; i < n; i++){
            unsigned short micro_time = micro_times[i];
            // Mask photons that are OUTSIDE valid ranges (out_of_bounds = !valid)
            masked[i] = masked[i] || !micro_time_valid[micro_time];
        }
    } else
#endif
    {
        for(int i = 0; i < n; i++){
            unsigned short micro_time = micro_times[i];
            // Mask photons that are OUTSIDE valid ranges (out_of_bounds = !valid)
            masked[i] = masked[i] || !micro_time_valid[micro_time];
        }
    }
}

std::vector<int> TTTRMask::get_indices(bool selected) {
    std::vector<int> idxs;
    if(selected){
        for(int idx=0; idx < size(); idx++){
            if(!masked[idx]) idxs.emplace_back(idx);
        }
    } else{
        for(int idx=0; idx < size(); idx++){
            if(masked[idx]) idxs.emplace_back(idx);
        }
    }
    return idxs;
}


std::vector<int> TTTRMask::get_selected_ranges() {
    std::vector<int> rng;
    int start = 0;
    int stop = 0;

    while(start < size()){
        // linear search for first element
        for(; start < size(); start++){
            if(masked[start] == 0){
                break;
            }
        }
        // If we reached the end without finding a selected element, exit
        if(start >= size()){
            break;
        }
        // linear search for last element
        for(stop=start + 1; stop < size(); stop++){
            if(masked[stop] == 0){
                break;
            }
        }
        rng.emplace_back(start);
        rng.emplace_back(stop);
        // Move start to the end of the current range to continue searching
        start = stop;
    }

    return rng;
}

void TTTRMask::select_count_rate(TTTR* tttr, double time_window, int n_ph_max, bool invert){
    if(tttr == nullptr) return;
    set_tttr(tttr);

    double macro_time_calibration = tttr->get_header()->get_macro_time_resolution();
    auto tw = (unsigned long) (time_window / macro_time_calibration);

    int i = 0;
    while (i < tttr->size() - 1){
        int n_ph = 0; int r = i;
        unsigned long long t_i = tttr->get_macro_time_at(i);
        while((tttr->get_macro_time_at(r) - t_i < tw) && (r < tttr->size() - 1)){
            r++; n_ph++;
        }
        masked[i] = invert ? (n_ph >= n_ph_max) : (n_ph < n_ph_max);
        i = r;
    }
}
