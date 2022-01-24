#include "DecayPhasor.h"


std::vector<double> DecayPhasor::compute_phasor(
        unsigned short* microtimes, int n_microtimes,
        double frequency,
        int minimum_number_of_photons,
        double g_irf,
        double s_irf,
        std::vector<int>* idxs
){
    double factor = (2. * frequency * M_PI);
    std::vector<double> re{-1, -1};
    double g_sum = 0.0;
    double s_sum = 0.0;
    double sum;
    if(idxs == nullptr){
        sum = n_microtimes;
        for(int i=0;i<n_microtimes;i++){
            g_sum += std::cos(microtimes[i] * factor);
            s_sum += std::sin(microtimes[i] * factor);
        }
    } else{
        sum = (double) idxs->size();
        for(auto &idx: *idxs){
            auto mt = microtimes[idx];
            g_sum += std::cos(mt * factor);
            s_sum += std::sin(mt * factor);
        }
    }
    if(sum > minimum_number_of_photons){
        double g_exp = g_sum / std::max(1., sum);
        double s_exp = s_sum / std::max(1., sum);
        re[0] = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
        re[1] = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
    }
    return re;
}


std::vector<double> DecayPhasor::compute_phasor_bincounts(
        std::vector<int> &bincounts,
        double frequency,
        int minimum_number_of_photons,
        double g_irf, double s_irf
){
    double factor = (2. * frequency * M_PI);
    std::vector<double> re{-1, -1};
    double g_sum = 0.0;
    double s_sum = 0.0;
    double sum = (double) std::accumulate(bincounts.begin(), bincounts.end(), 0.0);
    int mt = 0;
    for(auto &count: bincounts){
        g_sum += count * std::cos(mt * factor);
        s_sum += count * std::sin(mt * factor);
        mt++;
    }
    if(sum > minimum_number_of_photons){
        double g_exp = g_sum / std::max(1., sum);
        double s_exp = s_sum / std::max(1., sum);
        re[0] = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
        re[1] = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
    }
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

