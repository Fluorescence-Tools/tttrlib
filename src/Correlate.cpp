/****************************************************************************
 * Copyright (C) 2019 by Thomas-Otavio Peulen                               *
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

#include "include/Correlate.h"


void coarsen(
        unsigned long long * t, double* w, size_t nt
        ){

    t[0] /= 2;
    for(size_t i=1; i < nt; i++){
        t[i] /= 2;
        if(t[i] == t[i-1]){
            w[i] += w[i-1];
            w[i-1] = 0;
        }
    }
}


void correlate(
        size_t start_1, size_t end_1,
        size_t start_2, size_t end_2,
        size_t i_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1, size_t nt1,
        const unsigned long long *t2, const double *w2, size_t nt2
){

    size_t i1, i2, p, index;
    size_t edge_l, edge_r;

    start_1 = (start_1 > 0) ? start_1 : 0;
    end_1 = std::min(nt1, end_1);

    start_2 = (start_2 > 0) ? start_2 : 0;
    end_2 = std::min(nt2, end_2);

    auto scale = (unsigned long long) std::pow(2.0, i_casc);

    p = start_2;
    for (i1 = start_1; i1 < end_1; i1++) {
        if (w1[i1] != 0) {
            edge_l = t1[i1] + taus[i_casc * n_bins] / scale;
            edge_r = edge_l + n_bins;
            for(i2 = p; (i2 < end_2) && (t2[i2] <= edge_r); i2++) {
                if(w2[i2] != 0){
                    if (t2[i2] > edge_l) {
                        index = t2[i2] - edge_l + i_casc * n_bins;
                        corr[index] += (w1[i1] * w2[i2]);
                    } else{
                        p++;
                    }
                }
            }
        }
    }
}


void normalize_correlation(
        double np1, uint64_t dt1,
        double np2, uint64_t dt2,
        std::vector<unsigned long long> &x_axis, std::vector<double> &corr,
        size_t n_bins
){
    double cr1 = (double) np1 / (double) dt1;
    double cr2 = (double) np2 / (double) dt2;
    uint64_t pw;
    double t_corr;

    for(int j=0; j<x_axis.size(); j++){
        pw = (uint64_t) std::pow(2.0, (int) (float(j-1) / n_bins));
        t_corr = (dt1 < dt2 - x_axis[j]) ? (double) dt1 : (double) (dt2 - x_axis[j]);
        corr[j] /= pw;
        corr[j] /= (cr1 * cr2 * t_corr);
        x_axis[j] = x_axis[j] / pw * pw;
    }
}


void make_fine_times(unsigned long long *t, unsigned int n_times, unsigned int* tac, unsigned int n_tac){
    for(size_t i=0; i < n_times; i++){
        t[i] = t[i] * n_tac + tac[i];
    }
}



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

void Correlator::normalize(){
    double np1 = std::accumulate(w1, w1+n_t1, 0.0);
    double np2 = std::accumulate(w2, w2+n_t2, 0.0);
    for(size_t i=0; i<x_axis_normalized.size(); i++) x_axis_normalized[i] = x_axis[i];
    for(size_t i=0; i<corr_normalized.size(); i++) corr_normalized[i] = corr[i];

    normalize_correlation(
            np1, dt1,
            np2, dt2,
            x_axis_normalized,
            corr_normalized,
            n_bins
    );
}


void Correlator::run(){

    for(size_t i_casc=0; i_casc<n_casc; i_casc++){
        tf::Taskflow tf;

        correlate(
                0, n_t1,
                0, n_t2,
                i_casc, n_bins,
                x_axis, corr,
                t1, w1, n_t1,
                t2, w2, n_t2
        );
        auto taskA = tf.emplace([this](){coarsen(t1, w1, n_t1);});
        auto taskB = tf.emplace([this](){coarsen(t2, w2, n_t2);});

        // this seems to be for small tasks slower than without parallelization
        tf.wait_for_all();

    }
    // calculate a normalized correlation
    normalize();
}


void Correlator::get_corr_normalized(double** corr, int* n_out){
    (*n_out) = (int) corr_normalized.size();
    auto* t = (double *) malloc((*n_out) * sizeof(double));
    for(int i = 0; i < (*n_out); i++){
        t[i] = corr_normalized[i];
    }
    *corr = t;
}


void Correlator::get_x_axis_normalized(unsigned long long** x_axis, int* n_out){
    (*n_out) = (int) x_axis_normalized.size();
    auto* t = (unsigned long long*) malloc((*n_out) * sizeof(unsigned long long));
    for(int i = 0; i<(*n_out); i++){
        t[i] = x_axis_normalized[i];
    }
    *x_axis = t;
}


void Correlator::get_corr(double** corr, int* n_out){
    (*n_out) = (int) this->corr.size();
    auto* t = (double *) malloc((*n_out) * sizeof(double));
    for(int i = 0; i < (*n_out); i++){
        t[i] = this->corr[i];
    }
    *corr = t;
}


void Correlator::get_x_axis(unsigned long long** x_axis, int* n_out){
    (*n_out) = (int) this->x_axis.size();
    auto* t = (unsigned long long*) malloc((*n_out) * sizeof(unsigned long long));
    for(int i = 0; i<(*n_out); i++){
        t[i] = this->x_axis[i];
    }
    *x_axis = t;
}


void Correlator::make_fine(unsigned int* tac_1, unsigned int n_tac_1, unsigned int* tac_2, int unsigned n_tac_2, unsigned int n_tac){
    if(!is_fine){
        make_fine_times(t1, (unsigned int) n_t1, tac_1, n_tac);
        make_fine_times(t2, (unsigned int) n_t2, tac_2, n_tac);
        is_fine = true;
    }
}

