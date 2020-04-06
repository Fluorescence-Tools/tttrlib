/****************************************************************************
 * Copyright (C) 2020 by Thomas-Otavio Peulen                               *
 *                                                                          *
 * This file is part of the library tttrlib.                                *
 *                                                                          *
 *   tttrlib is free software: you can redistribute it and/or modify it     *
 *   under the terms of the MIT License.                                    *
 *                                                                          *
 *   tttrlib is distributed in the hope that it will be useful,             *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                   *
 *                                                                          *
 ****************************************************************************/

#include <include/correlation.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))


void Correlator::update_axis(){
    n_corr = n_casc * n_bins + 1;

    x_axis.resize(n_corr);
    corr.resize(n_corr);

    corr_normalized.resize(n_corr);
    x_axis_normalized.resize(n_corr);

    x_axis[0] = 0;
    for(unsigned int j=1; j <= n_casc*n_bins; j++){
        x_axis[j] = x_axis[j-1] + (uint64_t) std::pow(2, std::floor( (j-1)  / n_bins));
        x_axis_normalized[j] = x_axis[j];
        corr[j] = 0;
    }
}


void Correlator::set_macrotimes(
        unsigned long long  *t1, int n_t1,
        unsigned long long  *t2, int n_t2
){
    is_valid = false;

    Correlator::t1 = t1;
    Correlator::n_t1 = (size_t) n_t1;

    Correlator::t2 = t2;
    Correlator::n_t2 = (size_t) n_t2;

    dt1 = t1[n_t1 - 1] - t1[0];
    dt2 = t2[n_t2 - 1] - t2[0];
    maximum_macro_time = std::max(dt1, dt2);
}


void Correlator::set_events(
        unsigned long long  *t1, int n_t1,
        double* weight_ch1, int n_weights_ch1,
        unsigned long long  *t2, int n_t2,
        double* weight_ch2, int n_weights_ch2
){
    Correlator::t1 = t1;
    Correlator::w1 = weight_ch1;
    Correlator::n_t1 = (size_t) n_t1;

    Correlator::t2 = t2;
    Correlator::w2 = weight_ch2;
    Correlator::n_t2 = (size_t) n_t2;

    dt1 = t1[n_t1 - 1] - t1[0];
    dt2 = t2[n_t2 - 1] - t2[0];
    maximum_macro_time = std::max(dt1, dt2);
}


void Correlator::set_weights(
        double* weight_ch1, int n_weights_ch1,
        double* weight_ch2, int n_weights_ch2
){
    is_valid = false;

    Correlator::w1 = weight_ch1;
    Correlator::n_t1 = (size_t) n_t1;

    Correlator::w2 = weight_ch2;
    Correlator::n_t2 = (size_t) n_t2;

}



void Correlator::normalize(){
#if VERBOSE
    std::clog << "-- Normalizing correlation curve..." << std::endl;
#endif
    double np1 = std::accumulate(w1, w1+n_t1, 0.0);
    double np2 = std::accumulate(w2, w2+n_t2, 0.0);
#if VERBOSE
    std::clog << "-- Number sum of weights (Ch1): " << np1 << std::endl;
    std::clog << "-- Number sum of weights (Ch2): " << np2 << std::endl;
#endif

#if VERBOSE
    std::clog << "-- Maximum time (Ch1): " << dt1 << std::endl;
    std::clog << "-- Maximum time (Ch2): " << dt2 << std::endl;
    std::clog << "-- Maximum time: " << maximum_macro_time << std::endl;
#endif
    // compute count rates for normalization
    double cr1 = (double) np1 / dt1;
    double cr2 = (double) np2 / dt2;
#if VERBOSE
    std::clog << "-- Count rate in correlation channel 1: " << cr1 << std::endl;
    std::clog << "-- Count rate in correlation channel 2: " << cr2 << std::endl;
#endif

    for(size_t i=0; i<x_axis_normalized.size(); i++) x_axis_normalized[i] = x_axis[i];
    for(size_t i=0; i<corr_normalized.size(); i++) corr_normalized[i] = corr[i];

    if(correlation_method == "peulen"){
        peulen::correlation_normalize(
                np1, dt1,
                np2, dt2,
                x_axis_normalized,
                corr_normalized,
                n_bins
        );
    } else if (correlation_method == "lamb") {
        lamb::normalize(
                x_axis, corr,
                x_axis_normalized, corr_normalized,
                cr1, cr2,
                n_bins, n_casc, maximum_macro_time
                );
    }
}


void Correlator::run(){
#if VERBOSE
    std::clog << "CORRELATOR::RUN" << std::endl;
    std::clog << "-- Correlation mode: " << correlation_method << std::endl;
    std::clog << "-- Filling correlation vectors with zero: " << correlation_method << std::endl;
#endif
    if(is_valid){
#if VERBOSE
        std::clog << "CORRELATOR::RUN" << std::endl;
        std::clog << "-- results are already valid." << std::endl;
#endif
        return;
    }
    std::fill(corr.begin(), corr.end(), 0.0);
    if (correlation_method == "peulen"){
        peulen::correlation_full(
                n_casc, n_bins,
                x_axis, corr,
                t1, w1, n_t1,
                t2, w2, n_t2
        );
    } else if (correlation_method == "lamb") {
        lamb::CCF(
                (const unsigned long long*) t1,
                (const unsigned long long*) t2, w1, w2,
                (unsigned int) n_bins,
                (unsigned int) n_casc,
                (unsigned int) n_t1,
                (unsigned int) n_t2,
                x_axis.data(),
            corr.data()
        );
    } else{
        std::cerr << "WARNING: Correlation mode not recognized!" << std::endl;
    }
    normalize();
}


void Correlator::get_corr_normalized(
        double** corr,
        int* n_out
        ){
    if(!is_valid){
        run();
    }
    (*n_out) = (int) corr_normalized.size();
    auto* t = (double *) malloc((*n_out) * sizeof(double));
    for(int i = 0; i < (*n_out); i++){
        t[i] = corr_normalized[i];
    }
    *corr = t;
}


void Correlator::get_x_axis_normalized(
        unsigned long long** x_axis,
        int* n_out
        ){
    (*n_out) = (int) x_axis_normalized.size();
    auto* t = (unsigned long long*) malloc((*n_out) * sizeof(unsigned long long));
    for(int i = 0; i<(*n_out); i++){
        t[i] = x_axis_normalized[i];
    }
    *x_axis = t;
}


void Correlator::get_corr(
        double** corr,
        int* n_out
        ){
    (*n_out) = (int) this->corr.size();
    auto* t = (double *) malloc((*n_out) * sizeof(double));
    for(int i = 0; i < (*n_out); i++){
        t[i] = this->corr[i];
    }
    *corr = t;
}


void Correlator::get_x_axis(
        unsigned long long** x_axis,
        int* n_out
        ){
    (*n_out) = (int) this->x_axis.size();
    auto* t = (unsigned long long*) malloc((*n_out) * sizeof(unsigned long long));
    for(int i = 0; i<(*n_out); i++){
        t[i] = this->x_axis[i];
    }
    *x_axis = t;
}


void Correlator::set_microtimes(
        unsigned int* tac_1, unsigned int n_tac_1,
        unsigned int* tac_2, int unsigned n_tac_2,
        unsigned int n_tac
        ){
    is_valid = false;
    if(!is_fine){
        peulen::make_fine_times(t1, (unsigned int) n_t1, tac_1, n_tac);
        peulen::make_fine_times(t2, (unsigned int) n_t2, tac_2, n_tac);
        is_fine = true;
    }
}

