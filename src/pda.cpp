#include "include/pda.h"



void Pda::get_1dhistogram(
        double **histogram_x, int *n_histogram_x,
        double **histogram_y, int *n_histogram_y,
        double xmax, double xmin, int nbins,
        bool log_x,
        double *input, int n_input1, int n_input2,
        int n_max, int n_min,
        bool skip_zero_photon
) {
    double xminlog = log(xmin);
    auto Nbinsf = (double) nbins;

    (*n_histogram_x) = nbins;
    (*n_histogram_y) = nbins;
    *histogram_x = (double*) calloc(sizeof(double), nbins);
    *histogram_y = (double*) calloc(sizeof(double), nbins);
    n_max = n_max < 0? (int) _Nmax : MIN(n_max, (int) _Nmax);
    n_min = n_min < 0? (int) _Nmin : n_min;
    int first_photon = skip_zero_photon;

    int ch1, ch2, first_ch2, bin;
    // build histogram
    for (bin = 0; bin < nbins; bin++) (*histogram_y)[bin] = 0.;
    if(log_x) {
        double bin_width = (log(xmax) - xminlog) / ((double) nbins - 1);
        double inverse_bin_width = 1. / bin_width;
        double xmincorr = log(xmin) - 0.5 * bin_width;
        // histogram X
        for (bin = 0; bin < nbins; bin++)
            (*histogram_x)[bin] = exp(xminlog + bin_width * (double) bin);
        // histogram Y
        for (ch1 = first_photon; ch1 <= n_max; ch1++) {
            first_ch2 = ch1 > n_min ? 1 : n_min - ch1;
            for (ch2 = first_ch2; ch2 <= n_max - ch1; ch2++) {
                double x = log(_histogram_function->run(ch1, ch2)); // SgSgr;
                double binf = std::floor((x - xmincorr) * inverse_bin_width);
                bin = (int) binf;
                if ((binf < Nbinsf) && (binf >= 0.)) (*histogram_y)[bin] += _S1S2[ch1 * (n_max + 1) + ch2];
            }
        }
    } else {
        double bin_width = (xmax - xmin) / ((double) nbins - 1.);
        double inverse_bin_width = 1. / bin_width;
        double xmincorr = xmin - 0.5 * bin_width;
        // histogram X
        for (bin = 0; bin < nbins; bin++)
            (*histogram_x)[bin] = xmin + bin_width * (double) bin;
        // histogram Y
        for (ch1 = first_photon; ch1 <= n_max; ch1++) {
            first_ch2 = ch1 > n_min ? 1 : n_min - ch1;
            for (ch2 = first_ch2; ch2 <= n_max - ch1; ch2++) {
                double x = _histogram_function->run(ch1, ch2);
                double binf = std::floor((x - xmincorr) * inverse_bin_width);
                bin = (int) binf;
                if ((binf < Nbinsf) && (binf >= 0.)) (*histogram_y)[bin] += _S1S2[ch1 * (n_max + 1) + ch2];
            }
        }
    }
}


void Pda::evaluate() {
#if VERBOSE
    std::clog << "-- evaluate PDA..." << std::endl;
    std::clog << "-- making sure array sizes match" << std::endl;
#endif
    for(int i =0; i < _S1S2.size(); i++) _S1S2[i] = 0;
    auto Nmax = get_max_number_of_photons();
    if(pF.size() < Nmax + 1){
        std::cout << "WARNING pF array too short. Appending zeros" << std::endl;
        while(pF.size() < Nmax + 1){
            pF.emplace_back(0.0);
        }
    }
    if(_probability_ch1.size() < _amplitudes.size()){
        std::cout << "WARNING probability array too short. Appending zeros" << std::endl;
        while(_probability_ch1.size() < _amplitudes.size()){
            _probability_ch1.emplace_back(0.0);
        }
    }
    if(_amplitudes.size() < _probability_ch1.size()){
        std::cout << "WARNING amplitude array too short. Appending zeros" << std::endl;
        while(_amplitudes.size() < _amplitudes.size()){
            _amplitudes.emplace_back(0.0);
        }
    }
#if VERBOSE
    std::clog << "-- Computing S1S2 matrix" << std::endl;
#endif
    double bg_ch1 = get_ch1_background();
    double bg_ch2 = get_ch1_background();
    PdaFunctions::S1S2_pF(_S1S2, pF, Nmax, bg_ch1, bg_ch2,
                          _probability_ch1,
                          _amplitudes
    );
    _is_valid_sgsr = true;
}



//
void PdaFunctions::conv_pF(
        std::vector<double> &SgSr,
        const std::vector<double> &FgFr,
        unsigned int Nmax,
        double Bg,
        double Br
) {
    std::vector<double> tmp((Nmax + 1) * (Nmax + 1), 0.0);
    std::vector<double> bg(Nmax + 1, 0.0);
    poisson_0toN(bg, 0, Bg, Nmax);
    std::vector<double> br(Nmax + 1, 0.0);
    poisson_0toN(br, 0, Br, Nmax);
    // sum
    for (size_t red = 0; red <= Nmax; red++) {
        size_t i_start = 0;
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
        size_t i_start = 0;
        for (size_t red = 0; red <= Nmax - green; red++) {
            double s = 0.;
            size_t j = (Nmax + 1) * red;
            for (size_t i = i_start; i <= green; i++){
                s += tmp[j + i] * bg[green - i];
            }
            SgSr[(Nmax + 1) * green + red] = s;
        }
    }
}


void PdaFunctions::S1S2_pF(
        std::vector<double> &SgSr,        // see sgsr_pN
        std::vector<double> &pF,          // input: p(F)
        unsigned int Nmax,
        double Bg,
        double Br,
        std::vector<double> &pg_theor,
        std::vector<double> &amplitudes // corresponding amplitudes
){
    /*** F1F2: matrix, F1F2(i,j) = p(F1 = i, F2 = j) ***/
    size_t matrix_elements = (Nmax + 1) * (Nmax + 1);
    std::vector<double> FgFr(matrix_elements, 0.0);
    std::vector<double> tmp(matrix_elements, 0.0);
    for(size_t pg_idx = 0; pg_idx < pg_theor.size(); pg_idx++) {
        auto p = pg_theor[pg_idx];
        auto a = amplitudes[pg_idx];
#if VERBOSE
        std::clog << "-- Adding species (amplitude, theoretical "
                     "detection probability): " << a << ", " << p << std::endl;
#endif
        tmp[0] = 1.;
        // Propagate the probabilities to other matrix rows
        for (size_t row = 1; row <= Nmax; row++) {
            // marks beginning of current and previous matrix row
            size_t row_offset_cur = (row + 0) * (Nmax + 1);
            size_t row_offset_pre = (row - 1) * (Nmax + 1);
            tmp[row_offset_cur + 0] = tmp[row_offset_pre + 0] * p;
            for(size_t col = 0; col < row; col++){
                tmp[row_offset_cur + col + 1] =
                        tmp[row_offset_pre + col + 0] * (1. - p) +
                        tmp[row_offset_pre + col + 1] * p;
            }
        }
        for (size_t row = 0; row < Nmax; row++) {
            for (size_t red = 0; red <= row; red++)
                FgFr[(row - red) * (Nmax + 1) + red] +=
                        tmp[row * (Nmax + 1) + red] * a * pF[row];
        }
        for (size_t red = 0; red < Nmax; red++)
            FgFr[(Nmax - red) * (Nmax + 1) + red] +=
                    tmp[Nmax * (Nmax + 1) + red] * a * pF[Nmax];
    }
    /*** S1S2: matrix, S1S2(i,j) = p(S1 = i, S2 = j) ***/
    conv_pF(SgSr, FgFr, Nmax, Bg, Br);
}


void PdaFunctions::poisson_0toN(
        std::vector<double> &return_p,
        int start_idx,
        double lam,
        int return_dim
) {
    unsigned int i;
    return_p[start_idx] = exp(-lam);
    for (i = start_idx + 1; i < start_idx + return_dim; i++) {
        return_p[i] = return_p[i - 1] * lam / (double) i;
    }
}

