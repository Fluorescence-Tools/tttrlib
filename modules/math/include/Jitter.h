// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_JITTER_H
#define TTTRLIB_JITTER_H

// Validation: A/B-TESTED 2026-08-17 -- vs np.histogram2d (counts round trip exact, jitter preserves the histogram,
//   dither uniform in +-w/2). test/python/misc/test_math_ab_imaging.py.
//   Register: okf/testing/math-kernel-validation.md

// Jitter.h -- the bridge between photons and algorithms that want floats.
//
// ---------------------------------------------------------------------------
// The rule this header exists to make cheap
// ---------------------------------------------------------------------------
// Every algorithm in this library has to be usable on photons. That is the data
// the library is for: a detection is a time and a channel, and any grid -- a
// pixel, a histogram bin, a lag channel -- is something a reader *imposed*
// afterwards, not something the instrument measured.
//
// Many useful algorithms are nevertheless written for continuous values:
// clustering, deconvolution, density estimation, dimensionality reduction. Two
// entry points, not one:
//
//   the standard form   takes the binned array, and is what a caller who
//                       already has an image or a histogram expects. Keep it,
//                       and keep it numerically identical to the reference
//                       implementation people will compare against.
//
//   the photon form     takes the detections, with their fractional positions,
//                       and never forms the grid at all. This is the one that
//                       is *correct* on photon data.
//
// When an algorithm genuinely cannot be reformulated event-wise -- some are
// closed-form on a grid and nothing else -- the fallback is not to bin and
// pretend. It is to **jitter**: give each photon a continuous position drawn
// uniformly across the interval its quantised coordinate stands for, and run
// the continuous algorithm on that.
//
// The gain is not about bias in a mean. For a mean the bin centre is already
// fine, and it is worth saying so, because the obvious argument for jittering
// is that one and the obvious argument is wrong. The gain is that binning
// **collapses distinct photons onto identical coordinates**, and a large family
// of algorithms -- clustering, nearest neighbours, density estimation, anything
// that measures a distance -- is not merely degraded by that but degenerate on
// it. Four thousand photons of a sigma = 3 px spot, binned to that grid:
//
//     nearest-neighbour distances that are exactly zero      98.4 %
//     mean nearest-neighbour distance, true / binned    0.117 / 0.018
//
// Jittered, the same photons give 0.1169 against a truth of 0.1170. The ties
// are gone and the distance distribution is the right one.
//
// The cost is an added variance of `width^2 / 12`. That is known, quotable and
// subtractable -- it is Sheppard's correction -- which is what makes it an
// acceptable price rather than a hidden one. Note that a photon which has been
// through a histogram and back pays it *twice*: once when rounding threw the
// position away, and once when the dither put a random one back. Only the
// original coordinates avoid both, which is why this is the fallback and the
// event-wise formulation is the answer wherever one exists.
//
// This is why the dither is *uniform across the bin* and not Gaussian: the
// quantisation it undoes is a rectangle, so a rectangle is what reproduces the
// distribution the value was drawn from. A Gaussian of matched variance leaks
// outside the bin the photon is known to be in.
//
// ---------------------------------------------------------------------------
// Reproducibility
// ---------------------------------------------------------------------------
// The dither runs through the library's central RNG (`Random.h`), so
// `TTTR_RNG_SEED` fixes it and a run is repeatable. It is drawn per photon from
// a counter-based stream rather than from sequential state, which means the
// result does not depend on thread count or iteration order -- a jittered
// analysis gives the same answer on 1 core and on 32.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tttrlib {

/// Dither quantised coordinates in place, uniformly across their bins.
///
/// @param coordinates `(n_events, rank)` row-major, in bin units. Modified.
/// @param n_events    Number of photons.
/// @param rank        Coordinates per photon.
/// @param widths      Bin width per axis, in the same units. A width of zero
///                    leaves that axis alone, which is how an axis that is
///                    already continuous -- a macro time, say -- is passed
///                    through untouched beside one that is not.
/// @param seed        Zero takes the library master seed, so `TTTR_RNG_SEED`
///                    governs. Any other value is an independent stream.
void jitter_coordinates(double* coordinates, std::size_t n_events, int rank,
                        const double* widths, std::uint32_t seed = 0);

/// Photons from a histogram: one coordinate per count, dithered in its bin.
///
/// The inverse of binning, as far as an inverse exists. It cannot recover where
/// in the bin each photon actually was -- that was destroyed -- but it restores
/// the *distribution*, which is what an algorithm downstream is estimating. Use
/// it to run the photon form of an algorithm on data that reached you already
/// binned, and to check the two forms against each other.
///
/// Counts are rounded to the nearest non-negative integer; a fractional
/// histogram is not a photon count and silently truncating it would hide that.
///
/// @param counts  Histogram, row-major, shape `shape`.
/// @param shape   Extent per axis, slowest-varying first.
/// @param seed    Zero takes the library master seed.
/// @return `(total_counts, rank)` row-major coordinates in bin units, each
///         within +-0.5 of its bin's index.
std::vector<double> events_from_counts(const double* counts,
                                       const std::vector<int>& shape,
                                       std::uint32_t seed = 0);

/// Bin photons back onto a grid -- the other direction, for comparison.
///
/// @param coordinates `(n_events, rank)` in grid units.
/// @param shape       Grid extent per axis. Photons outside are dropped.
std::vector<double> counts_from_events(const double* coordinates, std::size_t n_events,
                                       int rank, const std::vector<int>& shape);

// ---------------------------------------------------------------------------
// Flat entry points (these are what the language bindings expose)
// ---------------------------------------------------------------------------

/// Dither `(n_events, rank)` coordinates, returning a new array.
void jitter_coordinates_2d(double* input, int n_input1, int n_input2,
                           double* widths, int n_widths, int seed,
                           double** output, int* n_output1, int* n_output2);

/// Photons from a 2-D histogram, as `(total_counts, 2)`.
void events_from_counts_2d(double* input, int n_input1, int n_input2, int seed,
                           double** output, int* n_output1, int* n_output2);

/// Bin `(n_events, 2)` photons onto a grid.
void counts_from_events_2d(double* input, int n_input1, int n_input2,
                           int n_output1, int n_output2,
                           double** output, int* n_out1, int* n_out2);

}  // namespace tttrlib

#endif  // TTTRLIB_JITTER_H
