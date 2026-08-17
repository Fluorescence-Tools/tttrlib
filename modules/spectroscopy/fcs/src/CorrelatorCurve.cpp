// SPDX-License-Identifier: BSD-3-Clause
#include "CorrelatorCurve.h"
#include "Verbose.h"

void CorrelatorCurve::get_x_axis(double **output, int *n_output){
    (*n_output) = (int) settings.get_ncorr();
    auto* t = (double*) malloc((*n_output) * sizeof(double));
    for(int i = 0; i<(*n_output); i++){
        t[i] = this->x_axis[i] * settings.macro_time_duration;
    }
    *output = t;
}

void CorrelatorCurve::set_x_axis(std::vector<long long unsigned int> input){
    this->x_axis = input;
}

void CorrelatorCurve::get_corr(double** output, int* n_output){
    (*n_output) = settings.get_ncorr();
    auto* t = (double *) malloc((*n_output) * sizeof(double));
    for(int i = 0; i < (*n_output); i++){
        t[i] = this->correlation[i];
    }
    *output = t;
}

void CorrelatorCurve::get_corr_normalized(double** output, int* n_output){
    (*n_output) = (int) settings.get_ncorr();
    auto* t = (double *) malloc((*n_output) * sizeof(double));
    for(int i = 0; i < (*n_output); i++) t[i] = corr_normalized[i];
    *output = t;
}

void CorrelatorCurve::update_axis(){
    resize(settings.get_ncorr());
if (is_verbose()) {
    std::clog << "-- Updating x-axis..." << std::endl;
    std::clog << "-- n_casc: " << settings.n_casc << std::endl;
    std::clog << "-- n_bins: " << settings.n_bins << std::endl;
    std::clog << "-- n_corr: " << settings.get_ncorr() << std::endl;
}
    x_axis[0] = 0;
    if (settings.correlation_method == "felekyan") {
        // Felekyan's block structure (Felekyan et al. 2005): block 0 is
        // n_bins lags at spacing 1, block k >= 1 is n_bins lags at spacing
        // 2^(k-1), contiguous. That is what ccf_felekyan counts (pw =
        // 2^(k-1), one coarsening per block after the first) and what
        // normalize_ccf_felekyan divides by; labelling those bins with the
        // wahl axis below (spacing 2^k) put every block-k label at up to
        // twice its true lag -- caught by the A/B against a closed-form
        // G(tau), 2026-08-17.
        uint64_t step = 1;
        for (size_t j = 1; j < size(); j++) {
            x_axis[j] = x_axis[j-1] + step;
            const size_t block = j / settings.n_bins;   // block about to start
            if (j % settings.n_bins == 0 && block >= 2) step <<= 1;
        }
        return;
    }
    // multi-tau axis: step doubles every n_bins bins.  Computing with bit
    // shifts instead of std::pow avoids the transcendental per element.
    uint64_t step = 1;
    for(size_t j=1; j < size(); j++){
        x_axis[j] = x_axis[j-1] + step;
        if (j % settings.n_bins == 0) step <<= 1;
    }
}
