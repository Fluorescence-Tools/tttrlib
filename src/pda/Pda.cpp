#include "include/Pda.h"



void Pda::get_1dhistogram(
        double **histogram_x, int *n_histogram_x,
        double **histogram_y, int *n_histogram_y,
        double x_max, double x_min, int n_bins, bool log_x,
        std::vector<double> s1s2,
        int n_min, bool skip_zero_photon,
        std::vector<double> species_amplitudes,
        std::vector<double> probabilities_ch1
) {
#if VERBOSE_TTTRLIB
    std::clog << "-- RUN: get_1dhistogram..." << std::endl;
    std::clog << "-- x_min: " << x_min << std::endl;
    std::clog << "-- x_max: " << x_max << std::endl;
    std::clog << "-- n_bins: " << n_bins << std::endl;
    std::clog << "-- log_x: " << log_x << std::endl;
#endif
    auto Nbinsf = (double) n_bins;
    if(
        !species_amplitudes.empty() && !probabilities_ch1.empty()
    ){
        if(species_amplitudes.size() == probabilities_ch1.size()){
            set_amplitudes(species_amplitudes.data(), species_amplitudes.size());
            set_probabilities_ch1(probabilities_ch1.data(), probabilities_ch1.size());
            evaluate();
        } else{
            std::cerr << "WARNING: species_amplitudes and probabilities_ch1"
                         "differ in size - did not update S1S2!";
        }
    }
    if(s1s2.empty()){
#if VERBOSE_TTTRLIB
        std::clog << "-- Using model s1s2 matrix! " << std::endl;
#endif
        s1s2 = _S1S2;
    }
#if VERBOSE_TTTRLIB
    else{
        std::clog << "-- Using input s1s2 matrix! " << std::endl;
    }
#endif
    int n_max = (int) std::sqrt(s1s2.size()) - 1;

    (*n_histogram_x) = n_bins;
    (*n_histogram_y) = n_bins;
    *histogram_x = (double*) calloc(sizeof(double), n_bins);
    *histogram_y = (double*) calloc(sizeof(double), n_bins);
    n_min = n_min < 0 ? (int) _n_2d_min : n_min;
    int first_photon = skip_zero_photon;

    // build histogram
    double bin_width = log_x ?
                       (log(x_max) - log(x_min)) / ((double) n_bins - 1) :
                       (x_max - x_min) / ((double) n_bins - 1.);
    double inverse_bin_width = 1. / bin_width;
    double xmincorr = log_x ?
                      log(x_min) - 0.5 * bin_width :
                      x_min - 0.5 * bin_width;
#if VERBOSE_TTTRLIB
    std::clog << "-- n_max: " << n_max << std::endl;
    std::clog << "-- n_min: " << n_min << std::endl;
    std::clog << "-- bin_width: " << bin_width << std::endl;
    std::clog << "-- inverse_bin_width: " << inverse_bin_width << std::endl;
    std::clog << "-- xmincorr: " << xmincorr << std::endl;
#endif

    int ch1, ch2, first_ch2, bin;
    // histogram X
    for (bin = 0; bin < n_bins; bin++)
        (*histogram_x)[bin] = log_x ?
                              exp(log(x_min) + bin_width * (double) bin) :
                              x_min + bin_width * (double) bin;
    // histogram Y
    for (bin = 0; bin < n_bins; bin++) (*histogram_y)[bin] = 0.;
    for (ch1 = first_photon; ch1 <= n_max; ch1++) {
        first_ch2 = ch1 > n_min ? 1 : n_min - ch1;
        for (ch2 = first_ch2; ch2 <= n_max - ch1; ch2++) {
            double x = log_x ?
                log(_histogram_function->run(ch1, ch2)):
                _histogram_function->run(ch1, ch2);
            double binf = std::floor((x - xmincorr) * inverse_bin_width);
            bin = (int) binf;
            if ((binf < Nbinsf) && (binf >= 0.)){
                (*histogram_y)[bin] += s1s2[ch2 * (n_max + 1) + ch1];
            }
        }
    }
}


void Pda::evaluate() {
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
    std::clog << "-- Computing S1S2 matrix" << std::endl;
#endif
    double bg_ch1 = get_ch1_background();
    double bg_ch2 = get_ch2_background();
    S1S2_pF(_S1S2, pF, Nmax, bg_ch1, bg_ch2,
                          _probability_ch1,
                          _amplitudes
    );
    _is_valid_sgsr = true;
}



//
void Pda::conv_pF(
        std::vector<double> &S1S2,
        const std::vector<double> &F1F2,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2
) {
    std::vector<double> tmp((Nmax + 1) * (Nmax + 1), 0.0);
    std::vector<double> bg(Nmax + 1, 0.0);
    poisson_0toN(bg, 0, background_ch1, Nmax);
    std::vector<double> br(Nmax + 1, 0.0);
    poisson_0toN(br, 0, background_ch2, Nmax);
    // sum
    for (size_t red = 0; red <= Nmax; red++) {
        size_t i_start = 0;
        for (size_t green = 0; green <= Nmax - red; green++) {
            double s = 0.;
            size_t j = (Nmax + 1) * green;
            for (size_t i = i_start; i <= red; i++){
                s += F1F2[j + i] * br[red - i];
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
            S1S2[(Nmax + 1) * green + red] = s;
        }
    }
}


void Pda::S1S2_pF(
        std::vector<double> &S1S2,        // see sgsr_pN
        std::vector<double> &pF,          // input: p(F)
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes // corresponding amplitudes
){
    /*** F1F2: matrix, F1F2(i,j) = p(F1 = i, F2 = j) ***/
    size_t matrix_elements = (Nmax + 1) * (Nmax + 1);
    std::vector<double> FgFr(matrix_elements, 0.0);
    std::vector<double> tmp(matrix_elements, 0.0);
    for(size_t pg_idx = 0; pg_idx < p_ch1.size(); pg_idx++) {
        auto p = p_ch1[pg_idx];
        auto a = amplitudes[pg_idx];
#if VERBOSE_TTTRLIB
        std::clog << "-- Computing S1S2 for species (amplitude, p(ch1)): " << a << ", " << p << std::endl;
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
    conv_pF(S1S2, FgFr, Nmax, background_ch1, background_ch2);
}


void Pda::poisson_0toN(
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


void Pda::compute_experimental_histograms(
        TTTR* tttr_data,
        double** s1s2, int* dim1, int* dim2,
        double** ps, int* dim_ps,
        int** tttr_indices, int* n_tttr_indices,
        std::vector<int> channels_1,
        std::vector<int> channels_2,
        int maximum_number_of_photons,
        int minimum_number_of_photons,
        double minimum_time_window_length
){
#if VERBOSE_TTTRLIB
    std::clog << "-- Make S1S2 matrix... " << std::endl;
    std::clog << "-- minimum_time_window_length: " << minimum_time_window_length << std::endl;
    std::clog << "-- minimum_number_of_photons_in_time_window: " << minimum_number_of_photons << std::endl;
#endif
    auto tmp_s1s2 = (double*) calloc(
            (maximum_number_of_photons + 1) * (maximum_number_of_photons + 1),
            sizeof(double)
    );
    for(int i=0; i< (maximum_number_of_photons + 1) * (maximum_number_of_photons + 1); i++) tmp_s1s2[i]=0.0;
    auto tmp_ps = (double*) calloc(
            (maximum_number_of_photons + 1) * 2, sizeof(double)
    );
    for(int i=0; i< (maximum_number_of_photons + 1) * 2; i++) tmp_ps[i]=0.0;
    std::vector<int> tmp_tttr_indices;
    int *tws; int n_tw;
    tttr_data->get_time_window_ranges(
            &tws, &n_tw,
            minimum_time_window_length,
            minimum_number_of_photons
    );
    signed char* routing_channels; int n_routing_channels;
    tttr_data->get_routing_channel(
            &routing_channels, &n_routing_channels
    );
#if VERBOSE_TTTRLIB
    std::clog << "-- Number of time windows: " << n_tw / 2 << std::endl;
    std::clog << "-- Getting routing channels... " << std::endl;
    std::clog << "-- Counting photons... " << std::endl;
#endif
    int n_tttr = 0;
    for(size_t i=0; i<n_tw/2; i++){
        size_t start = tws[i + 0];
        size_t stop = tws[i + 1];
        int n_ch1 = 0;
        int n_ch2 = 0;
        int j;
        for(j=start; j<stop; j++){
            short channel = routing_channels[j];
            for(auto &c: channels_1) n_ch1 += (c==channel);
            for(auto &c: channels_2) n_ch2 += (c==channel);
        }
        size_t n_photons = n_ch1 + n_ch2;
        if(
                n_photons < minimum_number_of_photons ||
                n_photons > maximum_number_of_photons
        ) continue;
        tmp_tttr_indices.emplace_back(j); n_tttr++;
        tmp_s1s2[n_ch2 * (maximum_number_of_photons + 1) + n_ch1] += 1.0;
        tmp_ps[n_photons] += 1.0;
    }
    *tttr_indices = (int*) malloc(tmp_tttr_indices.size() * sizeof(int));
    memcpy(*tttr_indices, tmp_tttr_indices.data(), tmp_tttr_indices.size());
    *n_tttr_indices = n_tttr;
    *ps = tmp_ps;
    *dim_ps = (maximum_number_of_photons + 1);
    *s1s2 = tmp_s1s2;
    *dim1 = (maximum_number_of_photons + 1);
    *dim2 = (maximum_number_of_photons + 1);
}
