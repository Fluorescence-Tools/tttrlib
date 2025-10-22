#include "TTTRRange.h"

TTTRRange::TTTRRange(const TTTRRange& p2){
    if(p2._tttr_indices){
        _tttr_indices = std::make_unique<indices_set>(*p2._tttr_indices);
    }
}

TTTRRange::TTTRRange(int start, int stop){
    _tttr_indices = std::make_unique<indices_set>();
    _tttr_indices->push_back(start);
    if (stop != start) _tttr_indices->push_back(stop);
}

std::vector<int> TTTRRange::get_tttr_indices() const{
    // Use move semantics for RVO optimization
    // Reserve exact size to avoid reallocation
    std::vector<int> v;
    if(_tttr_indices){
        v.reserve(_tttr_indices->size());
        v.assign(_tttr_indices->begin(), _tttr_indices->end());
    }
    return v;  // RVO/NRVO will elide the copy
}

double TTTRRange::compute_mean_lifetime(
        std::vector<int> &tttr_indices,
        TTTR *tttr_data,
        int minimum_number_of_photons,
        TTTR *tttr_irf, double m0_irf, double m1_irf,
        double dt,
        std::vector<double> *background, double m0_bg, double m1_bg,
        double background_fraction
) {
    return TTTR::compute_mean_lifetime(
            tttr_data, tttr_irf,
            m0_irf, m1_irf,
            &tttr_indices,
            dt, minimum_number_of_photons,
            background, m0_bg, m1_bg,
            background_fraction
    );
}

double TTTRRange::get_mean_lifetime(
        TTTR *tttr_data,
        int minimum_number_of_photons,
        TTTR *tttr_irf,
        double m0_irf, double m1_irf,
        double dt,
        std::vector<double> *background, double m0_bg, double m1_bg,
        double background_fraction
) {
    // Avoid double copy: get_tttr_indices() already creates a vector
    auto t = get_tttr_indices();
    return TTTRRange::compute_mean_lifetime(
            t,
            tttr_data,
            minimum_number_of_photons,
            tttr_irf,
            m0_irf, m1_irf,
            dt,
            background, m0_bg, m1_bg,
            background_fraction
    );
}

