// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayPhasor.h"
%}

// The native compute_phasor_bincounts takes std::vector<int>&, which every
// binding marshals differently and which R cannot pass at all: SWIG's R
// overload dispatcher wants a typed S4 proxy, and VectorInt32() hands back a
// bare externalptr it will not match. The house style for an array INPUT in
// this project is (T* IN_ARRAY1, int DIM1) -- a NumPy array in Python, a
// numeric vector in R, an int[] in Java, a TypedArray in JavaScript.
//
// Distinctive parameter names: %apply is global and keyed by name, so a
// generic (int* data, int n) here would redefine that pair for every other
// interface file too.
%apply (int* IN_ARRAY1, int DIM1) { (const int* bincounts_in, int n_bincounts_in) }

%extend DecayPhasor {
    // Same computation as compute_phasor_bincounts, reached through an array
    // rather than a std::vector proxy. Returns [g, s]; the not-a-value pair
    // (-1, -1) when the histogram holds too few photons, which is the native
    // method's contract and not an error.
    static std::vector<double> phasor_of_bincounts(
            const int* bincounts_in, int n_bincounts_in,
            double frequency = 1.0,
            int minimum_number_of_photons = 1,
            double g_irf = 1.0, double s_irf = 0.0) {
        std::vector<int> counts(bincounts_in, bincounts_in + n_bincounts_in);
        return DecayPhasor::compute_phasor_bincounts(
                counts, frequency, minimum_number_of_photons, g_irf, s_irf);
    }
}

%include "DecayPhasor.h"

%clear (const int* bincounts_in, int n_bincounts_in);
