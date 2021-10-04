#include "TTTRRange.h"

TTTRRange::TTTRRange(const TTTRRange& p2){
    _start = p2._start;
    _stop = p2._stop;
    _start_time = p2._start_time;
    _stop_time = p2._stop_time;
    _tttr_indices.reserve(p2._tttr_indices.size());
    for(auto &v: p2._tttr_indices){
        _tttr_indices.emplace_back(v);
    }
}

TTTRRange::TTTRRange(
        size_t start,
        size_t stop,
        unsigned int start_time,
        unsigned int stop_time,
        TTTRRange* other,
        int pre_reserve
) {
    if(other != nullptr){
        this->_start = other->_start;
        this->_stop = other->_stop;
        this->_start_time = other->_start_time;
        this->_stop_time = other->_stop_time;
    } else {
        this->_start = start;
        this->_stop = stop;
        this->_start_time = start_time;
        this->_stop_time = stop_time;
    }
    _tttr_indices.reserve(pre_reserve);
}


double TTTRRange::get_mean_microtime(TTTR* tttr_data, int minimum_number_of_photons){
    auto v = _tttr_indices;
    // calculate the mean arrival time iteratively
    double value = 0.0;
    if (v.size() > minimum_number_of_photons){
        double i = 1.0;
        for(auto event_i: v){
            value = value + 1. / (i + 1.) * (double) (tttr_data->micro_times[event_i] - value);
            i++;
        }
    }
    return value;
}


double TTTRRange::compute_mean_lifetime(
        std::vector<int> &tttr_indices,
        TTTR *tttr_data,
        int minimum_number_of_photons,
        TTTR *tttr_irf,
        double m0_irf,
        double m1_irf,
        double dt
) {
    if (tttr_irf != nullptr) {
        unsigned short *micro_times_irf; int n_micro_times_irf;
        micro_times_irf = tttr_irf->micro_times;
        n_micro_times_irf = tttr_irf->n_valid_events;
        // number of photons
        m0_irf = n_micro_times_irf;
        // sum of photon arrival times
        m1_irf = std::accumulate(micro_times_irf,micro_times_irf + n_micro_times_irf,0.0);
    }
    double lt = -1.0;
    double mu0 = 0.0; // total number of photons
    double mu1 = 0.0; // sum of photon arrival times
    mu0 += (double) tttr_indices.size();
    for (auto &vi: tttr_indices)
        mu1 += tttr_data->micro_times[vi];
    double g1 = mu0 / m0_irf;
    double g2 = (mu1 - g1 * m1_irf) / m0_irf;
    if (mu0 > minimum_number_of_photons) {
        lt = g2 / g1 * dt;
    }
    return lt;
}


double TTTRRange::get_mean_lifetime(
        TTTR *tttr_data,
        int minimum_number_of_photons,
        TTTR *tttr_irf,
        double m0_irf, double m1_irf,
        double dt
        ) {
    return TTTRRange::compute_mean_lifetime(
            _tttr_indices,
            tttr_data,
            minimum_number_of_photons,
            tttr_irf,
            m0_irf, m1_irf,
            dt
    );
}
