#include "CorrelatorPhotonStream.h"


void CorrelatorPhotonStream::make_fine_times(
        unsigned long long *t, unsigned int n_times,
        unsigned short *tac,
        unsigned int n_tac
) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Make fine, number of micro time channels: " << n_tac << std::endl;
#endif
    for (size_t i = 0; i < n_times; i++) {
        t[i] = t[i] * n_tac + tac[i];
    }
}

void CorrelatorPhotonStream::make_fine(
        unsigned short *tac, int n_tac,
        unsigned int number_of_microtime_channels
){
    if(n_tac < (int) size()){
        std::cerr << "Error: The number of micro time events is smaller than the number of"
                     "macro time events." << std::endl;
        resize(n_tac);
    }
    CorrelatorPhotonStream::make_fine_times(
            times.data(), times.size(),
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
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Sum of weights: " << np << std::endl;
#endif
    return np;
}

double CorrelatorPhotonStream::mean_count_rate(){
    double cr = sum_of_weights() / (double) dt();
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Mean count rate: " << cr << std::endl;
#endif
    return cr;
}

void CorrelatorPhotonStream::set_time_axis_calibration(double v) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Time axis calibration [sec/bin]: " << v << std::endl;
#endif
    time_axis_calibration = v;
}

void CorrelatorPhotonStream::set_tttr(
        std::shared_ptr<TTTR> tttr,
        bool make_fine
){
    this->tttr = tttr;
    set_time_axis_calibration(tttr->get_header()->get_macro_time_resolution());

    resize(tttr->size());
    for(size_t i = 0; i < size(); i++) times[i] = tttr->macro_times[i];

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
    // use the tttr
    if(tttr != nullptr){
        int n = tttr->size();
        if(routing_channels.empty()){
            routing_channels.assign(tttr->routing_channels, tttr->routing_channels + n);
        }
        if(micro_times.empty()){
            micro_times.assign(tttr->micro_times, tttr->micro_times + n);
        }
    }
    // make sure that the weights are of the right size
    resize(micro_times.size());
    // lookup the weights ch
    size_t idx = 0;
    for(auto r: routing_channels){
        auto m = micro_times[idx];
        weights[idx] = filter.at(r)[m];
        idx++;
    }
}
