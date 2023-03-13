#include "DecayFit.h"
#include "fsconv.h"


void DecayFitIntegrateSignals::compute_signal_and_background(MParam *p) {
    LVI32Array *expdata = *(p->expdata);
    LVDoubleArray *bg = *(p->bg);
    int Nchannels_exp = expdata->length / 2;

    // total signal and background
    Sp = 0.; Ss = 0.;
    Bp = 0.; Bs = 0.;

    int i;
    for (i = 0; i < Nchannels_exp; i++) {
        Sp += expdata->data[i];
        Bp += bg->data[i];
    }
    for (; i < 2 * Nchannels_exp; i++) {
        Ss += expdata->data[i];
        Bs += bg->data[i];
    }
    B = std::max(1.0, Bp + Bs);
    Bp *= (Sp + Ss) /std::max(1., B);
    Bs *= (Sp + Ss) /std::max(1., B);
    Bexpected = corrections->gamma * B;
#if VERBOSE_TTTRLIB
    std::cout << "COMPUTE_SIGNAL_AND_BACKGROUND" << std::endl;
    std::cout << "-- Nchannels_exp:" << Nchannels_exp << std::endl;
    std::cout << "-- Bp, Bs: " << Bp << ", " << Bs << std::endl;
    std::cout << "-- Sp, Ss: " << Sp << ", " << Ss << std::endl;
    std::cout << "-- Bexpected, Bs: " << Bp << ", " << Bs << std::endl;
#endif
}


void DecayFitIntegrateSignals::normM(double *M, int Nchannels) {
    int i;
    double s = 0., Sexp = Sp + Ss;
    for (i = 0; i < 2 * Nchannels; i++) s += M[i];
    for (i = 0; i < 2 * Nchannels; i++) M[i] *= Sexp / s;
}

// if already normalized to s:
void DecayFitIntegrateSignals::normM(double *M, double s, int Nchannels) {
    int i;
    double Sexp = Sp + Ss;
    for (i = 0; i < 2 * Nchannels; i++) M[i] *= Sexp / s;
}

void DecayFitIntegrateSignals::normM_p2s(double *M, int Nchannels) {
    double s = 0.;

    int i;
    for (i = 0; i < Nchannels; i++) s += M[i];
    for (i = 0; i < Nchannels; i++) M[i] *= Sp / s;

    s = 0.;
    for (i = Nchannels; i < 2 * Nchannels; i++) s += M[i];
    for (i = Nchannels; i < 2 * Nchannels; i++) M[i] *= Ss / s;
}

