
#include "include/correlation/correlate_seidel.h"



void seidel_lab::correlation_normalization(
        unsigned int nc, unsigned int nb,
        double *corrl, unsigned long long *xdat,
        unsigned int np1, unsigned int np2,
        double maxmat, double *mmat, int nf
) {
    double norm1, norm2;
    unsigned long long j, k, pw, ind = 0, km = 0;

    norm1 = (double) np1 / maxmat;
    norm2 = (double) np2 / maxmat;

    for (k = 0; k < nb; k++) {
        if (k == 0) { pw = 1; }
        else { pw = (unsigned long long) std::pow((double) 2.0, (int) k - 1); }
        j = 1;
        while ((j <= nc) && (k * nc + j < nb * nc) && (ind <= mmat[nf - 1])) {
            ind = (unsigned long long) ((xdat[k * nc] / pw) + j) * pw;
            xdat[k * nc + j] = ind;
            while (km < (unsigned int) (nf - 1) && ind >= mmat[km]) {
                maxmat -= mmat[km];
                km++;
            }
            corrl[k * nc + j] /= (double) ((maxmat - (nf - km) * ind) * (norm1 * norm2) * pw);
            j++;
        }
    }
}


void seidel_lab::correlation_normalization_mod(
        unsigned int nc, unsigned int nb,
        double *corrl, double *xdat,
        unsigned int np1, unsigned int np2,
        double maxmat, double *mmat, int nf,
        unsigned int *np11, unsigned int *np22) {
    double norm1, norm2;
    unsigned long long j, k, pw, ind = 0, km = 0;

    for (k = 0; k < nb; k++) {
        if (k == 0) { pw = 1; }
        else { pw = (unsigned long long) std::pow((double) 2.0, (int) k - 1); }
        j = 1;
        while ((j <= nc) && (k * nc + j < nb * nc) && (ind <= mmat[nf - 1])) {
            ind = ((unsigned long long) (xdat[k * nc] / pw) + j) * pw;
            xdat[k * nc + j] = ind;

            while (km < (unsigned int) (nf - 1) && ind >= mmat[km]) {
                maxmat -= mmat[km];
                np1 -= np11[km];
                np2 -= np22[km];
                km++;
            }

            norm1 = (double) np1 / maxmat;
            norm2 = (double) np2 / maxmat;

            corrl[k * nc + j] /= (double) ((maxmat - (nf - km) * ind) * (norm1 * norm2) * pw);//
            j++;
        }
    }
}


void seidel_lab::correlation_shell(unsigned long n, unsigned long long *a) {
    unsigned long i, j, inc;
    unsigned long long v;
    inc = 1; //Determine the starting increment.
    do {
        inc *= 3;
        inc++;
    } while (inc <= n);
    do { //Loop over the partial sorts.
        inc /= 3;
        for (i = inc + 1; i <= n; i++) //Outer loop of straight insertion.
        {
            v = a[i];
            j = i;
            while (a[j - inc] > v) //Inner loop of straight insertion.
            {
                a[j] = a[j - inc];
                j -= inc;
                if (j <= inc) break;
            }
            a[j] = v;
        }
    } while (inc > 1);
}


double seidel_lab::correlation_fast_full(
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
        bool do_full_correlation
) {
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int k = 0;
    unsigned int p = 0;
    unsigned long long maxmat;
    unsigned int im;
    unsigned int index;
    unsigned long long pw;
    unsigned long long limit_l;
    unsigned long long limit_r;

    auto t1 = (unsigned long long *) malloc(sizeof(unsigned long long) * np1);
    auto t2 = (unsigned long long *) malloc(sizeof(unsigned long long) * np2);

    if (do_full_correlation){
        for (im = 0; im < np1; im++) { t1[im] = tac1[im] + number_of_microtime_channels * mt1[im]; }
        for (im = 0; im < np2; im++) { t2[im] = tac2[im] + number_of_microtime_channels * mt2[im]; }
    } else{
        std::memcpy(t1, mt1, np1);
        std::memcpy(t2, mt2, np2);
    }

    auto weights1 = (double *) malloc(sizeof(double) *  np1);
    auto weights2 = (double *) malloc(sizeof(double) *  np2);
    std::memcpy(weights1, photon_weights1, np1);
    std::memcpy(weights2, photon_weights2, np2);

    //determine max macro time
    if (t1[np1 - 1] > t2[np2 - 1]) { maxmat = t1[np1 - 1]; }
    else { maxmat = t2[np2 - 1]; }
    correlation_shell(np1, t1);
    correlation_shell(np2, t2);
    for (k = 0; k < nb; k++) {
        p = 0;
        for (i = 0; i < np1; i++) {
            if (weights1[i] != 0) {
                if (k == 0) {
                    pw = 1;
                } else {
                    pw = (unsigned long long) std::pow((double) 2.0, (int) k - 1);
                }
                limit_l = xdat[k * nc] / pw + t1[i]; //left border of the block
                limit_r = limit_l + nc;          // right border of the block
                j = p;
                while ((j < np2) && (t2[j] <= limit_r)) {
                    if (weights2[j] != 0) {
                        if (t2[j] > limit_l)                        //if correlation time positive
                        {
                            index = (unsigned int) t2[j] - limit_l + k * nc;
                            corrl[index] += (weights1[i] * weights2[j]);
                        } else { p++; }
                    }
                    j++;
                }
            }
        }
        if (k > 0) {
            for (im = 0; im < np1; im++) t1[im] /= 2;
            for (im = 1; im < np1; im++) {
                if (t1[im] == t1[im - 1]) {
                    weights1[im] += weights1[im - 1];
                    weights1[im - 1] = 0;
                }
            }
            for (im = 0; im < np2; im++) t2[im] /= 2;
            for (im = 1; im < np2; im++) {
                if (t2[im] == t2[im - 1]) {
                    weights2[im] += weights2[im - 1];
                    weights2[im - 1] = 0;
                }
            }
        }
    }
    free(t1);
    free(t2);
    free(weights1);
    free(weights2);
    return ((double) maxmat);
}
