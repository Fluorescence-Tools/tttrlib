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

#include "include/pda.h"


////////////////////////////////// convolution with background ////////////////////////////////////


/*!
 * Histogram routine that
 * @tparam T
 * @param input the SgSr matrix that contains P(Sgreen, Sred)
 * @param Nmax max N photons
 * @param histogram_param "Histogram" cluster from Tatiana
 * @param exp_param "Experimental" cluster from Tatiana
 * @param histogram_x histogram X axis (return)
 * @param histogram_y histogram itself (return)
 */
void Pda::sgsr_histogram(
        double **histogram_x, int *n_histogram_x,
        double **histogram_y, int *n_histogram_y
) {
    double xminlog = log(xmin);
    auto Nbinsf = (double) NBins;

    (*n_histogram_x) = NBins;
    (*n_histogram_y) = NBins;
    *histogram_x = (double*) calloc(sizeof(double), NBins);
    *histogram_y = (double*) calloc(sizeof(double), NBins);

    int green, red, firstred, bin;
    // build histogram
    for (bin = 0; bin < NBins; bin++) (*histogram_y)[bin] = 0.;
    if(log_x) {
        double bin_width = (log(xmax) - xminlog) / ((double) NBins - 1);
        double inverse_bin_width = 1. / bin_width;
        double xmincorr = log(xmin) - 0.5 * bin_width;
        // histogram X
        for (bin = 0; bin < NBins; bin++)
            (*histogram_x)[bin] = exp(xminlog + bin_width * (double) bin);
        // histogram Y
        for (green = 0; green <= Nmax; green++) {
            green > Nmin ? firstred = 0 : firstred = Nmin - green;
            for (red = firstred; red <= Nmax - green; red++) {
                double x = log((double) green / (double) red); // SgSgr
                double binf = floor((x - xmincorr) * inverse_bin_width);
                bin = (int) binf;
                if ((binf < Nbinsf) && (binf >= 0.)) (*histogram_y)[bin] += SgSr[green * (Nmax + 1) + red];
            }
        }
    } else {        // not log
        double bin_width = (xmax - xmin) / ((double) NBins - 1.);
        double inverse_bin_width = 1. / bin_width;
        double xmincorr = xmin - 0.5 * bin_width;
        // histogram X
        for (bin = 0; bin < NBins; bin++)
            (*histogram_x)[bin] = xmin + bin_width * (double) bin;
        // histogram Y
        for (green = 0; green <= Nmax; green++) {
            green > Nmin ? firstred = 0 : firstred = Nmin - green;
            for (red = firstred; red <= Nmax - green; red++) {
                double x = (double) green / (double) red; // SgSgr;
                double binf = floor((x - xmincorr) * inverse_bin_width);
                bin = (int) binf;
                if ((binf < Nbinsf) && (binf >= 0.)) (*histogram_y)[bin] += SgSr[green * (Nmax + 1) + red];
            }
        }
    }
}

void PdaFunctions::conv_pF(
        double *SgSr,
        double *FgFr,
        unsigned int Nmax,
        double Bg,
        double Br
) {

    auto *tmp = new double[(Nmax + 1) * (Nmax + 1)]();
    // green and red background
    auto *bg = new double[Nmax + 1]();
    poisson_0toN(bg, Bg, Nmax);
    unsigned int Bg_max = 2 * (unsigned int) Bg + 52;
    auto *br = new double[Nmax + 1];
    poisson_0toN(br, Br, Nmax);
    unsigned int Br_max = 2 * (unsigned int) Br + 52;

    // sum
    for (size_t red = 0; red <= Nmax; red++) {
        size_t i_start = red > Br_max ? red - Br_max : 0;
        for (size_t green = 0; green <= Nmax - red; green++) {
            double s = 0.;
            size_t j = (Nmax + 1) * green;
            for (size_t i = i_start; i <= red; i++){
                s += FgFr[j + i] * br[red - i];
            }
            tmp[(Nmax + 1) * red + green] = s;
        }
    }
    for (size_t green = 0; green <= Nmax; green++) {
        size_t i_start = green > Bg_max ? green - Bg_max : 0;
        for (size_t red = 0; red <= Nmax - green; red++) {
            double s = 0.;
            size_t j = (Nmax + 1) * red;
            for (size_t i = i_start; i <= green; i++){
                s += tmp[j + i] * bg[green - i];
            }
            SgSr[(Nmax + 1) * green + red] = s;
        }
    }

    delete[] bg;
    delete[] br;
    delete[] tmp;
}

/***** modification for P(N) *****/
void PdaFunctions::conv_pN(
        double *SgSr, double *FgFr,
        unsigned int Nmax,
        double Bg,
        double Br,
        double *pN
        ) {
    conv_pF(SgSr, FgFr, Nmax, Bg, Br);
    for (unsigned int i = 0; i <= Nmax; i++) {
        double s = 0.;
        for (unsigned  int j = 0; j <= i; j++){
            s += SgSr[Nmax * j + i];
        }
        for (unsigned int j = 0; j <= i; j++){
            SgSr[Nmax * j + i] *= pN[i] / s;
        }
    }
}


void PdaFunctions::sgsr_pN(
        double *SgSr,        // return: SgSr(i,j) = p(Sg=i, Sr=j)
        double *pN,        // p(N)
        unsigned int Nmax,    // max number of photons (max N)
        double Bg,        // <background green>, per time window (!)
        double Br,        // <background red>, -"-
        double pg_theor)        // p(green|F=1), incl. crosstalk
{
    /*** FgFr: matrix, FgFr(i,j) = p(Fg=i, Fr=j | F=i+j) ***/
    auto *FgFr = new double[(Nmax + 1) * (Nmax + 1)]();
    FgFr[0] = 1.;
    for (unsigned int i = 1; i <= Nmax; i++) {
        polynom2_conv(
                FgFr + i * (Nmax + 1),
                FgFr + (i - 1) * (Nmax + 1),
                (unsigned int) i,
                (double) pg_theor
        );
        for (size_t red = 0; red <= i - 1; red++) {
            FgFr[(i - 1 - red) * (Nmax + 1) + red] = FgFr[(i - 1) * (Nmax + 1) + red];
        }
    }
    for (size_t red = 0; red <= Nmax; red++) {
        FgFr[(Nmax - red) * (Nmax + 1) + red] = FgFr[Nmax * (Nmax + 1) + red];
    }
    /*** SgSr: matrix, SgSr(i,j) = p(Sg = i, Sr = j) ***/
    conv_pN(SgSr, FgFr, Nmax, Bg, Br, pN);
    delete[] FgFr;
}


void PdaFunctions::sgsr_pF(
        double *SgSr,
        double *pF,
        unsigned int Nmax,
        double Bg,
        double Br,
        double pg_theor
) {

    /*** FgFr: matrix, FgFr(i,j) = p(Fg = i, Fr = j) ***/

    auto *FgFr = new double[(Nmax + 1) * (Nmax + 1)]();
    FgFr[0] = 1.;
    unsigned int red;

    for (unsigned int i = 1; i <= Nmax; i++) {
        polynom2_conv(
                FgFr + i * (Nmax + 1),
                FgFr + (i - 1) * (Nmax + 1),
                i,
                pg_theor
                );
        for (unsigned int red = 0; red <= i - 1; red++){
            FgFr[(i - 1 - red) * (Nmax + 1) + red] =
                    FgFr[(i - 1) * (Nmax + 1) + red] * pF[i - 1];
        }
    }
    for (unsigned int red = 0; red <= Nmax; red++){
        FgFr[(Nmax - red) * (Nmax + 1) + red] =
                FgFr[Nmax * (Nmax + 1) + red] * pF[Nmax];
    }
    /*** SgSr: matrix, SgSr(i,j) = p(Sg = i, Sr = j) ***/
    conv_pF(SgSr, FgFr, Nmax, Bg, Br);
    delete[] FgFr;
}


void PdaFunctions::sgsr_pN_manypg(
        double *SgSr,
        double *pN,
        unsigned int Nmax,
        double Bg,
        double Br,
        unsigned int N_pg,
        double *pg_theor,
        double *a) {
    /*** FgFr: matrix, FgFr(i,j) = p(Fg = i, Fr = j) ***/
    size_t matrix_elements = (Nmax + 1) * (Nmax + 1);
    auto *FgFr = new double[matrix_elements]();
    auto *tmp = new double[matrix_elements]();
    memset(FgFr, 0, matrix_elements);

    unsigned int red;
    for (unsigned int j = 0; j < N_pg; j++) {
        tmp[0] = 1.;
        for (unsigned int i = 1; i <= Nmax; i++) {
            polynom2_conv(
                    tmp + i * (Nmax + 1),
                    tmp + (i - 1) * (Nmax + 1),
                    i,
                    pg_theor[j]
                    );
            for (red = 0; red <= i - 1; red++)
                FgFr[(i - 1 - red) * (Nmax + 1) + red] +=
                        tmp[(i - 1) * (Nmax + 1) + red] * a[j];
        }
        for (unsigned int red = 0; red <= Nmax; red++){
            FgFr[(Nmax - red) * (Nmax + 1) + red] +=
                    tmp[Nmax * (Nmax + 1) + red] * a[j];
        }
    }
    /*** SgSr: matrix, SgSr(i,j) = p(Sg = i, Sr = j) ***/
    conv_pN(SgSr, FgFr, Nmax, Bg, Br, pN);
    delete[] FgFr;
    delete[] tmp;
}


void PdaFunctions::sgsr_pF_manypg(
        double *SgSr,        // see sgsr_pN
        double *pF,          // input: p(F)
        unsigned int Nmax,
        double Bg,
        double Br,
        std::vector<double> &pg_theor,
        std::vector<double> &a // corresponding amplitudes
){
    /*** FgFr: matrix, FgFr(i,j) = p(Fg = i, Fr = j) ***/
    size_t matrix_elements = (Nmax + 1) * (Nmax + 1);
    auto *FgFr = new double[matrix_elements]();
    auto *tmp = new double[matrix_elements]();
    for (size_t pg_idx = 0; pg_idx < pg_theor.size(); pg_idx++) {
        tmp[0] = 1.;
        for (size_t i = 1; i <= Nmax; i++) {
            polynom2_conv(tmp + i * (Nmax + 1),tmp + (i - 1) * (Nmax + 1), i, pg_theor[pg_idx]);
            for (size_t red = 0; red <= i - 1; red++)
                FgFr[(i - 1 - red) * (Nmax + 1) + red] += tmp[(i - 1) * (Nmax + 1) + red] * pF[i - 1] * a[pg_idx];
        }
        for (size_t red = 0; red <= Nmax; red++)
            FgFr[(Nmax - red) * (Nmax + 1) + red] += tmp[Nmax * (Nmax + 1) + red] * pF[Nmax] * a[pg_idx];
    }
    /*** SgSr: matrix, SgSr(i,j) = p(Sg = i, Sr = j) ***/
    conv_pF(SgSr, FgFr, Nmax, Bg, Br);
    delete[] FgFr;
    delete[] tmp;
}

///////////////////////// calculating p(G,R), several ratios, many P(F)s //////////////////////////

void PdaFunctions::sgsr_manypF(
        double *SgSr,        // see sgsr_pN
        double *pF,        // input: p(F); size = (Nmax+1)xN_pg
        unsigned int Nmax,
        double Bg,
        double Br,
        unsigned int N_pg,        // size of pg_theor
        double *pg_theor,
        double *a)            // corresponding amplitudes
{

    /*** FgFr: matrix, FgFr(i,j) = p(Fg = i, Fr = j) ***/
    size_t matrix_elements = (Nmax + 1) * (Nmax + 1);
    auto *FgFr = new double[matrix_elements]();
    auto *tmp = new double[matrix_elements]();

    unsigned int j, i, red;
    for (j = 0; j < N_pg; j++) {

        tmp[0] = 1.;
        for (i = 1; i <= Nmax; i++) {
            polynom2_conv(tmp + i * (Nmax + 1), tmp + (i - 1) * (Nmax + 1), i,
                          pg_theor[j]);
            for (red = 0; red <= i - 1; red++)
                FgFr[(i - 1 - red) * (Nmax + 1) + red] +=
                        tmp[(i - 1) * (Nmax + 1) + red] *
                        pF[(Nmax + 1) * j + i - 1] * a[j];
        }
        for (red = 0; red <= Nmax; red++)
            FgFr[(Nmax - red) * (Nmax + 1) + red] +=
                    tmp[Nmax * (Nmax + 1) + red] * pF[(Nmax + 1) * j + Nmax] *
                    a[j];
    }

    /*** SgSr: matrix, SgSr(i,j) = p(Sg = i, Sr = j) ***/

    conv_pF(SgSr, FgFr, Nmax, Bg, Br);

    delete[] FgFr;
    delete[] tmp;

}


void PdaFunctions::poisson_0toN(double *return_p, double lam, size_t return_dim) {
    unsigned int i;
    return_p[0] = exp(-lam);
    for (i = 1; i <= return_dim; i++) {
        return_p[i] = return_p[i - 1] * lam / (double) i;
    }
}


void PdaFunctions::poisson_0toN_multi(double *return_p, double *lambda, unsigned int M, unsigned int N) {
    for (size_t j = 0; j < M; j++) {
        size_t ishift = (N + 1) * j;
        return_p[ishift] = exp(-lambda[j]);
        for (size_t i = 1; i <= N; i++) {
            return_p[ishift + i] =
                    return_p[ishift + i - 1] * lambda[j] / (double) i;
        }
    }
}


void PdaFunctions::polynom2_conv(
        double *return_p,
        double *p, unsigned int n,
        double p2
        ) {
    return_p[0] = p[0] * p2;
    for (size_t i = 1; i < n; i++)
        return_p[i] = p[i - 1] * (1. - p2) + p[i] * p2;
    return_p[n] = p[n - 1] * (1. - p2);
}


unsigned int Pda::get_max_number_of_photons() const {
    return Nmax;
}

void Pda::append(double amplitude, double probability_green) {
    sgsr_valid = false;
    amplitudes.push_back(amplitude);
    probability_green_theor.push_back(probability_green);
}

void Pda::set_probability_green(double *input, int n_input) {
    sgsr_valid = false;
    int n_components = n_input / 2;
    for(int i=0; i < n_components; i++){
        double amplitude = input[2 * i];
        double probability_green = input[(2 * i) + 1];
        amplitudes.push_back(amplitude);
        probability_green_theor.push_back(probability_green);
    }
}

void Pda::get_amplitudes(double**out, int* dim1) {
    *dim1 = amplitudes.size();
    //auto* t = (double*) malloc(sizeof(double) * amplitudes.size());
    *out = amplitudes.data(); // t;
}

void Pda::get_probability_green(double**out, int* dim1) {
    *dim1 = probability_green_theor.size();
    //auto* t = (double*) malloc(sizeof(double) * probability_green_theor.size());
    *out = probability_green_theor.data(); //t;
}

void Pda::clear_probability_green() {
    sgsr_valid = false;
    amplitudes.clear();
    probability_green_theor.clear();
}

void Pda::set_max_number_of_photons(unsigned int nmax) {
    sgsr_valid = false;
    Nmax = nmax;
    SgSr.resize((Nmax + 1) * (Nmax + 1));
    std::fill(SgSr.begin(), SgSr.end(), 0);
}

void Pda::evaluate() {
    PdaFunctions::sgsr_pF_manypg(
            SgSr.data(),
            pF.data(),
            get_max_number_of_photons(),
            get_green_background(),
            get_red_background(),
            probability_green_theor,
            amplitudes
    );
}

double Pda::get_green_background() const {
    return Bg;
}

void Pda::set_green_background(double bg) {
    sgsr_valid = false;
    Bg = bg;
}

double Pda::get_red_background() const {
    return Br;
}

void Pda::set_red_background(double br) {
    sgsr_valid = false;
    Br = br;
}

void Pda::setPF(double *in1D, int in_nbr) {
    sgsr_valid = false;
    pF.assign(in1D, in1D + in_nbr);
}

void Pda::get_SgSr_matrix(
        double **out, int *dim1, int *dim2
) {
    if(!sgsr_valid) evaluate();
    *out = SgSr.data();
    *dim1 = Nmax;
    *dim2 = Nmax;
}