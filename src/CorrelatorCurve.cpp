#include "CorrelatorCurve.h"

void CorrelatorCurve::get_x_axis(double **output, int *n_output){
    (*n_output) = (int) settings.get_ncorr();
    auto* t = (double*) malloc((*n_output) * sizeof(double));
    for(int i = 0; i<(*n_output); i++){
        t[i] = this->x_axis[i] * settings.macro_time_duration;
    }
    *output = t;
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
#if VERBOSE_TTTRLIB
    std::clog << "-- Updating x-axis..." << std::endl;
    std::clog << "-- n_casc: " << settings.n_casc << std::endl;
    std::clog << "-- n_bins: " << settings.n_bins << std::endl;
    std::clog << "-- n_corr: " << settings.get_ncorr() << std::endl;
#endif
    x_axis[0] = 0;
    for(size_t j=1; j < size(); j++){
        x_axis[j] = x_axis[j-1] + (uint64_t) std::pow(2, std::floor( (j-1)  / settings.n_bins));
    }
}

