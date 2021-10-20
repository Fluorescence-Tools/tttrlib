#include "DecayPhasor.h"


std::vector<double> DecayPhasor::compute_phasor(
        unsigned short *micro_times,
        std::vector<int> &idxs,
        double frequency,
        int minimum_number_of_photons,
        double g_irf,
        double s_irf
){
    double factor = (2. * frequency * M_PI);
    std::vector<double> re{-1, -1};
    double g_sum = 0.0;
    double s_sum = 0.0;
    double sum = (double) idxs.size();
    for(auto &idx: idxs){
        auto mt = micro_times[idx];
        g_sum += std::cos(mt * factor);
        s_sum += std::sin(mt * factor);
    }
    if(sum > minimum_number_of_photons){
        double g_exp = g_sum / std::max(1., sum);
        double s_exp = s_sum / std::max(1., sum);
        re[0] = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
        re[1] = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
    }
    return re;
}


std::vector<double> DecayPhasor::compute_phasor_all(
        unsigned short* microtimes, int n_microtimes,
        double frequency
){
    double factor = (2. * frequency * M_PI);
    double sum = n_microtimes;
    double g_sum = 0.0; double s_sum = 0.0;
    for(int i=0;i<n_microtimes;i++){
        g_sum += std::cos(microtimes[i] * factor);
        s_sum += std::sin(microtimes[i] * factor);
    }
    std::vector<double> re{g_sum / sum, s_sum / sum};
    return re;
}


double DecayPhasor::g(
            double g_irf, double s_irf,
            double g_exp, double s_exp
    ) {
        return 1. / (g_irf * g_irf + s_irf * s_irf) * (g_irf * g_exp + s_irf * s_exp);
    }


double DecayPhasor::s(
        double g_irf, double s_irf,
        double g_exp, double s_exp
) {
    return 1. / (g_irf * g_irf + s_irf * s_irf) * (g_irf * s_exp - s_irf * g_exp);
}

