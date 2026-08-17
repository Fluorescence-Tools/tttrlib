// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECONVOLUTION_H
#define TTTRLIB_DECONVOLUTION_H

// Validation: A/B-TESTED 2026-08-17 -- richardson_lucy_2d benchmarked vs skimage: 1.8x, identical (bench_sciref.py,
//   check_sciref.py); richardson_lucy 2d/3d vs skimage.restoration.richardson_lucy (1e-15, all
//   PSF parities) and scipy direct convolution; wiener vs the NumPy formula only
//   (skimage.restoration.wiener is a different estimator); list-mode RL vs scipy
//   convolve/correlate. test/python/misc/test_math_ab_imaging.py.
//   Register: okf/testing/math-kernel-validation.md

// Deconvolution.h -- undoing a known blur.
//
// A microscope does not record the sample, it records the sample convolved with
// the instrument's point spread function. Deconvolution is the attempt to
// invert that, and the reason it is not a division is noise: the PSF suppresses
// high spatial frequencies, so dividing them back out amplifies whatever noise
// sits there by the same factor.
//
// Two answers are here, and they differ in what they assume about the noise:
//
//   richardson_lucy   Maximum likelihood under POISSON statistics, solved by a
//                     fixed-point iteration. This is the one that belongs in a
//                     photon-counting library: the noise in a confocal or
//                     widefield fluorescence image *is* Poisson, and the
//                     estimate stays non-negative by construction, which is the
//                     other thing a photon count owes reality. It has no
//                     regularisation term -- the iteration count is the
//                     regulariser, and running it too far amplifies noise into
//                     texture that looks like structure.
//
//   wiener            The linear one-shot answer under Gaussian noise, with a
//                     single balance parameter trading sharpness against noise.
//                     One FFT pair, no iteration; the right choice for a quick
//                     look or for data that is genuinely read-noise limited.
//
// ---------------------------------------------------------------------------
// Why the convolution is done the way it is
// ---------------------------------------------------------------------------
// Each Richardson-Lucy iteration needs two convolutions with the PSF, so a
// naive spatial implementation costs O(iterations * pixels * psf_pixels) --
// minutes for a 3-D stack. Here the PSF is transformed once and every iteration
// is four transforms of the padded image instead, which is O(iterations * n log
// n) and independent of the PSF size.
//
// The padding is not an optimisation but a correctness requirement: an FFT
// convolution is *circular*, so without padding to at least `n + m - 1` per
// axis the top of the image bleeds into the bottom. The result is then cropped
// the way a "same"-mode linear convolution crops, which is what keeps this
// numerically identical to the reference implementations.

#include <cstddef>
#include <vector>

namespace tttrlib {

/// Richardson-Lucy deconvolution: Poisson maximum likelihood, iterative.
///
/// @param image      Blurred image, row-major, shape `shape`. Values are counts
///                   or proportional to them; negatives are meaningless here.
/// @param shape      Extent per axis, slowest-varying first. Any rank.
/// @param psf        Point spread function, row-major, shape `psf_shape`. It is
///                   normalised internally, so its scale does not matter.
/// @param psf_shape  Extent of the PSF per axis; must have the same rank.
/// @param n_iter     Iterations. The iteration count *is* the regularisation:
///                   too few leaves the image blurred, too many turn noise into
///                   plausible-looking texture. Tens, not thousands.
/// @param clip       Clip the result to [-1, 1], as the reference does for
///                   images scaled to that range. Leave false for counts.
/// @param filter_epsilon
///                   Where the reblurred estimate falls below this, treat the
///                   ratio as zero instead of dividing. Guards the dark
///                   background of a sparse image, where the division is
///                   0/0-ish and amplifies nothing but noise. Zero disables it.
/// @param acceleration
///                   Biggs-Andrews vector extrapolation. Reaches a given
///                   likelihood in roughly a third of the iterations; off by
///                   default because it changes the iterate sequence, and the
///                   iteration count is a parameter people tune by eye.
std::vector<double> richardson_lucy(const double* image,
                                    const std::vector<int>& shape,
                                    const double* psf,
                                    const std::vector<int>& psf_shape,
                                    int n_iter = 30,
                                    bool clip = false,
                                    double filter_epsilon = 0.0,
                                    bool acceleration = false);

/// Wiener deconvolution: the linear minimum-mean-square answer.
///
/// @param balance Regularisation. Large values return the blurred image,
///                small values return noise; it is the ratio of assumed noise
///                power to signal power and is tuned by eye in practice.
std::vector<double> wiener_deconvolve(const double* image,
                                      const std::vector<int>& shape,
                                      const double* psf,
                                      const std::vector<int>& psf_shape,
                                      double balance = 0.1);

/// Richardson-Lucy from a photon list, without binning first.
///
/// A scanned photon-counting image is not a grid of measurements, it is a list
/// of detections; the grid is something the reader imposes afterwards. Binning
/// first throws away where in the pixel each photon actually landed and, worse,
/// bakes in a blur: during a pixel's dwell time the beam sweeps a whole pixel
/// width, so a binned pixel is a *line integral* along the scan, not a point
/// sample. Deconvolving that with the optical PSF alone under-corrects by a
/// known amount -- a rectangle one pixel wide, standard deviation 1/sqrt(12)
/// = 0.29 pixels, which for a diffraction-limited spot sampled at Nyquist is
/// comparable to the PSF itself.
///
/// This entry point takes the photons with their *fractional* coordinates,
/// which a scanned acquisition knows from each photon's macro time within its
/// line, and reconstructs without ever forming that rectangle. It is the
/// list-mode form of the same maximum-likelihood iteration:
///
///     f <- f / s * sum_i h(u_i - x) / (sum_x' h(u_i - x') f(x'))
///
/// where `u_i` is the i-th photon's position and `s` the sensitivity, which is
/// what keeps the borders -- where part of the PSF falls outside the frame --
/// from being driven up to compensate for photons that were never detectable.
///
/// @param coordinates  `(n_events, rank)` row-major, in grid units. Fractional
///                     positions are the point; integers would be a binning.
/// @param weights      Optional per-photon weight, or null for one each. A
///                     weight is how a caller applies a detection-efficiency
///                     correction, or bins coarsely in one axis while staying
///                     event-wise in another.
/// @param shape        Reconstruction grid.
/// @param psf          Point spread function, sampled `psf_oversampling` times
///                     per grid pixel and interpolated in between.
/// @param psf_oversampling
///                     Samples per grid pixel in `psf`. Pass a PSF sampled
///                     finer than the grid: interpolating between two taps is a
///                     convolution with a kernel of variance `t(1-t)`, so at
///                     one sample per pixel the reconstruction is broadened by
///                     up to 0.25 px^2 -- and by an amount that *varies with
///                     each photon's sub-pixel position*, which is precisely
///                     what working event-wise was meant to keep. Oversampling
///                     by `K` divides that by `K^2`; 8 makes it negligible
///                     against any real PSF.
std::vector<double> richardson_lucy_events(const double* coordinates, int n_events,
                                           int rank, const double* weights,
                                           const std::vector<int>& shape,
                                           const double* psf,
                                           const std::vector<int>& psf_shape,
                                           int n_iter = 30,
                                           int psf_oversampling = 1);

/// A one-dimensional kernel for the blur a scan adds on top of the optics.
///
/// Three contributions, and the first dominates so completely that the other
/// two are usually a rounding error -- which is worth knowing before anyone
/// spends effort on them:
///
///   dwell integration   The beam sweeps one pixel while the pixel counts, so
///                       the photon's origin is uniform across it: a rectangle
///                       of width 1 pixel, sigma = 0.289 px.
///   timing jitter       The detector's arrival-time uncertainty maps to a
///                       position through the scan speed: sigma_t / dwell
///                       pixels. At 100 ps and a 1 us dwell that is 1e-4 px.
///   time quantisation   The macro-time clock resolution, likewise: a rectangle
///                       of width resolution / dwell pixels.
///
/// @param dwell_seconds        Pixel dwell time.
/// @param jitter_seconds       Detector timing jitter, as a standard deviation.
/// @param resolution_seconds   Macro-time clock resolution.
/// @param oversampling         Samples per pixel in the returned kernel.
/// @param include_dwell        Set false when reconstructing event-wise, where
///                             the dwell rectangle was never applied.
std::vector<double> scan_blur_kernel(double dwell_seconds, double jitter_seconds = 0.0,
                                     double resolution_seconds = 0.0,
                                     int oversampling = 1,
                                     bool include_dwell = true);

// ---------------------------------------------------------------------------
// Flat entry points (these are what the language bindings expose)
// ---------------------------------------------------------------------------

/// 2-D Richardson-Lucy. See the n-D overload for what the parameters mean.
void richardson_lucy_2d(double* input, int n_input1, int n_input2,
                        double* psf, int n_psf1, int n_psf2,
                        int n_iter, bool clip, double filter_epsilon,
                        bool acceleration,
                        double** output, int* n_output1, int* n_output2);

/// 3-D Richardson-Lucy, for an axial stack.
void richardson_lucy_3d(double* input, int n_input1, int n_input2, int n_input3,
                        double* psf, int n_psf1, int n_psf2, int n_psf3,
                        int n_iter, bool clip, double filter_epsilon,
                        bool acceleration,
                        double** output, int* n_output1, int* n_output2,
                        int* n_output3);

/// 2-D Richardson-Lucy from a photon list. `coordinates` is `(n_events, 2)`.
void richardson_lucy_events_2d(double* input, int n_input1, int n_input2,
                               double* psf, int n_psf1, int n_psf2,
                               int n_output1, int n_output2, int n_iter, int psf_oversampling,
                               double** output, int* n_out1, int* n_out2);

/// The scan blur kernel, as a 1-D array.
void scan_blur_kernel_1d(double dwell_seconds, double jitter_seconds,
                         double resolution_seconds, int oversampling,
                         bool include_dwell, double** output, int* n_output);

/// 2-D Wiener deconvolution.
void wiener_deconvolve_2d(double* input, int n_input1, int n_input2,
                          double* psf, int n_psf1, int n_psf2, double balance,
                          double** output, int* n_output1, int* n_output2);

}  // namespace tttrlib

#endif  // TTTRLIB_DECONVOLUTION_H
