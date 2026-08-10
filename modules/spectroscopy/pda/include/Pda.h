// SPDX-License-Identifier: BSD-3-Clause
#include "Verbose.h"
#ifndef TTTRLIB_PDA_H
#define TTTRLIB_PDA_H

#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "TTTR.h"
#include "PdaCallback.h"
#include "HistogramAxis.h"   // HistogramBinning, shared with the hist module

enum PdaImplementation {
    PDA_DEFAULT,     ///< The default, original implementation
    PDA_OPTIMIZED    ///< An optimized implementation using OpenMP and dynamic thresholds
};

/// \class Pda
/// \brief Photon Distribution Analysis class for computing histograms.
///
/// \par Matrix index convention
/// Every (Nmax+1) x (Nmax+1) photon-count matrix produced or consumed by this
/// class -- the model matrix from evaluate()/get_S1S2_matrix(), the
/// experimental matrix from compute_experimental_histograms(), and the
/// optional `s1s2` input of get_1dhistogram() -- is stored row-major with
///
///     M[ch1 * (Nmax + 1) + ch2] == p(S1 == ch1, S2 == ch2)
///
/// so the ROW index is channel 1 (green in a FRET experiment) and the COLUMN
/// index is channel 2 (red). Feeding a transposed matrix in still runs and
/// still produces a plausible-looking histogram -- it simply mirrors the
/// projected axis (E <-> 1 - E), which is why the convention is spelled out
/// here rather than left implicit.
class Pda {

private:
    /// Set to True of the SgSr histogram is valid, i.e., if the inputs match
    /// the values reported in the histogram
    bool _is_valid_sgsr = false;
    PdaCallback* _histogram_function;

    /// The selected PDA implementation
    PdaImplementation _implementation = PdaImplementation::PDA_DEFAULT;

    /// Cache for get_1dhistogram: target bin of each VISITED S1S2 cell, in
    /// row-major visit order (compact, not one entry per matrix cell).
    /// Off-axis cells are mapped to bin n_bins, a trash slot, so the
    /// accumulation needs no per-cell test. The callback value of a cell only
    /// depends on the callback and the binning parameters, so this is reused
    /// across calls (fit iterations) and rebuilt when either changes.
    std::vector<int> _hist1d_bins;
    bool _hist1d_valid = false;
    double _hist1d_xmax = 0.0, _hist1d_xmin = 0.0;
    int _hist1d_nbins = -1, _hist1d_nmax = -1, _hist1d_nmin = -1;
    bool _hist1d_logx = false, _hist1d_skip = false;

    /// Probablity of detecting a green photon for the species
    std::vector<double> _probability_ch1;

    /// Amplitudes of the species
    std::vector<double> _amplitudes;

    /// Fills _hist1d_bins for the given axis, unless it is already current.
    void build_hist1d_cache(
            const HistogramBinning<double>& axis,
            int n_max, int n_min, int first_photon,
            double x_max, double x_min, int n_bins, bool log_x,
            bool skip_zero_photon
    );

    /// Sparse mat-vec of the cached projection against one S1S2 matrix.
    void project_s1s2(
            const std::vector<double>& src,
            int n_max, int n_min, int first_photon, int n_bins,
            double* out
    ) const;


protected:

    /// Maximum number of photons in the SgSr histogram
    unsigned int _n_2d_max = 300;

    /// Minimum number of photons
    unsigned int _n_2d_min = 3;

    /// Background in the first channel (in FRET green channel)
    double _bg_ch1 = 0.0;

    /// Background in the second channel (in FRET red channel)
    double _bg_ch2 = 0.0;

    /// S1S2 histogram, a histogram of the counts in channel 1 and channel 2
    /// in a FRET experiment ch1 and ch2 are green and red, respectively.
    std::vector<double> _S1S2;

    /// Probability P(F) of having a certain amount of photons
    std::vector<double> pF;


public:

    /*!
     * \brief Computes the S1S2 histogram.
     *
     * This function computes the S1S2 histogram based on the specified parameters,
     * such as the maximum and minimum number of photons, background levels, and
     * the probability distribution.
     */
    void evaluate();

    /*!
     * \brief Constructor for creating a new Pda object.
     *
     * Initializes a Pda object for computing Photon Distribution Analysis histograms.
     *
     * @param hist2d_nmax The maximum number of photons.
     * @param hist2d_nmin The minimum number of photons considered.
     * @param background_ch1 Background level in the first channel (green channel).
     * @param background_ch2 Background level in the second channel (red channel).
     * @param pF Probability distribution of having a certain number of photons.
     * @param implementation The PdaImplementation to use (default or optimized).
     */
    Pda(
        int hist2d_nmax=300,
        int hist2d_nmin=5,
        double background_ch1=0.0,
        double background_ch2=0.0,
        std::vector<double> pF = std::vector<double>(),
        PdaImplementation implementation = PdaImplementation::PDA_DEFAULT
    ){
        set_max_number_of_photons(std::abs(hist2d_nmax));
        _n_2d_min = std::abs(hist2d_nmin);
        _histogram_function = new PdaCallback();
        _bg_ch1 = background_ch1;
        _bg_ch2 = background_ch2;
        setPF(pF.data(), static_cast<int>(pF.size()));
        _implementation = implementation;
    }

    ~Pda() {
        delete _histogram_function;
    }

    /// Pda owns _histogram_function and a PdaCallback cannot be cloned, so a
    /// copy would double-free it.
    Pda(const Pda&) = delete;
    Pda& operator=(const Pda&) = delete;


    /*!
     * \brief Appends a species to the Pda object.
     *
     * A species is defined by the amplitude (fraction) and the probability
     * of detecting a photon in the first detection channel.
     *
     * @param amplitude The amplitude (fraction) of the species.
     * @param probability_ch1 The probability of detecting the species in the
     *                        first detection channel.
     */
    void append(double amplitude, double probability_ch1) {
        _is_valid_sgsr = false;
        _amplitudes.push_back(amplitude);
        _probability_ch1.push_back(probability_ch1);
    }

    /*!
     * \brief Clears the model and removes all species from the Pda object.
     */
    void clear_probability_ch1() {
        _is_valid_sgsr = false;
        _amplitudes.clear();
        _probability_ch1.clear();
    }

    /*!
     * \brief Returns the amplitudes of the species.
     *
     * @param output_view[out] A C type array containing the amplitude of the species.
     * @param n_output[out] The number of species.
     */
    void get_amplitudes(double **output_view, int *n_output) {
        *n_output = static_cast<int>(_amplitudes.size());
        *output_view = _amplitudes.data();
    }

    /*!
     * \brief Sets the amplitudes of the species.
     *
     * @param input[in] A C type array that contains the amplitude of the species.
     * @param n_input[in] The number of species.
     */
    void set_amplitudes(double *input, int n_input) {
        _amplitudes.clear();
        _is_valid_sgsr = false;
        for(int i = 0; i < n_input; i++)
            _amplitudes.emplace_back(input[i]);
    }

    /*!
     * \brief Set the callback (cb) for the computation of a 1D histogram.
     *
     * The cb function reduces two-dimensional values, i.e., the intensity in
     * channel (ch1) and ch2, to a one-dimensional number. The cb is used to
     * compute either FRET efficiencies, etc.
     *
     * @param cb[in] Object that computes the value on a 1D histogram.
     */
    void set_callback(PdaCallback* cb){
        if(cb == _histogram_function) return;
        delete _histogram_function;
        _histogram_function = cb;
        _hist1d_valid = false;
    }

    /*!
     * \brief Get the current PDA implementation.
     *
     * @return The current PdaImplementation.
     */
    PdaImplementation get_implementation() const {
        return _implementation;
    }

    /*!
     * \brief Set the PDA implementation.
     *
     * @param impl The PdaImplementation to use.
     */
    void set_implementation(PdaImplementation impl) {
        _implementation = impl;
        _is_valid_sgsr = false;
    }

public:

    /*!
     * Returns the S1S2 matrix that contains the photon counts in the two
     * channels.
     *
     * The matrix is recomputed first if any model parameter changed since the
     * last evaluate(). Row is channel 1, column is channel 2 -- see the class
     * documentation for the index convention.
     *
     * @param output[out] the S1S2 matrix
     * @param n_output1[out] dimension 1 of the matrix (channel 1)
     * @param n_output2[out] dimension 2 of the matrix (channel 2)
     */
    void get_S1S2_matrix(double **output, int *n_output1, int *n_output2){
        if(!_is_valid_sgsr) evaluate();
        auto* t = (double *) malloc(_S1S2.size() * sizeof(double));
        std::memcpy(t, _S1S2.data(), _S1S2.size() * sizeof(double));
        *output = t;
        *n_output1 = int (_n_2d_max + 1);
        *n_output2 = int (_n_2d_max + 1);
    }

    /*!
     * Set the theoretical probability spectrum of detecting a photon in the
     * first channel
     *
     * The probability spectrum is an interleaved array of the amplitudes and
     * the probabilities of detecting a photon in the first channel
     *
     * @param input[in] a C type array containing the probability spectrum
     * @param n_input[in] the number of array elements
     */
    void set_probability_spectrum_ch1(double *input, int n_input){
        _is_valid_sgsr = false;
        _amplitudes.clear();
        _probability_ch1.clear();
        int n_components = n_input / 2;
        for(int i=0; i < n_components; i++){
            double amplitude = input[2 * i];
            double probability_green = input[(2 * i) + 1];
            _amplitudes.emplace_back(amplitude);
            _probability_ch1.emplace_back(probability_green);
        }
    }

    /*!
     * \brief Returns the amplitudes of the species.
     *
     * @param output_view[out] A C type array containing the amplitude of the species.
     * @param n_output[out] The number of species.
     */
    void get_probabilities_ch1(double **output_view, int *n_output) {
        *n_output = static_cast<int>(_probability_ch1.size());
        *output_view = _probability_ch1.data();
    }

    /*!
     * \brief Sets the theoretical probabilities for detecting a species in the
     * first channel.
     *
     * @param input[in] A C type array that contains the probabilities of the species.
     * @param n_input[in] The number of species.
     */
    void set_probabilities_ch1(double *input, int n_input) {
        _probability_ch1.clear();
        _is_valid_sgsr = false;
        for(int i = 0; i < n_input; i++)
            _probability_ch1.emplace_back(input[i]);
    }


    /*!
     * \brief Get the theoretical probability spectrum of detecting a photon in the
     * first channel.
     *
     * The probability spectrum is an interleaved array of the amplitudes and
     * the probabilities of detecting a photon in the first channel.
     *
     * @param output[out] Array containing the probability spectrum.
     * @param n_output[out] Number of elements in the output array.
     */
    void get_probability_spectrum_ch1(double** output, int* n_output) {
        int n = (int)_amplitudes.size() * 2;
        auto temp = (double*) malloc(sizeof(double) * n);
        for(unsigned int i=0; i < _amplitudes.size(); i++){
            temp[2 * i] = _amplitudes[i];
            temp[2 * i + 1] = _probability_ch1[i];
        }
        *n_output = n;
        *output = temp;
    }

    /*!
     * \brief Set the probability P(F).
     *
     * @param input[in] A C type array containing the probability P(F).
     * @param n_input[in] The number of elements in the input array.
     */
    void setPF(double *input, int n_input){
if (is_verbose()) {
        std::clog << "-- Setting pF " << std::endl;
}
        _is_valid_sgsr = false;
        pF.assign(input, input + n_input);
    }

    /*!
     * \brief Get the probability P(F).
     *
     * @param output_view[out] A C type array containing the probability P(F).
     * @param n_output[out] The number of elements in the output array.
     */
    void getPF(double** output_view, int* n_output){
        *n_output = static_cast<int>(pF.size());
        *output_view = pF.data();
    }

    /*!
     * \brief Computes a 1D histogram from the 2D counting array of the two channels.
     *
     * This method calculates a 1D histogram based on the 2D counting array of the
     * two channels.
     *
     * @param histogram_x[out] Histogram X-axis.
     * @param n_histogram_x[out] Dimension of the X-axis.
     * @param histogram_y[out] Array containing the computed histogram.
     * @param n_histogram_y[out] Dimension of the histogram.
     * @param x_max[in] Maximum x-value of the histogram.
     * @param x_min[in] Minimum x-value of the histogram.
     * @param n_bins[in] Number of histogram bins.
     * @param log_x[in] If set to true (default is true), x-axis values are
     * logarithmically spaced; otherwise, linear spacing.
     * @param s1s2[in] Optional input for the S1S2 matrix. If empty (default), the
     * Pda object's own S1S2 matrix is used, re-evaluating the model first if any
     * parameter changed since the last evaluate(). If non-empty, this matrix is
     * projected instead and the model is left untouched. The matrix must be
     * square and laid out row-is-channel-1 (see the class documentation);
     * passing its transpose mirrors the resulting axis.
     * @param n_min[in] Minimum number of photons in the histogram. If -1 (default),
     * the number set when the Pda object was instantiated is used.
     * @param skip_zero_photon[in] When true, only s1s2 matrix elements i,j (i>0 and
     * j>0) are considered.
     * @param amplitudes[in] Species amplitudes (optional). Updates the s1s2 matrix
     * of the object.
     * @param probabilities_ch1[in] Theoretical probabilities of detecting the
     * species in channel 1 (optional). Updates the s1s2 matrix of the object.
     */
    void get_1dhistogram(
            double **histogram_x, int *n_histogram_x,
            double **histogram_y, int *n_histogram_y,
            double x_max=1000.0, double x_min=0.01, int n_bins=81,
            bool log_x=true,
            std::vector<double> s1s2 = std::vector<double>(),
            int n_min=-1,
            bool skip_zero_photon=true,
            std::vector<double> amplitudes = std::vector<double>(),
            std::vector<double> probabilities_ch1 = std::vector<double>()
    );

    /*!
     * \brief One projected 1D histogram per species, each at amplitude 1.
     *
     * The projection is a fixed linear map and the model is linear in the
     * species amplitudes, so the full histogram is just
     * `amplitudes @ rows`. For a fit that varies only the amplitudes -- the
     * usual species-fraction fit -- that turns an O(hist2d_nmax^2) re-evaluation
     * per iteration into an O(n_species * n_bins) dot product. Call this once,
     * then reweight; recall it when `probabilities_ch1`, `pF`, the background
     * or the binning change.
     *
     * @param histogram_x[out] Histogram X-axis, `n_bins` long.
     * @param n_histogram_x[out] Dimension of the X-axis.
     * @param output[out] Row-major `(n_species, n_bins)` matrix of projections.
     * @param n_output1[out] Number of species.
     * @param n_output2[out] Number of bins.
     * @param x_max,x_min,n_bins,log_x,n_min,skip_zero_photon As get_1dhistogram.
     */
    void get_1dhistogram_per_species(
            double **histogram_x, int *n_histogram_x,
            double **output, int *n_output1, int *n_output2,
            double x_max=1000.0, double x_min=0.01, int n_bins=81,
            bool log_x=true, int n_min=-1, bool skip_zero_photon=true
    );

    /*!
     * \brief Computes experimental histograms.
     *
     * This static method computes experimental histograms based on the provided TTTR data.
     *
     * @param tttr_data[in] TTTR data input.
     * @param s1s2[out] Output S1S2 matrix.
     * @param dim1[out] Output dimension 1 of the S1S2 matrix.
     * @param dim2[out] Output dimension 2 of the S1S2 matrix.
     * @param ps[out] Output PS matrix.
     * @param dim_ps[out] Output dimension of the PS matrix.
     * @param tttr_indices[out] Interleaved start/stop event indices of the time
     * windows that were counted, i.e. [start_0, stop_0, start_1, stop_1, ...].
     * Window w covers the half-open event range [start_w, stop_w).
     * @param n_tttr_indices[out] Length of `tttr_indices` (twice the number of
     * accepted time windows).
     * @param channels_1[in] Routing channel numbers used for the first channel in
     * the S1S2 matrix. Photons with this channel number are counted and increment
     * values in the S1S2 matrix.
     * @param channels_2[in] Routing channel numbers used for the second channel in
     * the S1S2 matrix. Photons with this channel number are counted and increment
     * values in the S1S2 matrix.
     * @param maximum_number_of_photons[in] Maximum number of photons in the computed
     * S1S2 matrix.
     * @param minimum_number_of_photons[in] Minimum number of photons in a time window
     * and in the S1S2 matrix.
     * @param minimum_time_window_length[in] Minimum length of a time window, in the
     * same physical unit as the macro-time resolution reported by the TTTR header
     * (milliseconds for the formats that record it in ms).
     *
     * @note The returned matrix uses the same row-is-channel-1 layout as the
     * model matrix, so it can be handed straight to get_1dhistogram().
     */
    static void compute_experimental_histograms(
        TTTR* tttr_data,
        double** s1s2, int* dim1, int* dim2,
        double** ps, int* dim_ps,
        int** tttr_indices, int* n_tttr_indices,
        std::vector<int> channels_1,
        std::vector<int> channels_2,
        int maximum_number_of_photons,
        int minimum_number_of_photons,
        double minimum_time_window_length
    );

    /*!
     * \brief Calculates p(G,R) for several ratios using the same P(F).
     *
     * This static method calculates p(G,R) for several ratios using the same P(F).
     *
     * @param S1S2[in] See sgsr_pN.
     * @param pF[in] Input P(F).
     * @param Nmax[in] Maximum number of photons.
     * @param background_ch1[in] Background in the green channel.
     * @param background_ch2[in] Background in the red channel.
     * @param p_ch1[in] Input probabilities for channel 1.
     * @param amplitudes[in] Corresponding amplitudes.
     */
    static void S1S2_pF(
        std::vector<double> &S1S2,
        std::vector<double> &pF,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes
    );

    /*!
     * \brief Calculates p(G,R) for several ratios using the same P(F) (optimized).
     *
     * This is an optimized version that uses OpenMP parallelization and dynamic thresholding.
     *
     * @param S1S2[in] See sgsr_pN.
     * @param pF[in] Input P(F).
     * @param Nmax[in] Maximum number of photons.
     * @param background_ch1[in] Background in the green channel.
     * @param background_ch2[in] Background in the red channel.
     * @param p_ch1[in] Input probabilities for channel 1.
     * @param amplitudes[in] Corresponding amplitudes.
     */
    static void S1S2_pF_optimized(
        std::vector<double> &S1S2,
        std::vector<double> &pF,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2,
        std::vector<double> &p_ch1,
        std::vector<double> &amplitudes
    );

    /*!
     * \brief Convolves the fluorescence matrix F1F2 with the background
     * to yield the signal matrix S1S2.
     *
     * This static method convolves the fluorescence matrix F1F2 with the background
     * to produce the signal matrix S1S2.
     *
     * @param S1S2[out] Output signal matrix.
     * @param F1F2[in] Input fluorescence matrix.
     * @param Nmax Maximum number of photons.
     * @param background_ch1 Background in the green channel.
     * @param background_ch2 Background in the red channel.
     */
    static void conv_pF(
        std::vector<double> &S1S2,
        const std::vector<double> &F1F2,
        unsigned int Nmax,
        double background_ch1,
        double background_ch2
    );

    /*!
     * \brief Writes a Poisson distribution with an average lam for 0..N
     * into a vector starting at a specified index.
     *
     * This static method generates a Poisson distribution with an average lam
     * for values 0 to N and writes it into the vector starting at the specified index.
     *
     * @param return_p[in,out] Vector to store the Poisson distribution.
     * @param start_idx Starting index in the vector.
     * @param lam Average lambda for the Poisson distribution.
     * @param return_dim Dimension of the vector.
     */
    static void poisson_0toN(
        std::vector<double> &return_p,
        int start_idx,
        double lam,
        int return_dim
    );


    /*!
     * \brief Get the maximum number of photons in the S1S2 matrix.
     *
     * This method returns the maximum number of photons that the S1S2 matrix can
     * accommodate.
     *
     * @return The maximum number of photons in the S1S2 matrix.
     */
    unsigned int get_max_number_of_photons() const{
        return _n_2d_max;
    }

    /*!
     * \brief Set the maximum number of photons in the S1S2 matrix.
     *
     * This method sets the maximum number of photons that the S1S2 matrix can
     * accommodate. It also resizes the internal S1S2 matrix accordingly.
     *
     * Note: The size of the pF array must agree with the maximum number of photons.
     *
     * @param nmax[in] The maximum number of photons.
     */
    void set_max_number_of_photons(unsigned int nmax){
        _n_2d_max = nmax;
        _S1S2.resize((_n_2d_max + 1) * (_n_2d_max + 1));
        _is_valid_sgsr = false;
    }

    /*!
     * \brief Get the minimum number of photons in the S1S2 matrix.
     *
     * This method retrieves the minimum number of photons that the S1S2 matrix can
     * have.
     *
     * @return The minimum number of photons.
     */
    unsigned int get_min_number_of_photons() const{
        return this->_n_2d_min;
    }

    /*!
     * \brief Set the minimum number of photons in the S1S2 matrix.
     *
     * This method sets the minimum number of photons that the S1S2 matrix can have.
     * It also invalidates the current S1S2 matrix to ensure it is recomputed with
     * the new minimum number of photons.
     *
     * @param nmin The minimum number of photons to set.
     */
    void set_min_number_of_photons(unsigned int nmin){
        this->_n_2d_min = nmin;
        _is_valid_sgsr = false;
    }

    /*!
     * \brief Get the background in the green channel.
     *
     * This method returns the background value in the green channel.
     *
     * @return The background value in the green channel.
     */
    double get_ch1_background() const{
        return _bg_ch1;
    }

    /*!
     * \brief Set the background in the first channel.
     *
     * This method sets the background value in the green channel.
     *
     * @param bg The background value to be set in the green channel.
     */
    void set_ch1_background(double bg){
        _is_valid_sgsr = false;
        _bg_ch1 = bg;
    }

    /*!
     * \brief Get the background in the second channel.
     *
     * This method returns the background value in the second channel.
     *
     * @return The background value in the second channel.
     */
    double get_ch2_background() const{
        return _bg_ch2;
    }

    /*!
     * \brief Set the background in the second channel.
     *
     * This method sets the background value in the second channel.
     *
     * @param br The background value to be set.
     */
    void set_ch2_background(double br) {
        _bg_ch2 = br;
        _is_valid_sgsr = false;
    }

    /*!
     * \brief Check if the S1S2 histogram is valid.
     *
     * This method returns true if the S1S2 histogram is considered valid,
     * meaning that it provides correct output for the given input parameters.
     * The validity is set to true after calling the evaluate method.
     *
     * @return True if the S1S2 histogram is valid, false otherwise.
     */
    bool is_valid_sgsr() const{
        return _is_valid_sgsr;
    }

    /*!
     * \brief Set the S1S2 histogram validity (for testing purposes).
     *
     * This method allows setting the validity of the S1S2 histogram explicitly,
     * primarily intended for testing purposes.
     *
     * @param v[in] True to set the S1S2 histogram as valid, false otherwise.
     */
    void set_valid_sgsr(bool v){
        _is_valid_sgsr = v;
    }

};


#endif //TTTRLIB_PDA_H
