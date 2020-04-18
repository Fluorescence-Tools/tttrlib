#ifndef TTTRLIB_PDA_H
#define TTTRLIB_PDA_H

#include <vector>
#include <iostream>
#include <cmath>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))


class PdaCallback{

public:

    virtual double run(double ch1, double ch2){
        return ch1 / ch2;
    }

    PdaCallback() = default;
    virtual ~PdaCallback() {};
};


class Pda {

private:
    /// Set to True of the SgSr histogram is valid, i.e., if the inputs match
    /// the values reported in the histogram
    bool _is_valid_sgsr = false;
    PdaCallback* _histogram_function;

    /// Probablity of detecting a green photon for the species
    std::vector<double> _probability_ch1;

    /// Amplitudes of the species
    std::vector<double> _amplitudes;


protected:

    /// Maximum number of photons in the SgSr histogram
    unsigned int _Nmax = 300;

    /// Minimum number of photons
    unsigned int _Nmin = 3;

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

    /// Computes the S1S2 histogram
    void evaluate();

    /*!
     * Constructor creating a new Pda object
     *
     * A Pda object can be used to compute Photon Distribution Analysis
     * histograms.
     *
     * @param hist2d_nmax the maximum number of photons
     * @param hist2d_nmin the minimum number of photons considered
     */
    Pda(
            int hist2d_nmax=300,
            int hist2d_nmin=5,
            double background_ch1=0.0,
            double background_ch2=0.0
    ){
        set_max_number_of_photons(std::abs(hist2d_nmax));
        _Nmin = std::abs(hist2d_nmin);
        _histogram_function = new PdaCallback();
        _bg_ch1 = background_ch1;
        _bg_ch2 = background_ch2;
    }

    ~Pda() = default;
    
    /// Add a species
    /*!
     * Appends a species.
     *
     * A species is defined by the probability of detecting a photon in the
     * first detection channel.
     *
     * @param amplitude the amplitude (fraction) of the species
     * @param probability_ch1 the probability of detecting the species in the
     * first detection channel
     */
    void append(double amplitude, double probability_ch1) {
        _is_valid_sgsr = false;
        _amplitudes.push_back(amplitude);
        _probability_ch1.push_back(probability_ch1);
    }

    /// Clears the model and removes all species
    void clear_probability_ch1() {
        _is_valid_sgsr = false;
        _amplitudes.clear();
        _probability_ch1.clear();
    }

    /*!
     * Returns the amplitudes of the species
     * @param output[out] A C type array containing the amplitude of the species
     * @param n_output[out] The number of species
     */
    void get_amplitudes(double **output, int *n_output) {
        *n_output = _amplitudes.size();
        *output = _amplitudes.data();
    }

    /*!
     * Sets the amplitudes of the species.
     *
     * @param input[in] A C type array that contains the amplitude of the species
     * @param n_input[in] The number of species
     */
    void set_amplitudes(double *input, int n_input) {
        _amplitudes.clear();
        _is_valid_sgsr = false;
        for(int i = 0; i < n_input; i++)
            _amplitudes.emplace_back(input[i]);
    }

    /*!
     * Set the callback (cb) for the computation of a 1D histogram.
     *
     * The cb function recudes two dimensional values, i.e., the intensity in
     * channel (ch1) and ch2 to a one dimensional number. The cb is used to
     * compute either FRET efficiencies, etc.
     *
     * @param callback[in] object that computes the value on a 1D histogram.
     */
    void set_callback(PdaCallback* cb){
        _histogram_function = cb;
    }

public:

    /*!
     * Returns the S1S2 matrix that contains the photon counts in the two
     * channels
     *
     * @param output[out] the S1S2 matrix
     * @param n_output1[out] dimension 1 of the matrix
     * @param n_output2[out] dimension 2 of the matrix
     */
    void get_S1S2_matrix(double **output, int *n_output1, int *n_output2){
        if(!_is_valid_sgsr) evaluate();
        auto* t = (double *) malloc(_S1S2.size() * sizeof(double));
        for(int i = 0; i < _S1S2.size(); i++) t[i] = _S1S2[i];
        *output = t;
        *n_output1 = _Nmax + 1;
        *n_output2 = _Nmax + 1;
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
     * Returns the amplitudes of the species
     * @param output[out] A C type array containing the amplitude of the species
     * @param n_output[out] The number of species
     */
    void get_probabilities_ch1(double **output, int *n_output) {
        *n_output = _probability_ch1.size();
        *output = _probability_ch1.data();
    }

    /*!
     * Sets the theoretical probabilities for detecting a the species in the
     * first channel.
     *
     * @param input[in] A C type array that contains the probabilities of the species
     * @param n_input[in] The number of species
     */
    void set_probabilities_ch1(double *input, int n_input) {
        _probability_ch1.clear();
        _is_valid_sgsr = false;
        for(int i = 0; i < n_input; i++)
            _probability_ch1.emplace_back(input[i]);
    }


    /*!
     * Get the theoretical probability spectrum of detecting a photon in the
     * first channel
     *
     * The probability spectrum is an interleaved array of the amplitudes and
     * the probabilities of detecting a photon in the first channel
     *
     * @param output[out] array containing the probability spectrum
     * @param n_output[out] number of elements in the output array
     */
    void get_probability_spectrum_ch1(double **output, int *n_output) {
        int n = (int)_amplitudes.size() * 2;
        auto temp = (double*) malloc(sizeof(double) * n);
        for(int i=0; i < _amplitudes.size(); i++){
            temp[2 * i] = _amplitudes[i];
            temp[2 * i + 1] = _probability_ch1[i];
        }
        *n_output = n;
        *output = temp;
    }

    /*!
     * Set the probability P(F)
     *
     * @param input[in]
     * @param n_input[in]
     */
    void setPF(double *input, int n_input){
        _is_valid_sgsr = false;
        pF.assign(input, input + n_input);
    }

    /// Set the probability P(F)
    void getPF(double **output, int *n_output){
        *n_output = pF.size();
        *output = pF.data();
    }

    /*!
    * Returns a one dimensional histogram of the 2D counting array of the
    * two channels.
    *
    * @param histogram_x[out] histogram X axis
    * @param n_histogram_x[out] dimension of the x-axis
    * @param histogram_y[out] array containing the computed histogram
    * @param n_histogram_y[out] dimension of the histogram
    * @param xmax[in] maximum x value of the histogram
    * @param xmin[in] minimum x value of the histogram
    * @param nbins[int] number of histogram bins
    * @param log_x[in] If set to true (default is true) the values on the
    * x-axis are logarithmically spaced otherwise they have a linear spacing.
    * @param input[in] Optional input for the S1S2 matrix. If this is set
    * to a nullptr (default) the S1S2 matrix of the Pda object is used to
    * compute the 1D histogram. If this is not set to nullptr and both dimensions
    * set by n_input1 and n_input2 are larger than zero. The input is used as
    * S1S2 matrix. The input matrix must be quadratic.
    * @param n_input1[in] First dimension of the S1S2 matrix.
    * @param n_input2[in] Second dimension of the S1S2 matrix.
    * @param n_max[in] Maximum number of photons in the histogram. If set to -1
    * (default is -1) the minimum number set when the Pda object was created
    * is used. Otherwise, the minimum of n_max and the number when the object was
    * created is used.
    * @param n_min[in] Minimum number of photons in the histogram. If set to -1
    * the number set when the Pda object was instancitated is used.
    * @param skip_zero_photon[in] When this option is set to true only elements
    * of the s1s2 matrix i,j (i>0 and j>0) are considered.
    */
    void get_1dhistogram(
            double **histogram_x, int *n_histogram_x,
            double **histogram_y, int *n_histogram_y,
            double xmax=1000.0, double xmin=0.01, int nbins=81,
            bool log_x=true,
            double *input = nullptr, int n_input1=0, int n_input2=0,
            int n_max=-1, int n_min=-1,
            bool skip_zero_photon=true
    );

    /// The maximum number of photons in the SgSr matrix
    unsigned int get_max_number_of_photons() const{
        return _Nmax;
    }

    /*!
     * Set the maximum number of photons in the S1S2 matrix
     *
     * Note: the size of the pF array must agree with the maximum number
     * of photons!
     *
     * @param nmax[in] the maximum number of photons
     */
    void set_max_number_of_photons(unsigned int nmax){
        _Nmax = nmax;
        _S1S2.resize((_Nmax + 1) * (_Nmax + 1));
        _is_valid_sgsr = false;
    }

    /// The minimum number of photons in the SgSr matrix
    unsigned int get_min_number_of_photons() const{
        return this->_Nmin;
    }

    /// Set the minimum number of photons in the SgSr matrix
    void set_min_number_of_photons(unsigned int nmin){
        this->_Nmin = nmin;
        _is_valid_sgsr = false;
    }

    /// Get the background in the green channel
    double get_ch1_background() const{
        return _bg_ch1;
    }

    /// Set the background in the green channel
    void set_ch1_background(double bg){
        _is_valid_sgsr = false;
        _bg_ch1 = bg;
    }

    /// Get the background in the red channel
    double get_ch2_background() const{
        return _bg_ch2;
    }

    /// Set the background in the red channel
    void set_ch2_background(double br) {
        _bg_ch2 = br;
        _is_valid_sgsr = false;
    }

    /// Returns true if the
    /// SgSr histogram is valid, i.e., if output is correct
    /// for the input parameter. This value is set to true by evaluate.
    bool is_valid_sgsr() const{
        return _is_valid_sgsr;
    }

    /// Set the SgSr histogram to valid (only used for testing)
    void set_valid_sgsr(bool v){
        _is_valid_sgsr = v;
    }

};


namespace PdaFunctions {

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
     * @param amplitudes corresponding amplitudes
     */
    void S1S2_pF(
            std::vector<double> &SgSr,
            std::vector<double> &pF,
            unsigned int Nmax,
            double Bg,
            double Br,
            std::vector<double> &pg_theor,
            std::vector<double> &amplitudes);

    /*!
     * Convolves the Fluorescence matrix F1F2 with the background
     * to yield the signal matrix S1S2
     *
     * @param S1S2[out]
     * @param F1F2[in]
     * @param Nmax
     * @param Bg
     * @param Br
     */
    void conv_pF(
            std::vector<double> &SgSr,
            const std::vector<double> &FgFr,
            unsigned int Nmax,
            double Bg,
            double Br
    );

    /*!
    * Writes a Poisson distribution with an average lam, for 0..N
    * into a vector starting at a specified index.
    *
    * @param return_p
    * @param lam
    * @param return_dim
    */
    void poisson_0toN(
            std::vector<double> &return_p,
            int start_idx,
            double lam,
            int return_dim
    );

}


#endif //TTTRLIB_PDA_H
