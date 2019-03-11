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

#include "../include/correlate.h"


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

    start_1 = (start_1 >= 0) ? start_1 : 0;
    end_1 = std::min(nt1, end_1);

    start_2 = (start_2 >= 0) ? start_2 : 0;
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
        std::vector<uint64_t> &x_axis, std::vector<double> &corr,
        size_t n_bins
){
    double cr1 = (double) np1 / (double) dt1;
    double cr2 = (double) np2 / (double) dt2;
    uint64_t pw;
    double t_corr;

    for(int j=0; j<x_axis.size(); j++){
        pw = (uint64_t) std::pow(2.0, (int) (float(j-1) / n_bins));
        t_corr = (dt1 < dt2 - x_axis[j]) ? dt1 : dt2 - x_axis[j];
        corr[j] /= pw;
        corr[j] /= (cr1 * cr2 * t_corr);
        x_axis[j] = x_axis[j] / pw * pw;
    }
}


void make_fine_times(unsigned long long *t, unsigned int* tac, size_t n_times){
    for(size_t i=0; i < n_times; i++){
        t[i] += tac[i];
    }
}

