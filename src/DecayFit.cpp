#include "DecayFit.h"

void DecayFitIntegrateSignals::compute_signal_and_background(MParam *p) {
    LVI32Array *expdata = *(p->expdata);
    LVDoubleArray *bg = *(p->bg);
    int Nchannels_exp = expdata->length / 2;

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
    Bp *= (Sp + Ss) / std::max(1., B);
    Bs *= (Sp + Ss) / std::max(1., B);
    if (corrections != nullptr) {
        Bexpected = corrections->gamma * B;
    } else {
        Bexpected = 0.0;
    }

    if (is_verbose()) {
        std::cout << "COMPUTE_SIGNAL_AND_BACKGROUND" << std::endl;
        std::cout << "-- Nchannels_exp:" << Nchannels_exp << std::endl;
        std::cout << "-- Bp, Bs: " << Bp << ", " << Bs << std::endl;
        std::cout << "-- Sp, Ss: " << Sp << ", " << Ss << std::endl;
        std::cout << "-- Bexpected: " << Bexpected << std::endl;
    }
}


void DecayFitIntegrateSignals::normM(double *M, int Nchannels) {
    double s = 0.;
    double Sexp = Sp + Ss;
    for (int i = 0; i < 2 * Nchannels; i++) s += M[i];
    if (s <= 0.) {
        return;
    }
    for (int i = 0; i < 2 * Nchannels; i++) M[i] *= Sexp / s;
}

void DecayFitIntegrateSignals::normM(double *M, double s, int Nchannels) {
    double Sexp = Sp + Ss;
    if (s <= 0.) {
        return;
    }
    for (int i = 0; i < 2 * Nchannels; i++) M[i] *= Sexp / s;
}

void DecayFitIntegrateSignals::normM_p2s(double *M, int Nchannels) {
    double s = 0.;

    for (int i = 0; i < Nchannels; i++) s += M[i];
    if (s > 0.) {
        for (int i = 0; i < Nchannels; i++) M[i] *= Sp / s;
    }

    s = 0.;
    for (int i = Nchannels; i < 2 * Nchannels; i++) s += M[i];
    if (s > 0.) {
        for (int i = Nchannels; i < 2 * Nchannels; i++) M[i] *= Ss / s;
    }
}

