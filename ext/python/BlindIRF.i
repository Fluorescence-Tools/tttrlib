// SPDX-License-Identifier: BSD-3-Clause
%{#include "BlindIRF.h"%}
namespace tttrlib {};
%include "BlindIRF.h"

// The array form: an (n_bins x n_channels) decay stack in, the IRF stack of the
// same shape out -- one call, NumPy typemaps (okf/bindings/marshalling-cost.md).
// The std::vector form above stays for the other bindings and for callers with
// a flat list; this one adds nothing to the algorithm, only removes the reshape
// and the element-by-element marshalling the example writers ran into.
%apply(double* IN_ARRAY2, int DIM1, int DIM2) {(const double* blind_irf_data, int blind_irf_n_bins, int blind_irf_n_channels)}
%apply(double** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(double** blind_irf_out, int* blind_irf_dim1, int* blind_irf_dim2)}
%inline %{
void blind_irf_estimate_array(
        const double* blind_irf_data, int blind_irf_n_bins, int blind_irf_n_channels,
        double** blind_irf_out, int* blind_irf_dim1, int* blind_irf_dim2,
        double dt = 1.0, int rl_iterations = 500, int regularization = 3,
        int sg_window = 11, int sg_order = 3) {
    std::vector<double> data(blind_irf_data,
                             blind_irf_data + (size_t) blind_irf_n_bins * blind_irf_n_channels);
    std::vector<double> irf = tttrlib::blind_irf_estimate(
            data, blind_irf_n_bins, blind_irf_n_channels, dt, rl_iterations, regularization,
            sg_window, sg_order);
    double* out = (double*) malloc(std::max<size_t>(irf.size(), 1) * sizeof(double));
    if (!out) throw std::bad_alloc();
    std::copy(irf.begin(), irf.end(), out);
    *blind_irf_out = out;
    *blind_irf_dim1 = blind_irf_n_bins;
    *blind_irf_dim2 = blind_irf_n_channels;
}
%}
%clear (const double* blind_irf_data, int blind_irf_n_bins, int blind_irf_n_channels);
%clear (double** blind_irf_out, int* blind_irf_dim1, int* blind_irf_dim2);
