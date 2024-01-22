#include "TTTRRange.h"

TTTRRange::TTTRRange(const TTTRRange& p2){
    _tttr_indices = p2._tttr_indices;
}

TTTRRange::TTTRRange(int start, int stop){
    _tttr_indices.insert(start);
    _tttr_indices.insert(stop);
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
    auto s = get_tttr_indices();
    std::vector<int> t(s.begin(), s.end());
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

