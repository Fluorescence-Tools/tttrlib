#ifndef FIT2X_DECAYFIT_H
#define FIT2X_DECAYFIT_H

#include <iostream>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>

#include "i_lbfgs.h"
#include "LvArrays.h"
#include "DecayConvolution.h"
#include "DecayStatistics.h"


struct DecayFitCorrections{

    double gamma = 0.0;
    double g = 1.0;
    double l1 = 0.0;
    double l2 = 0.0;
    double period = 1000;
    int convolution_stop = 0;

    void set_gamma(double v){
        if (v < 0.)
            gamma = 0.;        // 0 < gamma < 0.999
        else if (v > 0.999)
            gamma = 0.999;
        else
            gamma = v;
    }

    std::string str(){
        auto s = std::stringstream();
        s << "-- Correction factors:\n";
        s << "-- g-factor: " << g << std::endl;
        s << "-- l1, l2: " << l1 << ", " << l2 << std::endl;
        s << "-- period: " << period << std::endl;
        s << "-- convolution_stop: " << convolution_stop << std::endl;
        return s.str();
    }

    explicit DecayFitCorrections(
            double gamma = 0.0,
            double g = 1.0,
            double l1 = 0.0,
            double l2 = 0.0,
            double period = 1000,
            int convolution_stop = 0
    ) {
        this->gamma = gamma;
        this->g = g;
        this->l1 = l1;
        this->l2 = l2;
        this->period = period;
        this->convolution_stop = convolution_stop;
    }

};


struct DecayFitSettings{

    int fixedrho = 0;
    int softbifl = 0;
    int p2s_twoIstar = 0;
    int firstcall = 1;
    double penalty = 0.0;

    std::string str(){
        auto s = std::stringstream();
        s << "DECAYFITSETTINGS: " << std::endl;
        s << "-- fixedrho: " << fixedrho << std::endl;
        s << "-- softbifl: " << softbifl << std::endl;
        s << "-- p2s_twoIstar: " << p2s_twoIstar << std::endl;
        s << "-- firstcall: " << firstcall << std::endl;
        s << "-- penalty: " << penalty << std::endl;
        return s.str();
    }
};


struct DecayFitIntegrateSignals{

    DecayFitCorrections* corrections = nullptr;

    /// Total signal parallel
    double Sp = 0.0;

    /// Total signal perpendicular
    double Ss = 0.0;

    /// Total background signal parallel
    double Bp = 0.0;

    /// Total background signal perpendicular
    double Bs = 0.0;

    /// Total background
    double B = 0.0;

    /// expected <B> corresponding to the mean Bg signal
    double Bexpected = 0.0;


    double Fp(){
        double g = 1.0;
        if(corrections != nullptr){
            g = corrections->g;
        }
        if(g == 1.0){
            return (Sp - Bp);
        } else{
            return (Sp - g * Bp) / (1. - g);
        }
    }

    double Fs(){
        double g = 1.0;
        if(corrections != nullptr){
            g = corrections->g;
        }
        if(g == 1.0){
            return (Ss - Bs);
        } else{
            return (Ss - g * Bs) / (1. - g);
        }
    }

    double r(){
        double fp = Fp();
        double fs = Fs();
        double g = 1.0, l1 = 0.0, l2 = 0.0;
        if(corrections != nullptr){
            g = corrections->g;
            l1 = corrections->l1;
            l2 = corrections->l2;
        }
        double nom = (fp - g * fs);
        double denom = (fp * (1. - 3. * l2) + (2. - 3. * l1) * g * fs);
        return nom / denom;
    }

    double rho(double tau, double r0){
        double rh = tau / (r0 / r() - 1.);        // rho = tau/(r0/r-1)
        return std::max(rh, 1.e-4);
    }

    double rs(){
        double g = 1.0, l1 = 0.0, l2 = 0.0;
        if(corrections != nullptr){
            g = corrections->g;
            l1 = corrections->l1;
            l2 = corrections->l2;
        }
        return (Sp - g * Ss) / (Sp * (1. - 3. * l2) + (2. - 3. * l1) * g * Ss);
    }

    /*!
     * Computes the total number of photons in the parallel and perpendicular
     * detection channel for the background and the measured signal. The
     * computed number of photons are stored in the static variables
     * Sp, Ss, Bp, Bs.
     *
     * @param p[in] a pointer to a MParam object
     */
    void compute_signal_and_background(MParam *p);

    /*!
     * Normalizes the number of photons in the entire model function to the
     * number of experimental photons.
     *
     * Here, the Number of experimental photons is Sp + Ss (signal in parallel and
     * perpendicular). Sp and Ss are global variables that can be computed by
     * `compute_signal_and_background`.
     *
     * @param M[in,out] array containing the model function in Jordi format
     * @param Nchannels[in] number of channels in the experiment(half length of
     * M array)
     */
    void normM(double *M, int Nchannels);

    /*!
     * Normalizes a model function (that is already normalized to a unit area) to
     * the total number of photons in parallel and perpendicular,
     *
     * @param M[in,out] array containing the model function in Jordi format
     * @param s[in] a scaling factor by which the model function is divided.
     * @param Nchannels[in] the number of channels in the model function (half length of
     * M array)
     */
    void normM(double *M, double s, int Nchannels);


    /*!
     * Normalizes the number of photons in the model function for Ss and Sp
     * individually to the number of experimental photons in Ss and Sp.
     *
     * Here, the number of experimental photons are global variables that can be
     * computed by `compute_signal_and_background`.
     *
     * @param M array[in,out] containing the model function in Jordi format
     * @param Nchannels[in] number of channels in the experiment (half length of
     * M array)
     */
    void normM_p2s(double *M, int Nchannels);


    std::string str(){
        auto s = std::stringstream();
        s << "-- Signals: " << std::endl;
        s << "-- Bp, Bs: " << Bp << ", " << Bs << std::endl;
        s << "-- Sp, Ss: " << Sp << ", " << Ss << std::endl;
        s << "-- Fp, Fs: " << Fp() << ", " << Fs() << std::endl;
        s << "-- r: " << r() << std::endl;
        return s.str();
    }

    DecayFitIntegrateSignals(DecayFitCorrections *corrections = nullptr){
        this->corrections = corrections;
    }

};


class DecayFit{


public:

    /*!
     * @brief Function to compute a model fluorescence decay
     *
     * @param[in] param array containing the model parameters
     * @param[in] irf instrument response function in Jordi format (parallel, perpendicular)
     * @param bg[in] background pattern in Jordi format (parallel, perpendicular)
     * @param Nchannels[in] number of channels (half the length of the Jordi arrays)
     * @param dt[in] time difference between two consecutive counting channels
     * @param corrections[in] array with corrections (details see implementations)
     * @param mfunction[out] output array of the computed decay in Jordi format. The
     * output array has to have twice the number of channels. It needs to be allocated
     * by beforehand.
     * @return integer For reporting failures (default 0)
     */
    static int modelf(double *param,
                       double *irf,
                       double *bg,
                       int Nchannels,
                       double dt,
                       double *corrections,
                       double *mfunction
    ) {
        return 0;
    };

    /*!
     * @brief Target function (to minimize)
     *
     * Computes the model function and returns a score that quantifies
     * the discrepancy between the data and the model.
     *
     * @param x[in,out] a vector of length that that contains the model parameters
     * @param pv[in] a pointer to a MParam structure that contains the data and
     * a set of corrections.
     * @return a normalized chi2
     */
    static double targetf(double *x, void *pv){
        return 0.0;
    };


    /*!
     * Function that optimizes parameters of model23 to data.
     *
     * @param x[in,out] a vector of length that that contains the starting parameters
     * @param fixed an array that specifies if a parameter is optimized. If a value is set to 1,
     * the parameter is optimized.
     * @param p an instance of MParam that contains all relevant information
     * @return
     */
    static double fit(double* x, short* fixed, MParam* p){
        return 0.0;
    };


    /*!
     * Correct input parameters and compute values
     *
     * @param x[in,out] input output array (see implementations of derived classes)
     * @param xm[in,out] array that will contain the corrected parameters
     * @param corrections[in] array with correction parameters
     * @param return_r[in] if set to true (positive) computes the anisotropy and returns
     * the scatter corrected and the signal (no scatter correction) anisotropy and
     * writes the values to the input/output vector x.
     */
    static void correct_input(double *x, double *xm, LVDoubleArray *corrections, int return_r){};

};


#endif //FIT2X_DECAYFIT_H
