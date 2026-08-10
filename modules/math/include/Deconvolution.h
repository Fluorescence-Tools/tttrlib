// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECONVOLUTION_H
#define TTTRLIB_DECONVOLUTION_H

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

/// 2-D Wiener deconvolution.
void wiener_deconvolve_2d(double* input, int n_input1, int n_input2,
                          double* psf, int n_psf1, int n_psf2, double balance,
                          double** output, int* n_output1, int* n_output2);

}  // namespace tttrlib

#endif  // TTTRLIB_DECONVOLUTION_H
