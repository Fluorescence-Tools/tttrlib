// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_WATERSHED_H
#define TTTRLIB_WATERSHED_H

// Validation: A/B-TESTED 2026-08-17 -- vs scikit-image 0.25.2 live (watershed labels and marching-squares segments
//   in raster order, exact, >1400 cases; the live sweeps need skimage >= 0.25.1, see the marker-seed note
//   below) and benchmarked vs it on 1024^2: watershed 1.4x, marching squares 3.5x, both identical
//   (bench_sciref.py, check_sciref.py). test/python/misc/test_watershed.py and
//   test/python/misc/test_math_ab_imaging.py.
//   Register: okf/testing/math-kernel-validation.md

// Watershed.h -- the two region-segmentation kernels: a priority-queue
// watershed flood, and iso-contour extraction by marching squares.
//
// Port of the reference Python implementation in ChiSurf
// (`core/roi/segmentation.py`, `_flood`/`_marching_squares`), but the
// contract here is **scikit-image, current upstream (>= 0.25.1)**: ChiSurf
// documents its `core/roi` as skimage-exact and its tests compare against
// skimage, so a merely-correct port fails them. Two places where ChiSurf's
// code and skimage's seemed to disagree were measured:
//
//   * the flood's marker seed. skimage 0.25.0 pushed every marker at `-inf`
//     (a marker at a maximum then floods before any other pixel is reachable);
//     ChiSurf pushes `image[marker]`. This port first followed 0.25.0 --
//     patching ChiSurf's `_flood` to `-inf` gave 0 diffs against it -- and
//     then upstream reverted (PR 7702, 0.25.1: "not actually a bug"), so it
//     now follows ChiSurf and current skimage: markers enter at their own
//     value. On a 1024^2 basin image with 200 markers 13% of the pixels
//     change basin between the two, so the version matters and the tests
//     say which they compare against;
//
//   * the marching-squares case bits are `ul=1, ur=2, ll=4, lr=8`, where
//     ChiSurf swaps the lower row (`lr=4, ll=8`) and inverts the ambiguous
//     square's orientations. A table built from skimage's `_get_contour_segments`
//     matches the compiled cython bit for bit, in order, across seeds 0-7 and
//     both vertex_connect_high values.
//
// Both kernels are "one call per analysis": padding, footprint, neighbour
// offsets and the crop all happen inside the call, which is what makes the
// output deterministic rather than a function of how the caller assembled the
// inputs.

#include <cstddef>

namespace tttrlib {

/*!
 * \brief Flood `image` from `markers`, assigning every pixel a basin label.
 *
 * A faithful port of `skimage.segmentation.watershed` for a 2D image and a
 * scalar connectivity, including the quirks that determine which of two
 * equally-cheap flood paths claims a plateau: the priority queue is ordered
 * on `(image value, entry age)`, markers enter at their own image value
 * (skimage >= 0.25.1), and labels are assigned at push time (the
 * plain, non-compact, no-line watershed). `mask` pixels that are zero are
 * never flooded and keep label 0, and any marker outside the mask is dropped
 * rather than left as an orphan label.
 *
 * \param image         (rows x cols) float64 landscape to flood; low is early.
 * \param n_rows, n_cols  extent of image, markers and mask.
 * \param markers       (rows x cols) int64 label image; nonzero entries are
 *                      the seeds, 0 means "not a marker".
 * \param mask          (rows x cols) uint8, 0/1; only 1 pixels are flooded.
 * \param connectivity  1 (faces) or 2 (faces and edges), matching
 *                      `scipy.ndimage.generate_binary_structure(2, connectivity)`.
 * \param out_labels    (rows x cols) int64 labels, allocated here. Values are
 *                      the marker values that each basin grew from.
 *
 * \throws std::invalid_argument if the shapes disagree, connectivity is out
 *         of range, or any input is null.
 */
void watershed(
        const double* image, int n_rows, int n_cols,
        const long long* markers, int m_rows, int m_cols,
        const unsigned char* mask, int k_rows, int k_cols,
        int connectivity,
        long long** out_labels, int* out_rows, int* out_cols);

/*!
 * \brief Iso-level contour segments through a 2D image, by marching squares.
 *
 * A faithful port of `skimage.measure._get_contour_segments`, which is the
 * level 0.25.0 upstream runs before assembling a polygon. Each 2x2 block is
 * classified by which corners lie above `level` with bits `ul=1, ur=2, ll=4,
 * lr=8`; blocks with any NaN corner are skipped; `vertex_connect_high`
 * resolves the two diagonal (ambiguous) cases 6 and 9 exactly as skimage
 * does. Endpoints are placed by linear interpolation along the block's edges.
 *
 * The segments come out in raster order -- upper-left block first, row-major
 * over the grid -- and that order is part of the contract, because the caller
 * assembles them into contours by chaining endpoints and skimage's
 * deterministic assembly depends on it. Each segment is four doubles
 * `[r0, c0, r1, c1]`, one endpoint pair.
 *
 * \param image                (rows x cols) float64.
 * \param n_rows, n_cols       extent of image.
 * \param level                the iso value to trace.
 * \param vertex_connect_high  nonzero: the high-value corners are the
 *                             connected set at a saddle; zero: the low ones.
 * \param out_segments         (n x 4) float64 segments, allocated here.
 * \param out_n_segments, out_n_cols  extent of the result; out_n_cols is 4.
 *
 * \throws std::invalid_argument if image is null or smaller than 2x2.
 */
void marching_squares(
        const double* image, int n_rows, int n_cols,
        double level, int vertex_connect_high,
        double** out_segments, int* out_n_segments, int* out_n_cols);

}  // namespace tttrlib

#endif  // TTTRLIB_WATERSHED_H
