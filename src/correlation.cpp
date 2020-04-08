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


Correlator::Correlator(TTTR *tttr,
        std::string method,
        int n_bins,
        int n_casc
) {
#if VERBOSE
    std::clog << "CORRELATOR" << std::endl;
#endif
    this->n_bins = n_bins;
    this->n_casc = n_casc;
    update_axis();
    if (tttr != nullptr) {
#if VERBOSE
        std::clog << "-- Passed a TTTR object" << std::endl;
#endif
        this->tttr = tttr;
        auto header = tttr->get_header();
        // the macro time is in nanoseconds
        set_time_axis_calibration(
                header.macro_time_resolution / 1e6
        );
    }
    set_correlation_method(method);
}


void Correlator::update_axis(){
    n_corr = n_casc * n_bins + 1;
#if VERBOSE
    std::clog << "-- Updating x-axis..." << std::endl;
    std::clog << "-- n_casc: " << n_casc << std::endl;
    std::clog << "-- n_bins: " << n_bins << std::endl;
    std::clog << "-- n_corr: " << n_corr << std::endl;
#endif
    x_axis.resize(n_corr);
    corr.resize(n_corr);
    corr_normalized.resize(n_corr);
    x_axis_normalized.resize(n_corr);
    x_axis[0] = 0;
    for(unsigned int j=1; j <= n_casc*n_bins; j++){
        x_axis[j] = x_axis[j-1] + (uint64_t) std::pow(2, std::floor( (j-1)  / n_bins));
        x_axis_normalized[j] = x_axis[j];
    }
}


void Correlator::set_macrotimes(
        unsigned long long  *t1v, int n_t1v,
        unsigned long long  *t2v, int n_t2v
){
#if VERBOSE
    std::clog << "-- Setting macro times..." << std::endl;
    std::clog << "-- n_t1v, n_t2v: " << n_t1v << "," << n_t2v << std::endl;
#endif
    is_valid = false;
    free(t1); free(t2);
    t1 = (unsigned long long*) malloc(sizeof(unsigned long long) * n_t1v);
    t2 = (unsigned long long*) malloc(sizeof(unsigned long long) * n_t2v);
    for(int i=0; i<n_t1v; i++) t1[i] = t1v[i];
    for(int i=0; i<n_t2v; i++) t2[i] = t2v[i];
    n_t1 = (size_t) n_t1v;
    n_t2 = (size_t) n_t2v;
    compute_dt();
}

void Correlator::set_weights(
        double* weight_ch1, int n_weights_ch1,
        double* weight_ch2, int n_weights_ch2
){
#if VERBOSE
    std::clog << "-- Setting weights..." << std::endl;
    std::clog << "-- n_weights_ch1, n_weights_ch2: " <<
    n_weights_ch1 << "," << n_weights_ch2 << std::endl;
#endif
    is_valid = false;
    free(w1); free(w2);
    w1 = (double*) malloc(sizeof(double) * n_weights_ch1);
    w2 = (double*) malloc(sizeof(double) * n_weights_ch2);
    for(int i=0; i<n_weights_ch1; i++) w1[i] = weight_ch1[i];
    for(int i=0; i<n_weights_ch2; i++) w2[i] = weight_ch2[i];
    n_t1 = (size_t) n_weights_ch1;
    n_t2 = (size_t) n_weights_ch2;
}

void Correlator::set_events(
        unsigned long long  *t1, int n_t1,
        double* weight_ch1, int n_weights_ch1,
        unsigned long long  *t2, int n_t2,
        double* weight_ch2, int n_weights_ch2
){
    set_macrotimes(t1, n_t1, t2, n_t2);
    set_weights(weight_ch1, n_weights_ch1, weight_ch2, n_weights_ch2);
}

void Correlator::normalize(){
#if VERBOSE
    std::clog << "-- Normalizing correlation curve..." << std::endl;
#endif
    double np1 = std::accumulate(w1, w1+n_t1, 0.0);
    double np2 = std::accumulate(w2, w2+n_t2, 0.0);
#if VERBOSE
    std::clog << "-- Sum of weights (Ch1): " << np1 << std::endl;
    std::clog << "-- Sum of weights (Ch2): " << np2 << std::endl;
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

    if(correlation_method == "default"){
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
    std::clog << "-- Running correlator..." << std::endl;
    std::clog << "-- Correlation mode: " << correlation_method << std::endl;
    std::clog << "-- Filling correlation vectors with zero." << std::endl;
#endif
    if(is_valid){
#if VERBOSE
        std::clog << "CORRELATOR::RUN" << std::endl;
        std::clog << "-- results are already valid." << std::endl;
#endif
        return;
    }
    if((t1 == nullptr) || (t2== nullptr) || (w1 == nullptr) || (w2 == nullptr)){
#if VERBOSE
        std::clog << "-- No time or weight arrays set." << std::endl;
#endif
        if(tttr != nullptr){
#if VERBOSE
            std::clog << "-- Computing ACF for all macro times. " << std::endl;
#endif
            // copy to array and make sure everything is correctly setup.
            // cannot use setter here (pointing in circle)
            //set_macrotimes(t1, n_t1, t2, n_t2);
            int nt1; int nt2;
            tttr->get_macro_time(&t1, &nt1);
            tttr->get_macro_time(&t2, &nt2);
            n_t1 = nt1; n_t2 = nt2;
            w1 = (double *) malloc(sizeof(double) * n_t1);
            w2 = (double *) malloc(sizeof(double) * n_t2);
            for(int i=0; i<n_t1;i++) w1[i]=1.0;
            for(int i=0; i<n_t2;i++) w2[i]=1.0;
            is_valid = false;
            compute_dt();
        }
    }
    std::fill(corr.begin(), corr.end(), 0.0);
    if (correlation_method == "default"){
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


void Correlator::get_corr_normalized(double** output, int* n_output){
    if(!is_valid){
        run();
    }
    (*n_output) = n_corr;
    auto* t = (double *) malloc((*n_output) * sizeof(double));
    for(int i = 0; i < (*n_output); i++){
        t[i] = corr_normalized[i];
    }
    *output = t;
}


void Correlator::get_x_axis_normalized(double** output, int* n_output){
    (*n_output) = (int) n_corr;
    auto* t = (double*) malloc((*n_output) * sizeof(double));
    double tc = time_axis_calibration;
    for(int i = 0; i<(*n_output); i++){
        t[i] = x_axis_normalized[i] * tc;
    }
    *output = t;
}


void Correlator::get_corr(
        double** corr,
        int* n_out
        ){
    (*n_out) = n_corr;
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
    (*n_out) = (int) n_corr;
    auto* t = (unsigned long long*) malloc((*n_out) * sizeof(unsigned long long));
    for(int i = 0; i<(*n_out); i++){
        t[i] = this->x_axis[i];
    }
    *x_axis = t;
}


void Correlator::set_microtimes(
        unsigned int* tac_1, unsigned int n_tac_1,
        unsigned int* tac_2, unsigned int n_tac_2,
        unsigned int n_tac
        ){
#if VERBOSE
    std::clog << "-- Setting micro times..." << std::endl;
#endif
    is_valid = false;
    peulen::make_fine_times(t1, n_tac_1, tac_1, n_tac);
    peulen::make_fine_times(t2, n_tac_2, tac_2, n_tac);
    is_fine = true;
    set_time_axis_calibration(time_axis_calibration /= n_tac);
    compute_dt();
}

void Correlator::compute_dt(){
    dt1 = t1[n_t1 - 1] - t1[0];
    dt2 = t2[n_t2 - 1] - t2[0];
    maximum_macro_time = std::max(dt1, dt2);
#if VERBOSE
    std::clog << "-- Maximum time (Ch1): " << dt1 << std::endl;
    std::clog << "-- Maximum time (Ch2): " << dt2 << std::endl;
    std::clog << "-- Maximum time: " << maximum_macro_time << std::endl;
#endif
}

void Correlator::set_tttr(
        TTTR* tttr_1, TTTR* tttr_2,
        bool make_fine
){
    is_fine = false;
    is_valid = false;
    tttr = tttr_1;
    set_time_axis_calibration(
            tttr_1->get_header().macro_time_resolution / 1e6
    );
    int nt1; int nt2;
    tttr_1->get_macro_time(&t1, &nt1);
    tttr_2->get_macro_time(&t2, &nt2);
    n_t1 = nt1; n_t2 = nt2;
    // set weights
    w1 = (double*) malloc(sizeof(double) * n_t1);
    w2 = (double*) malloc(sizeof(double) * n_t2);
    for(int i=0;i<n_t1;i++) w1[i] = 1.0;
    for(int i=0;i<n_t2;i++) w2[i] = 1.0;
    compute_dt();
    if(make_fine){
        int n_tac = MIN(
                tttr_1->get_number_of_micro_time_channels(),
                tttr_2->get_number_of_micro_time_channels()
                );
        unsigned int* tac_1; int n_tac1;
        tttr_1->get_micro_time(&tac_1, &n_tac1);
        unsigned int* tac_2; int n_tac2;
        tttr_2->get_micro_time(&tac_2, &n_tac2);
        set_microtimes(tac_1, n_tac1, tac_2, n_tac2, n_tac);
    }
}
