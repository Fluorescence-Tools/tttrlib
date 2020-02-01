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

#ifndef TTTRLIB_PDA_H
#define TTTRLIB_PDA_H

#include <math.h>
#include <string.h>
#include <vector>


class Pda {

private:

    double Bg;
    double Br;
    unsigned int Nmax;
    std::vector<double> SgSr;
    std::vector<double> pF;
    std::vector<double> amplitudes;
    std::vector<double> probability_green_theor;

public:

    // Constructor and Destructor
    ~Pda(){
    }

    // Methods
    void append(double amplitude, double probability_green);
    void set_probability_green(double *in, int n_in);
    void clear_probability_green();
    void get_amplitudes(double**out, int* dim1);
    void get_probability_green(double**out, int* dim1);

    // Getter and Setter
    unsigned int get_max_number_of_photons() const;

    void set_max_number_of_photons(unsigned int nmax);

    double get_green_background() const;

    void set_green_background(double bg);

    double get_red_background() const;

    void set_red_background(double br);

    void setPF(double *in, int n_in);

    void get_SgSr_matrix(
            double** out, int* dim1, int* dim2
    );


    void evaluate();

};


namespace PdaFunctions{

    /*!
     *
     * calculating p(G,R) according to Matthew
     *
     * @param SgSr  SgSr(i,j) = p(Sg=i, Sr=j)
     * @param pN p(N)
     * @param Nmax max number of photons (max N)
     * @param Bg <background green>, per time window (!)
     * @param Br <background red>, -"-
     * @param pg_theor
     */
    void sgsr_pN(
            double *SgSr,
            double *pN,
            unsigned int Nmax,
            double Bg,
            double Br,
            double pg_theor
    );

    /*!
     * calculating p(G,R), one ratio, one P(F)
     *
     * @param SgSr sgsr_pN
     * @param pF input p(F)
     * @param Nmax
     * @param Bg
     * @param Br
     * @param pg_theor
     */
    void sgsr_pF(
            double *SgSr,
            double *pF,
            unsigned int Nmax,
            double Bg,
            double Br,
            double pg_theor
    );


    /*!
     *
     * calculating p(G,R), several ratios, same P(N)
     *
     * @param SgSr see sgsr_pN
     * @param pN input: p(N)
     * @param Nmax
     * @param Bg
     * @param Br
     * @param N_pg size of pg_theor
     * @param pg_theor
     * @param a corresponding amplitudes
     */
    void sgsr_pN_manypg(
            double *SgSr,
            double *pN,
            unsigned int Nmax,
            double Bg,
            double Br,
            unsigned int N_pg,
            double *pg_theor,
            double *a);


    /*!
     *
     * calculating p(G,R), several ratios, same P(F)
     *
     * @param SgSr see sgsr_pN
     * @param pF input: p(F)
     * @param Nmax
     * @param Bg
     * @param Br
     * @param N_pg size of pg_theor
     * @param pg_theor
     * @param a corresponding amplitudes
     */
    void sgsr_pF_manypg(
            double *SgSr,
            double *pF,
            unsigned int Nmax,
            double Bg,
            double Br,
            unsigned int N_pg,
            double *pg_theor,
            double *a);

    void sgsr_manypF(double *, double *, unsigned int, double, double, unsigned int, double *, double *);


    /*!
     *
     * @param SgSr
     * @param FgFr
     * @param Nmax
     * @param Bg
     * @param Br
     */
    void conv_pF(double *SgSr, double *FgFr, unsigned int Nmax, double Bg, double Br);


    /*!
     *
     * @param SgSr
     * @param FgFr
     * @param Nmax
     * @param Bg
     * @param Br
     * @param pN
     */
    void conv_pN(double *SgSr, double *FgFr, unsigned int Nmax, double Bg, double Br, double *pN);


    /*!
    * generates Poisson distribution witn average= lambda, for 0..N
    *
    * @param return_p
    * @param lambda
    * @param return_dim
    */
    void poisson_0toN(double *return_p, double lambda, unsigned int return_dim);


    /*!
     * generates Poisson distribution for a set of lambdas
     */
    void poisson_0toN_multi(double *, double *, unsigned int, unsigned int);


    /*!
     * convolves vectors p and [p2 1-p2]
     */
    void polynom2_conv(double *, double *, unsigned int, double);

}


#endif //TTTRLIB_PDA_H
