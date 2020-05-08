#include "include/correlation/peulen.h"


void peulen::correlation_full(
        size_t n_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1, size_t nt1,
        const unsigned long long *t2, const double *w2, size_t nt2
) {
#if VERBOSE
    std::clog << "-- Copying data to new arrays..." << std::endl;
#endif
    // the arrays can be modified inplace during the correlation. Thus, copied to a new array.
    auto t1_c = (unsigned long long *) malloc(sizeof(unsigned long long) * nt1);
    std::memcpy(t1_c, t1, sizeof(unsigned long long) * nt1);
    auto t2_c = (unsigned long long *) malloc(sizeof(unsigned long long) * nt2);
    std::memcpy(t2_c, t2, sizeof(unsigned long long) * nt2);
    auto w1_c = (double *) malloc(sizeof(double) * nt1);
    std::memcpy(w1_c, w1, sizeof(double) * nt1);
    auto w2_c = (double *) malloc(sizeof(double) * nt2);
    std::memcpy(w2_c, w2, sizeof(double) * nt2);

    for (size_t i_casc = 0; i_casc < n_casc; i_casc++) {
        peulen::correlation(
                0, nt1,
                0, nt2,
                i_casc, n_bins,
                taus, corr,
                t1_c, w1_c, nt1,
                t2_c, w2_c, nt2
        );
#pragma omp parallel sections default(none) shared(t1_c, t2_c, w1_c, w2_c, nt1, nt2)
        {
#pragma omp section
            peulen::correlation_coarsen(t1_c, w1_c, nt1);
#pragma omp section
            peulen::correlation_coarsen(t2_c, w2_c, nt2);
        }
    }
}


void peulen::correlation_coarsen(
        unsigned long long *t, double *w, size_t nt
) {
    t[0] /= 2;
    for (size_t i = 1; i < nt; i++) {
        t[i] /= 2;
        if (t[i] == t[i - 1]) {
            w[i] += w[i - 1];
            w[i - 1] = 0;
        }
    }
}

void peulen::correlation(
        size_t start_1, size_t end_1,
        size_t start_2, size_t end_2,
        size_t i_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1, size_t nt1,
        const unsigned long long *t2, const double *w2, size_t nt2
) {
    size_t i1, i2, p, index;
    start_1 = (start_1 > 0) ? start_1 : 0;
    end_1 = std::min(nt1, end_1);
    start_2 = (start_2 > 0) ? start_2 : 0;
    end_2 = std::min(nt2, end_2);
    auto scale = (unsigned long long) pow(2.0, i_casc);

    size_t offset = taus[i_casc * n_bins] / scale;
    p = start_2;
    for (i1 = start_1; i1 < end_1; i1++) {
        if (w1[i1] == 0) continue;
        size_t edge_l = t1[i1] + offset;
        size_t edge_r = edge_l + n_bins;
        for (i2 = p; i2 < end_2; i2++) {
            if (t2[i2] > edge_r) break;
            // if (w2[i2] != 0) { // faster without this if statement
            if (t2[i2] > edge_l) {
                index = t2[i2] - edge_l + i_casc * n_bins;
                corr[index] += (w1[i1] * w2[i2]);
            } else {
                p++;
            }
            //}
        }
    }
}

void peulen::correlation_normalize(
        double np1, uint64_t dt1,
        double np2, uint64_t dt2,
        std::vector<unsigned long long> &x_axis, std::vector<double> &corr,
        size_t n_bins,
        bool correct_x_axis
) {
    double cr1 = (double) np1 / std::max(1.0, (double) dt1);
    double cr2 = (double) np2 / std::max(1.0, (double) dt2);
    for (int j = 0; j < x_axis.size(); j++) {
        uint64_t pw = (uint64_t) pow(2.0, (int) (float(j - 1) / n_bins));
        double t_corr = (dt1 < dt2 - x_axis[j]) ? (double) dt1 : (double) (dt2 - x_axis[j]);
        corr[j] /= pw; corr[j] /= (cr1 * cr2 * t_corr);
        if(correct_x_axis){
            x_axis[j] = x_axis[j] / pw * pw;
        } else{
            x_axis[j] = x_axis[j];
        }
    }
}

void peulen::make_fine_times(
        unsigned long long *t, unsigned int n_times,
        unsigned short *tac, unsigned int n_tac
) {
#if VERBOSE
    std::clog << "-- Make fine, number of micro time channels: " << n_tac << std::endl;
#endif
    for (size_t i = 0; i < n_times; i++) {
        t[i] = t[i] * n_tac + tac[i];
    }
}


