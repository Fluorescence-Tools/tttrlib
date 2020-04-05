//
// Created by Thomas-Otavio Peulen on 4/2/20.
//

#ifndef TTTRLIB_CORRELATE_SEIDEL_H
#define TTTRLIB_CORRELATE_SEIDEL_H

#include <cstring>
#include <cmath>
#include <stdlib.h>


namespace seidel_lab{

    double correlation_fast_full(
            const unsigned long long *mt1,
            const unsigned long long *mt2,
            const unsigned int *tac1,
            const unsigned int *tac2,
            unsigned long long number_of_microtime_channels,
            const double* photon_weights1,
            const double* photon_weights2,
            unsigned int nc, unsigned int nb,
            unsigned int np1, unsigned int np2,
            double *corrl,
            const unsigned long long *xdat,
            bool do_full_correlation=false
    );

    void correlation_normalization(
            unsigned int nc, unsigned int nb,
            double *corrl, unsigned long long *xdat,
            unsigned int np1, unsigned int np2,
            double maxmat, double *mmat, int nf
    );

    void correlation_normalization_mod(
            unsigned int nc, unsigned int nb,
            double *corrl, double *xdat,
            unsigned int np1, unsigned int np2,
            double maxmat, double *mmat, int nf,
            unsigned int *np11, unsigned int *np22
    );

    inline void correlation_shell(
            unsigned long n,
            unsigned long long *a
    );

}


#endif //TTTRLIB_CORRELATE_SEIDEL_H
