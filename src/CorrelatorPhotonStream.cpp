#include "CorrelatorPhotonStream.h"
#include "include/Verbose.h"


void CorrelatorPhotonStream::make_fine_times(
        unsigned long long *t, unsigned int n_times,
        unsigned short *tac,
        unsigned int n_tac
) {
if (is_verbose()) {
    std::clog << "-- Make fine, number of micro time channels: " << n_tac << std::endl;
}
    for (size_t i = 0; i < n_times; i++) {
        t[i] = t[i] * n_tac + tac[i];
    }
}

void CorrelatorPhotonStream::make_fine(
        unsigned short *tac, int n_tac,
        unsigned int number_of_microtime_channels
){
    if(n_tac < static_cast<int>(size())){
        std::cerr << "Error: The number of micro time events is smaller than the number of"
                     "macro time events." << std::endl;
        resize(n_tac);
    }
    CorrelatorPhotonStream::make_fine_times(
            times.data(), static_cast<unsigned int>(times.size()),
            tac, number_of_microtime_channels
    );
    set_time_axis_calibration(time_axis_calibration / number_of_microtime_channels);
}

unsigned long long CorrelatorPhotonStream::dt(){
    unsigned long long dt = times[size() - 1] - times[0];
    return dt;
}

double CorrelatorPhotonStream::sum_of_weights(){
    double np = std::accumulate(weights.begin(), weights.end(), 0.0);
if (is_verbose()) {
    std::clog << "-- Sum of weights: " << np << std::endl;
}
    return np;
}

double CorrelatorPhotonStream::mean_count_rate(){
    double cr = sum_of_weights() / (double) dt();
if (is_verbose()) {
    std::clog << "-- Mean count rate: " << cr << std::endl;
}
    return cr;
}

void CorrelatorPhotonStream::set_time_axis_calibration(double v) {
if (is_verbose()) {
    std::clog << "-- Time axis calibration [sec/bin]: " << v << std::endl;
}
    time_axis_calibration = v;
}

void CorrelatorPhotonStream::set_tttr(
        std::shared_ptr<TTTR> tttr,
        bool make_fine
){
    this->tttr = tttr;
    set_time_axis_calibration(tttr->get_header()->get_macro_time_resolution());

    resize(tttr->size());
    for(size_t i = 0; i < size(); i++) times[i] = tttr->get_macro_time_at(i);

    if(make_fine){
        unsigned int number_of_microtime_channels = tttr->get_number_of_micro_time_channels();
        unsigned short* tac; int n_tac;
        tttr->get_micro_times(&tac, &n_tac);
        this->make_fine(tac, n_tac, number_of_microtime_channels);
    }
}

void CorrelatorPhotonStream::coarsen() {
    for(size_t i=0; i< size(); i++) 
        times[i] /= 2;
    for (size_t i = 1; i < times.size(); i++) {
        if (times[i] == times[i - 1]) {
            weights[i] += weights[i - 1];
            weights[i - 1] = 0.0;
        }
    }
    size_t j=0;
    for (size_t i = 0; i < size(); i++) {
        if (weights[i] != 0) {
            weights[j] = weights[i];
            times[j] = times[i];
            j++;
        }
    }
    times.resize(j);
    weights.resize(j);
}

void CorrelatorPhotonStream::set_weights(
        const std::map<short, std::vector<double>>& filter,
        std::vector<unsigned int> micro_times,
        std::vector<signed char> routing_channels
){
    const std::vector<unsigned int>* p_micro_times = &micro_times;
    const std::vector<signed char>* p_routing_channels = &routing_channels;
    
    // Local storage in case we need to copy from TTTR
    std::vector<unsigned int> local_micro_times;
    std::vector<signed char> local_routing_channels;

    // use the tttr
    if(tttr != nullptr){
        int n = static_cast<int>(tttr->size());
        if(p_routing_channels->empty()){
            local_routing_channels.assign(tttr->routing_channels, tttr->routing_channels + n);
            p_routing_channels = &local_routing_channels;
        }
        if(p_micro_times->empty()){
            local_micro_times.assign(tttr->micro_times, tttr->micro_times + n);
            p_micro_times = &local_micro_times;
        }
    }
    // make sure that the weights are of the right size
    resize(p_micro_times->size());
    
    // Build fast lookup table for routing channels
    // Valid routing channels are [-128, 127], mapped to [0, 255] via (unsigned char) cast
    const std::vector<double>* fast_lookup[256] = {nullptr};
    for(const auto& kv : filter) {
        fast_lookup[static_cast<unsigned char>(kv.first)] = &kv.second;
    }

    // lookup the weights ch
    size_t idx = 0;
    for(auto r: *p_routing_channels){
        unsigned int m = (*p_micro_times)[idx];
        
        // Use fast lookup if possible
        const std::vector<double>* vec = fast_lookup[static_cast<unsigned char>(r)];
        if (vec != nullptr) {
            weights[idx] = (*vec)[m];
        } else {
            // Fallback to map.at() to preserve exception behavior for missing keys
            weights[idx] = filter.at(r)[m];
        }
        idx++;
    }
}