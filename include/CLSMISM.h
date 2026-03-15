#ifndef TTTRLIB_CLSMISM_H
#define TTTRLIB_CLSMISM_H

#include <cstddef>
#include <vector>
#include <utility>
#include <memory>

/*
 Minimal CLSMISM interface and implementation stub using FFT-based helpers.
 The heavy lifting is implemented in src/CLSMISM.cpp. This header declares
 a small API used by C++ examples and exposed to SWIG via ext/python/CLSM.i.
*/
class CLSMISM {
public:
    CLSMISM() = default;
    ~CLSMISM() = default;

    // Adaptive Pixel Reassignment (APR) reconstruction working on detector cubes
    // provided as plain double buffers (NumPy-compatible). The caller owns the
    // memory returned via `output`/return value and must free it with std::free.
    static void apr_reconstruction(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        double** output, int* out_dim1, int* out_dim2, int* out_dim3,
        int usf = 10, int ref_idx = -1, double filter_sigma = 0.0,
        int nz = 1, int n_det = -1);
    static double* apr_reconstruction(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        int usf = 10, int ref_idx = -1, double filter_sigma = 0.0,
        int nz = 1, int n_det = -1);

    // Focus-ISM reconstruction for background rejection operating on raw arrays.
    static void focus_reconstruction(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        double** output, int* out_dim1, int* out_dim2, int* out_dim3,
        double sigma_bound = 2.0, double threshold = 0.1, int calibration_size = 10,
        bool parallelize = false,
        int nz = 1, int n_det = -1,
        const double* detector_coords = nullptr, int detector_coords_len = 0);
    static double* focus_reconstruction(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        double sigma_bound = 2.0, double threshold = 0.1, int calibration_size = 10,
        bool parallelize = false,
        int nz = 1, int n_det = -1,
        const double* detector_coords = nullptr, int detector_coords_len = 0);

    // s2ISM (Adaptive Maximum Likelihood) reconstruction is not exposed from C++
    // when using array-only workflows. Higher-level routines should be implemented
    // in Python on top of the provided array-based primitives if required.
};

#endif // TTTRLIB_CLSMISM_H
