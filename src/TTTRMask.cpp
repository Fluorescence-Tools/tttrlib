#include "TTTRMask.h"

void TTTRMask::set_tttr(TTTR* tttr){
    masked.resize(tttr->size(), true);
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
    for (int i = 0; i < n_routing_channels; i++) {
        int ch = routing_channels[i];
        for (int j = 0; j < tttr->size(); j++) {
            if (tttr->routing_channels[j] == ch) {
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
    for(int i=0; i < tttr->size(); i++){
        auto micro_time = tttr->micro_times[i];
        bool mask = false;
        for(auto r: micro_time_ranges){
            mask |= micro_time <= r.first;
            mask |= micro_time >= r.second;
        }
        masked[i] = masked[i] || mask;
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
        // linear search for last element
        for(stop=start + 1; stop < size(); stop++){
            if(masked[stop] == 0){
                break;
            }
        }
        rng.emplace_back(start);
        rng.emplace_back(stop);
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
        while((tttr->macro_times[r] - tttr->macro_times[i] < tw) && (r < tttr->size() - 1)){
            r++; n_ph++;
        }
        masked[i] = invert ? (n_ph >= n_ph_max) : (n_ph < n_ph_max);
        i = r;
    }
}
