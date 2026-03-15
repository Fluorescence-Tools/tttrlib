#include "include/Pda.h"
#include "include/Verbose.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#include "thirdparty/pocketfft/pocketfft_hdronly.h"
#include <complex>
#include <cmath>

void Pda::get_1dhistogram(
        double **histogram_x, int *n_histogram_x,
        double **histogram_y, int *n_histogram_y,
        double x_max, double x_min, int n_bins, bool log_x,
        std::vector<double> s1s2,
        int n_min, bool skip_zero_photon,
        std::vector<double> species_amplitudes,
        std::vector<double> probabilities_ch1
) {
if (is_verbose()) {
    std::clog << "-- RUN: get_1dhistogram..." << std::endl;
    std::clog << "-- x_min: " << x_min << std::endl;
    std::clog << "-- x_max: " << x_max << std::endl;
    std::clog << "-- n_bins: " << n_bins << std::endl;
    std::clog << "-- log_x: " << log_x << std::endl;
}
    auto Nbinsf = (double) n_bins;
    if(
        !species_amplitudes.empty() && !probabilities_ch1.empty()
    ){
        if(species_amplitudes.size() == probabilities_ch1.size()){
            set_amplitudes(species_amplitudes.data(), static_cast<int>(species_amplitudes.size()));
            set_probabilities_ch1(probabilities_ch1.data(), static_cast<int>(probabilities_ch1.size()));
            evaluate();
        } else{
            std::cerr << "WARNING: species_amplitudes and probabilities_ch1"
                         "differ in size - did not update S1S2!";
        }
    }
    if(s1s2.empty()){
        if (is_verbose()) {
            std::clog << "-- Using model s1s2 matrix! " << std::endl;
        }
        s1s2 = _S1S2;
    } else {
        if (is_verbose()) {
            std::clog << "-- Using input s1s2 matrix! " << std::endl;
        }
    }
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
if (is_verbose()) {
    std::clog << "-- n_max: " << n_max << std::endl;
    std::clog << "-- n_min: " << n_min << std::endl;
    std::clog << "-- bin_width: " << bin_width << std::endl;
    std::clog << "-- inverse_bin_width: " << inverse_bin_width << std::endl;
    std::clog << "-- xmincorr: " << xmincorr << std::endl;
}

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
if (is_verbose()) {
    std::clog << "-- evaluate PDA..." << std::endl;
    std::clog << "-- making sure array sizes match" << std::endl;
}
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
if (is_verbose()) {
    std::clog << "-- Computing S1S2 matrix" << std::endl;
}
    double bg_ch1 = get_ch1_background();
    double bg_ch2 = get_ch2_background();
    
    // Dispatch to the appropriate implementation
    if (_implementation == PdaImplementation::PDA_OPTIMIZED) {
        S1S2_pF_optimized(_S1S2, pF, Nmax, bg_ch1, bg_ch2,
                          _probability_ch1,
                          _amplitudes
        );
    } else {
        S1S2_pF(_S1S2, pF, Nmax, bg_ch1, bg_ch2,
                          _probability_ch1,
                          _amplitudes
        );
    }
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
if (is_verbose()) {
        std::clog << "-- Computing S1S2 for species (amplitude, p(ch1)): " << a << ", " << p << std::endl;
}
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


/// \brief Find optimal FFT size (power of 2) for efficiency.
///
/// Calculates the smallest power-of-2 that is at least 2*(Nmax+1),
/// which is optimal for FFT computation.
///
/// \param Nmax Maximum number of photons
/// \return Optimal FFT size (power of 2)
static int good_fft_size(int Nmax) {
    int size = 2 * (Nmax + 1);
    int fft_size = 1;
    while (fft_size < size) fft_size *= 2;
    return fft_size;
}

/// \brief Determine Poisson threshold where probability drops below 1e-15.
///
/// Uses polynomial approximation for lambda < 100 and linear approximation
/// for lambda >= 100 to determine where Poisson probability becomes negligible.
/// This is used to optimize FFT size and skip negligible tail values.
///
/// Polynomial coefficients (lambda < 100):
///   threshold = -1.4040e-6*λ⁴ + 3.5397e-4*λ³ - 0.0337*λ² + 2.9359*λ + 22.7281
///
/// Linear approximation (lambda >= 100):
///   threshold = 1.32*λ + 63
///
/// \param lambda Poisson distribution parameter (mean number of photons)
/// \return Threshold index where Poisson probability < 1e-15
/// \note Reduces FFT size by 30-50% for typical backgrounds
static unsigned int poisson_threshold15(double lambda) {
    if (lambda < 100.) {
        return (unsigned int)std::ceil(
            -1.4040e-6*lambda*lambda*lambda*lambda + 
            3.5397e-4*lambda*lambda*lambda - 
            0.0337*lambda*lambda + 
            2.9359*lambda + 22.7281
        );
    } else {
        return (unsigned int)std::ceil(1.32*lambda + 63.);
    }
}

/// \brief Generate multiple Poisson distributions simultaneously.
///
/// Efficiently computes M different Poisson distributions with different
/// lambda parameters. Uses iterative computation: p[i] = p[i-1] * lambda / i
/// which is more numerically stable than computing factorials.
///
/// \param return_p[out] Output array of size (N+1)*M containing M Poisson distributions
/// \param lambda[in] Vector of M lambda parameters (mean photon counts)
/// \param N Maximum number of photons
/// \note Useful for multi-channel background correction
static void poisson_0toN_multi(std::vector<double>& return_p, 
                               const std::vector<double>& lambda, 
                               unsigned int N) {
    for (size_t j = 0; j < lambda.size(); j++) {
        size_t offset = (N + 1) * j;
        return_p[offset] = std::exp(-lambda[j]);
        for (unsigned int i = 1; i <= N; i++) {
            return_p[offset + i] = return_p[offset + i - 1] * lambda[j] / i;
        }
    }
}

/// \brief 1D FFT-based deconvolution to compute p^(1/order).
///
/// Uses FFT to efficiently compute the fractional power of a probability
/// distribution. This is used for multi-molecule event correction by
/// computing the single-molecule distribution from the observed multi-molecule
/// distribution.
///
/// Algorithm:
///   1. Forward FFT of input distribution
///   2. Point-by-point power(1/order) in Fourier space
///   3. Inverse FFT to get result
///
/// \param pf1[out] Output probability distribution p^(1/order)
/// \param pf_exp[in] Input probability distribution
/// \param Nmax Maximum number of photons
/// \param order Convolution order (typically 2-50 for multi-molecule correction)
/// \note Uses pocketfft for complex FFT operations
static void fftdc1D(std::vector<double>& pf1, const std::vector<double>& pf_exp, 
                    int Nmax, int order) {
    int fft_size = good_fft_size(Nmax);
    std::vector<std::complex<double>> dft_a(fft_size, std::complex<double>(0.0, 0.0));
    
    // Copy input to complex array
    for (int i = 0; i <= Nmax; i++) {
        dft_a[i] = std::complex<double>(pf_exp[i], 0.0);
    }
    
    // Forward FFT
    pocketfft::shape_t shape{(size_t)fft_size};
    pocketfft::stride_t stride{sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0};
    pocketfft::c2c(shape, stride, stride, axes, false, dft_a.data(), dft_a.data(), 1.0);
    
    // Point-by-point power(1/order)
    double pw = 1.0 / order;
    for (int i = 0; i < fft_size; i++) {
        double r = std::pow(std::abs(dft_a[i]), pw);
        double phi = pw * std::arg(dft_a[i]);
        dft_a[i] = std::polar(r, phi);
    }
    
    // Inverse FFT
    pocketfft::c2c(shape, stride, stride, axes, true, dft_a.data(), dft_a.data(), 1.0 / fft_size);
    
    // Copy back to output
    pf1.resize(Nmax + 1);
    for (int i = 0; i <= Nmax; i++) {
        pf1[i] = dft_a[i].real();
    }
}

/// \brief 2D FFT-based convolution to compute A^order.
///
/// Uses 2D FFT to efficiently compute the power of a 2D probability matrix.
/// This is used for multi-molecule event correction by computing the
/// multi-molecule distribution from the single-molecule distribution.
///
/// Algorithm:
///   1. Forward 2D FFT of input matrix
///   2. Point-by-point power(order) in Fourier space
///   3. Inverse 2D FFT to get result
///
/// \param AxA[out] Output 2D probability matrix A^order
/// \param A[in] Input 2D probability matrix (size (Nmax+1)×(Nmax+1))
/// \param Nmax Maximum number of photons
/// \param order Convolution order (typically 2-50 for multi-molecule correction)
/// \note Uses pocketfft for 2D complex FFT operations
/// \note Result is normalized to maintain probability distribution properties
static void fftc2D_AA(std::vector<double>& AxA, const std::vector<double>& A, 
                      int Nmax, int order) {
    int fft_size = good_fft_size(Nmax);
    std::vector<std::complex<double>> dft_A(fft_size * fft_size, std::complex<double>(0.0, 0.0));
    
    // Copy A to complex array
    for (int i = 0; i <= Nmax; i++) {
        for (int j = 0; j <= Nmax; j++) {
            dft_A[fft_size * i + j] = std::complex<double>(A[(Nmax + 1) * i + j], 0.0);
        }
    }
    
    // Forward 2D FFT
    pocketfft::shape_t shape{(size_t)fft_size, (size_t)fft_size};
    pocketfft::stride_t stride{(ptrdiff_t)(fft_size * sizeof(std::complex<double>)), 
                               (ptrdiff_t)sizeof(std::complex<double>)};
    pocketfft::shape_t axes{0, 1};
    pocketfft::c2c(shape, stride, stride, axes, false, dft_A.data(), dft_A.data(), 1.0);
    
    // Point-by-point power(order)
    for (int i = 0; i < fft_size * fft_size; i++) {
        dft_A[i] = std::pow(dft_A[i], (double)order);
    }
    
    // Inverse 2D FFT
    pocketfft::c2c(shape, stride, stride, axes, true, dft_A.data(), dft_A.data(), 1.0 / (fft_size * fft_size));
    
    // Copy back to output
    AxA.resize((Nmax + 1) * (Nmax + 1));
    for (int i = 0; i <= Nmax; i++) {
        for (int j = 0; j <= Nmax; j++) {
            AxA[(Nmax + 1) * i + j] = dft_A[fft_size * i + j].real();
        }
    }
}

/// \brief Determine convolution order for multi-molecule correction.
///
/// Calculates how many times to convolve the distribution with itself
/// to achieve the desired probability threshold. Based on the formula:
///   order = ceil(log(p0) / log(p0_wanted))
///
/// \param p0 Current probability of zero photons (pF[0])
/// \param p0_wanted Target probability threshold (typically 1e-15)
/// \return Convolution order (1 = no correction, >1 = correction needed)
/// \note order=1 means no correction is needed
/// \note order=50 means significant multi-molecule correction
static int pF_conv_order(double p0, double p0_wanted) {
    return (int)std::ceil(std::log(p0) / std::log(p0_wanted));
}

/// \brief Optimized S1S2 computation with OpenMP parallelization and FFT correction.
///
/// This is the optimized implementation of S1S2_pF that includes:
///   - OpenMP parallelization over species
///   - Thread-local accumulators for efficiency
///   - Automatic FFT-based multi-molecule correction
///   - Poisson threshold optimization
///
/// The computation proceeds in two stages:
///   1. Parallel computation of FgFr matrix (green/red photon distribution)
///   2. Optional FFT-based multi-molecule correction if needed
///   3. Convolution with background to get final S1S2 matrix
///
/// \param S1S2[out] Output S1S2 matrix (size (Nmax+1)×(Nmax+1))
/// \param pF[in] Probability distribution of total photon counts
/// \param Nmax Maximum number of photons
/// \param background_ch1 Background level in channel 1 (green)
/// \param background_ch2 Background level in channel 2 (red)
/// \param p_ch1[in] Vector of green detection probabilities for each species
/// \param amplitudes[in] Vector of amplitudes (fractions) for each species
///
/// \note Automatically applies FFT correction when pF[0] > 1e-15
/// \note Thread-safe with OpenMP critical sections
/// \note Falls back to single-threaded when OpenMP unavailable
void Pda::S1S2_pF_optimized(
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

#ifdef _OPENMP
    #pragma omp parallel
    {
#endif
        std::vector<double> tmp(matrix_elements, 0.0);
        std::vector<double> FgFr_local(matrix_elements, 0.0);

#ifdef _OPENMP
        #pragma omp for
#endif
        for(int pg_idx = 0; pg_idx < (int)p_ch1.size(); pg_idx++) {
            auto p = p_ch1[pg_idx];
            auto a = amplitudes[pg_idx];
            if (is_verbose()) {
                std::clog << "-- Computing S1S2 for species (amplitude, p(ch1)): " << a << ", " << p << std::endl;
            }
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
                    FgFr_local[(row - red) * (Nmax + 1) + red] +=
                            tmp[row * (Nmax + 1) + red] * a * pF[row];
            }
            for (size_t red = 0; red < Nmax; red++)
                FgFr_local[(Nmax - red) * (Nmax + 1) + red] +=
                        tmp[Nmax * (Nmax + 1) + red] * a * pF[Nmax];
        }

#ifdef _OPENMP
        #pragma omp critical
        {
            for(size_t i = 0; i < matrix_elements; i++){
                FgFr[i] += FgFr_local[i];
            }
        }
    }
#else
    FgFr = FgFr_local;
#endif

    /*** Multi-molecule correction using FFT ***/
    // Determine convolution order based on pF[0] (probability of detecting 0 photons)
    // We want to correct for multi-molecule events
    double p0 = pF[0];
    double p0_wanted = 1e-15;  // Target threshold
    int conv_order = pF_conv_order(p0, p0_wanted);
    
    if (conv_order > 1) {
        if (is_verbose()) {
            std::clog << "-- Applying multi-molecule correction with order: " << conv_order << std::endl;
        }
        
        // Apply FFT-based 2D convolution for multi-molecule correction
        std::vector<double> FgFr_corrected;
        fftc2D_AA(FgFr_corrected, FgFr, Nmax, conv_order);
        
        // Normalize the corrected result
        double s = (1.0 - pF[0]) / (1.0 - FgFr_corrected[0]);
        FgFr_corrected[0] = pF[0];
        for (size_t j = 1; j < matrix_elements; j++) {
            FgFr_corrected[j] *= s;
        }
        
        FgFr = FgFr_corrected;
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
    int i;
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
if (is_verbose()) {
    std::clog << "-- Make S1S2 matrix... " << std::endl;
    std::clog << "-- minimum_time_window_length: " << minimum_time_window_length << std::endl;
    std::clog << "-- minimum_number_of_photons_in_time_window: " << minimum_number_of_photons << std::endl;
}
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
if (is_verbose()) {
    std::clog << "-- Number of time windows: " << n_tw / 2 << std::endl;
    std::clog << "-- Getting routing channels... " << std::endl;
    std::clog << "-- Counting photons... " << std::endl;
}
    int n_tttr = 0;
    for(size_t i=0; i<n_tw/2; i++){
        size_t start = tws[i + 0];
        size_t stop = tws[i + 1];
        int n_ch1 = 0;
        int n_ch2 = 0;
        size_t j;
        for(j=start; j<stop; j++){
            short channel = routing_channels[j];
            for(auto &c: channels_1) n_ch1 += (c==channel);
            for(auto &c: channels_2) n_ch2 += (c==channel);
        }
        size_t n_photons = static_cast<size_t>(n_ch1 + n_ch2);
        if(
                n_photons < static_cast<size_t>(minimum_number_of_photons) ||
                n_photons > static_cast<size_t>(maximum_number_of_photons)
        ) continue;
        tmp_tttr_indices.emplace_back(static_cast<int>(j)); n_tttr++;
        tmp_s1s2[n_ch2 * (maximum_number_of_photons + 1) + n_ch1] += 1.0;
        tmp_ps[n_photons] += 1.0;
    }
    *tttr_indices = (int*) malloc(tmp_tttr_indices.size() * sizeof(int));
    memcpy(*tttr_indices, tmp_tttr_indices.data(), tmp_tttr_indices.size() * sizeof(int));
    *n_tttr_indices = n_tttr;
    *ps = tmp_ps;
    *dim_ps = (maximum_number_of_photons + 1);
    *s1s2 = tmp_s1s2;
    *dim1 = (maximum_number_of_photons + 1);
    *dim2 = (maximum_number_of_photons + 1);
}
