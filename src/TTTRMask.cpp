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
                masked[j] = false;
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

std::vector<int> TTTRMask::get_indices() {
    std::vector<int> idxs;
    for(int idx=0; idx < size(); idx++){
        if(!masked[idx]){
            idxs.emplace_back(idx);
        }
    }
    return idxs;
}