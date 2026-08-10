// SPDX-License-Identifier: BSD-3-Clause
#include "Deconvolution.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>
#include <new>
#include <numeric>
#include <stdexcept>

#include "pocketfft/pocketfft_hdronly.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tttrlib {

namespace {

/// The smallest size at or above `n` that pocketfft factorises well.
///
/// A transform of a length whose largest prime factor is big falls back on
/// Bluestein's algorithm and costs several times more than a neighbouring
/// 5-smooth length. Padding is free here -- the padded region is zeros either
/// way -- so rounding up to a friendly length is pure gain.
std::size_t good_size(std::size_t n) {
    if (n <= 6) return n;
    for (std::size_t candidate = n;; ++candidate) {
        std::size_t rest = candidate;
        for (int factor : {2, 3, 5, 7}) {
            while (rest % factor == 0) rest /= factor;
        }
        if (rest == 1) return candidate;
    }
}

/// Row-major strides in bytes, for pocketfft.
pocketfft::stride_t byte_strides(const pocketfft::shape_t& shape, std::size_t itemsize) {
    pocketfft::stride_t strides(shape.size());
    std::ptrdiff_t running = static_cast<std::ptrdiff_t>(itemsize);
    for (std::size_t axis = shape.size(); axis-- > 0;) {
        strides[axis] = running;
        running *= static_cast<std::ptrdiff_t>(shape[axis]);
    }
    return strides;
}

/// A real array and its half-spectrum, on one padded grid.
///
/// Everything in this file works on the same padded shape, so the plan (shape,
/// strides, axes, spectrum size) is computed once and shared rather than
/// rebuilt per transform.
struct Grid {
    pocketfft::shape_t shape;
    pocketfft::shape_t axes;
    pocketfft::stride_t real_strides;
    pocketfft::stride_t spectrum_strides;
    std::size_t n_real = 0;
    std::size_t n_spectrum = 0;

    explicit Grid(const std::vector<int>& padded) {
        shape.assign(padded.begin(), padded.end());
        axes.resize(shape.size());
        std::iota(axes.begin(), axes.end(), 0);
        n_real = 1;
        for (std::size_t size : shape) n_real *= size;
        pocketfft::shape_t half = shape;
        half.back() = shape.back() / 2 + 1;
        n_spectrum = 1;
        for (std::size_t size : half) n_spectrum *= size;
        real_strides = byte_strides(shape, sizeof(double));
        spectrum_strides = byte_strides(half, sizeof(std::complex<double>));
    }

    void forward(const double* in, std::complex<double>* out) const {
        pocketfft::r2c(shape, real_strides, spectrum_strides, axes, /*forward=*/true,
                       in, out, 1.0);
    }

    void inverse(const std::complex<double>* in, double* out) const {
        // 1/n here, so a round trip is the identity.
        pocketfft::c2r(shape, spectrum_strides, real_strides, axes, /*forward=*/false,
                       in, out, 1.0 / static_cast<double>(n_real));
    }
};

/// Copy `source` (shape `source_shape`) into the corner of a zero grid.
void embed(const double* source, const std::vector<int>& source_shape,
           const Grid& grid, double* target) {
    std::fill(target, target + grid.n_real, 0.0);
    const std::size_t rank = source_shape.size();
    std::vector<int> index(rank, 0);
    std::size_t n_source = 1;
    for (int size : source_shape) n_source *= static_cast<std::size_t>(size);
    for (std::size_t flat = 0; flat < n_source; ++flat) {
        std::size_t offset = 0;
        for (std::size_t axis = 0; axis < rank; ++axis)
            offset = offset * grid.shape[axis] + static_cast<std::size_t>(index[axis]);
        target[offset] = source[flat];
        for (std::size_t axis = rank; axis-- > 0;) {
            if (++index[axis] < source_shape[axis]) break;
            index[axis] = 0;
        }
    }
}

/// Cut the "same"-mode window out of a full linear convolution.
///
/// The full convolution of an `n` array with an `m` kernel has `n + m - 1`
/// entries; "same" keeps `n` of them starting at `(m - 1) / 2`. That offset is
/// the whole compatibility surface with the reference implementations: get it
/// wrong by one and the result is the right image shifted by a pixel.
void crop_same(const double* padded, const Grid& grid, const std::vector<int>& shape,
               const std::vector<int>& psf_shape, double* out) {
    const std::size_t rank = shape.size();
    std::vector<int> start(rank);
    for (std::size_t axis = 0; axis < rank; ++axis) start[axis] = (psf_shape[axis] - 1) / 2;
    std::vector<int> index(rank, 0);
    std::size_t n_out = 1;
    for (int size : shape) n_out *= static_cast<std::size_t>(size);
    for (std::size_t flat = 0; flat < n_out; ++flat) {
        std::size_t offset = 0;
        for (std::size_t axis = 0; axis < rank; ++axis)
            offset = offset * grid.shape[axis]
                     + static_cast<std::size_t>(index[axis] + start[axis]);
        out[flat] = padded[offset];
        for (std::size_t axis = rank; axis-- > 0;) {
            if (++index[axis] < shape[axis]) break;
            index[axis] = 0;
        }
    }
}

/// Reverse a kernel along every axis, which is what turns a convolution into a
/// correlation -- the adjoint the Richardson-Lucy update needs.
std::vector<double> mirrored(const double* psf, const std::vector<int>& psf_shape) {
    std::size_t n = 1;
    for (int size : psf_shape) n *= static_cast<std::size_t>(size);
    std::vector<double> out(n);
    for (std::size_t flat = 0; flat < n; ++flat) out[flat] = psf[n - 1 - flat];
    return out;
}

void validate(const std::vector<int>& shape, const std::vector<int>& psf_shape) {
    if (shape.empty()) throw std::invalid_argument("deconvolution: the image has no axes");
    if (shape.size() != psf_shape.size())
        throw std::invalid_argument(
            "deconvolution: the point spread function must have the same rank as the image");
    for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (shape[axis] <= 0 || psf_shape[axis] <= 0)
            throw std::invalid_argument("deconvolution: every extent must be positive");
        if (psf_shape[axis] > shape[axis])
            throw std::invalid_argument(
                "deconvolution: the point spread function is larger than the image on "
                "some axis, which leaves nothing to restore");
    }
}

/// The padded grid, the PSF spectrum, and the mirrored PSF spectrum.
struct Plan {
    Grid grid;
    std::vector<std::complex<double>> psf_spectrum;
    std::vector<std::complex<double>> mirror_spectrum;
    std::vector<int> padded;

    Plan(const double* psf, const std::vector<int>& shape,
         const std::vector<int>& psf_shape)
        : grid(pad_for(shape, psf_shape)) {
        padded.resize(shape.size());
        for (std::size_t axis = 0; axis < shape.size(); ++axis)
            padded[axis] = static_cast<int>(grid.shape[axis]);

        // Normalised: Richardson-Lucy assumes the PSF conserves flux, and an
        // unnormalised kernel silently rescales the estimate every iteration.
        std::size_t n_psf = 1;
        for (int size : psf_shape) n_psf *= static_cast<std::size_t>(size);
        std::vector<double> unit(psf, psf + n_psf);
        const double total = std::accumulate(unit.begin(), unit.end(), 0.0);
        if (!(std::abs(total) > 0.0))
            throw std::invalid_argument(
                "deconvolution: the point spread function sums to zero");
        for (double& value : unit) value /= total;

        std::vector<double> buffer(grid.n_real);
        psf_spectrum.resize(grid.n_spectrum);
        mirror_spectrum.resize(grid.n_spectrum);
        embed(unit.data(), psf_shape, grid, buffer.data());
        grid.forward(buffer.data(), psf_spectrum.data());
        const std::vector<double> flipped = mirrored(unit.data(), psf_shape);
        embed(flipped.data(), psf_shape, grid, buffer.data());
        grid.forward(buffer.data(), mirror_spectrum.data());
    }

    static std::vector<int> pad_for(const std::vector<int>& shape,
                                    const std::vector<int>& psf_shape) {
        std::vector<int> padded(shape.size());
        for (std::size_t axis = 0; axis < shape.size(); ++axis)
            padded[axis] = static_cast<int>(
                good_size(static_cast<std::size_t>(shape[axis] + psf_shape[axis] - 1)));
        return padded;
    }
};

}  // namespace

std::vector<double> richardson_lucy(const double* image, const std::vector<int>& shape,
                                    const double* psf, const std::vector<int>& psf_shape,
                                    int n_iter, bool clip, double filter_epsilon,
                                    bool acceleration) {
    validate(shape, psf_shape);
    if (n_iter < 0) throw std::invalid_argument("deconvolution: n_iter must not be negative");

    std::size_t n_image = 1;
    for (int size : shape) n_image *= static_cast<std::size_t>(size);

    Plan plan(psf, shape, psf_shape);
    const Grid& grid = plan.grid;

    // The reference starts from a flat 0.5 rather than from the image: an
    // uninformative start is what keeps the fixed point from inheriting the
    // blurred image's own structure as a prior.
    std::vector<double> estimate(n_image, 0.5);
    std::vector<double> previous, previous_change, change;
    if (acceleration) {
        previous = estimate;
        previous_change.assign(n_image, 0.0);
        change.assign(n_image, 0.0);
    }

    std::vector<double> work(grid.n_real);
    std::vector<double> convolved(n_image);
    std::vector<std::complex<double>> spectrum(grid.n_spectrum);

    // Matches the reference exactly; it exists to keep the division finite
    // where the reblurred estimate has gone to zero.
    const double eps = 1e-12;

    for (int iteration = 0; iteration < n_iter; ++iteration) {
        std::vector<double> input = estimate;
        if (acceleration && iteration >= 2) {
            // Biggs-Andrews: step further along the direction the last two
            // iterations agreed on. The factor is their normalised inner
            // product, clamped, so a disagreement falls back to plain RL.
            double numerator = 0.0, denominator = 0.0;
            for (std::size_t i = 0; i < n_image; ++i) {
                numerator += change[i] * previous_change[i];
                denominator += previous_change[i] * previous_change[i];
            }
            double alpha = denominator > 0.0 ? numerator / denominator : 0.0;
            alpha = std::max(0.0, std::min(1.0, alpha));
            if (alpha > 0.0) {
                for (std::size_t i = 0; i < n_image; ++i) {
                    input[i] = estimate[i] + alpha * change[i];
                    if (input[i] < 0.0) input[i] = 0.0;
                }
            }
        }

        // conv = convolve(estimate, psf, 'same')
        embed(input.data(), shape, grid, work.data());
        grid.forward(work.data(), spectrum.data());
        for (std::size_t i = 0; i < grid.n_spectrum; ++i) spectrum[i] *= plan.psf_spectrum[i];
        grid.inverse(spectrum.data(), work.data());
        crop_same(work.data(), grid, shape, psf_shape, convolved.data());

        // ratio = image / conv, guarded
        std::vector<double> ratio(n_image);
        for (std::size_t i = 0; i < n_image; ++i) {
            const double denominator = convolved[i] + eps;
            if (filter_epsilon > 0.0 && denominator < filter_epsilon) ratio[i] = 0.0;
            else ratio[i] = image[i] / denominator;
        }

        // estimate *= correlate(ratio, psf, 'same')
        embed(ratio.data(), shape, grid, work.data());
        grid.forward(work.data(), spectrum.data());
        for (std::size_t i = 0; i < grid.n_spectrum; ++i) spectrum[i] *= plan.mirror_spectrum[i];
        grid.inverse(spectrum.data(), work.data());
        crop_same(work.data(), grid, shape, psf_shape, convolved.data());

        if (acceleration) {
            previous = estimate;
            previous_change = change;
        }
        for (std::size_t i = 0; i < n_image; ++i) input[i] *= convolved[i];
        if (acceleration) {
            for (std::size_t i = 0; i < n_image; ++i) change[i] = input[i] - previous[i];
        }
        estimate.swap(input);
    }

    if (clip) {
        for (double& value : estimate) {
            if (value > 1.0) value = 1.0;
            if (value < -1.0) value = -1.0;
        }
    }
    return estimate;
}

std::vector<double> wiener_deconvolve(const double* image, const std::vector<int>& shape,
                                      const double* psf,
                                      const std::vector<int>& psf_shape, double balance) {
    validate(shape, psf_shape);
    if (!(balance > 0.0))
        throw std::invalid_argument("deconvolution: balance must be positive");

    std::size_t n_image = 1;
    for (int size : shape) n_image *= static_cast<std::size_t>(size);

    Plan plan(psf, shape, psf_shape);
    const Grid& grid = plan.grid;
    std::vector<double> work(grid.n_real);
    std::vector<std::complex<double>> spectrum(grid.n_spectrum);
    embed(image, shape, grid, work.data());
    grid.forward(work.data(), spectrum.data());

    // H* / (|H|^2 + balance): the minimum-mean-square inverse, with `balance`
    // standing in for the noise-to-signal power ratio. Where the PSF has killed
    // a frequency, |H|^2 is tiny and the balance term keeps the filter finite
    // instead of amplifying the noise that is all that is left there.
    for (std::size_t i = 0; i < grid.n_spectrum; ++i) {
        const std::complex<double> h = plan.psf_spectrum[i];
        const double power = std::norm(h);
        spectrum[i] *= std::conj(h) / (power + balance);
    }
    grid.inverse(spectrum.data(), work.data());

    std::vector<double> out(n_image);
    crop_same(work.data(), grid, shape, psf_shape, out.data());
    return out;
}

// ---------------------------------------------------------------------------
// List mode: reconstructing from the photons themselves
// ---------------------------------------------------------------------------

namespace {

/// Multilinear interpolation of the PSF at a fractional offset.
///
/// The PSF is sampled, but a photon sits between samples. Rounding its position
/// to the nearest sample is what binning does, and avoiding that is the entire
/// point of working event-wise -- so the kernel is interpolated instead.
///
/// Interpolating is not free, and the cost is specific enough to state: linear
/// interpolation between two taps `1-t` and `t` is a convolution with a
/// two-point kernel of variance `t(1-t)`, so it *broadens* the PSF by up to
/// 0.25 px^2 at a half-sample offset and by nothing at all at a whole one. That
/// is a blur which oscillates with sub-pixel position -- exactly the quantity
/// event mode exists to preserve. Sampling the PSF `K` times finer shrinks it
/// to `t(1-t)/K^2`, which is why `oversampling` exists and why the callers
/// above this one do not leave it at 1.
///
/// @param oversampling  Samples per grid pixel in `psf`. `offset` is always in
///                      grid pixels regardless.
double psf_at(const double* psf, const std::vector<int>& psf_shape,
              const std::vector<double>& offset, int oversampling) {
    const std::size_t rank = psf_shape.size();
    std::vector<int> base(rank);
    std::vector<double> fraction(rank);
    for (std::size_t axis = 0; axis < rank; ++axis) {
        const double centre = (psf_shape[axis] - 1) / 2.0;
        const double position = offset[axis] * oversampling + centre;
        const double floored = std::floor(position);
        base[axis] = static_cast<int>(floored);
        fraction[axis] = position - floored;
        if (base[axis] < -1 || base[axis] >= psf_shape[axis]) return 0.0;
    }
    // Sum over the 2^rank corners of the cell the point falls in.
    double total = 0.0;
    const std::size_t corners = std::size_t(1) << rank;
    for (std::size_t corner = 0; corner < corners; ++corner) {
        double weight = 1.0;
        std::size_t flat = 0;
        bool inside = true;
        for (std::size_t axis = 0; axis < rank; ++axis) {
            const int step = (corner >> axis) & 1;
            const int index = base[axis] + step;
            if (index < 0 || index >= psf_shape[axis]) { inside = false; break; }
            weight *= step ? fraction[axis] : (1.0 - fraction[axis]);
            flat = flat * static_cast<std::size_t>(psf_shape[axis])
                   + static_cast<std::size_t>(index);
        }
        if (inside && weight > 0.0) total += weight * psf[flat];
    }
    return total;
}

}  // namespace

std::vector<double> richardson_lucy_events(const double* coordinates, int n_events,
                                           int rank, const double* weights,
                                           const std::vector<int>& shape,
                                           const double* psf,
                                           const std::vector<int>& psf_shape,
                                           int n_iter, int psf_oversampling) {
    if (rank <= 0 || static_cast<std::size_t>(rank) != shape.size()
        || shape.size() != psf_shape.size())
        throw std::invalid_argument(
            "richardson_lucy_events: coordinates, grid and PSF must share a rank");
    if (n_events < 0) throw std::invalid_argument("richardson_lucy_events: negative event count");
    if (n_iter < 0) throw std::invalid_argument("richardson_lucy_events: negative n_iter");
    if (psf_oversampling < 1)
        throw std::invalid_argument("richardson_lucy_events: psf_oversampling must be at least 1");

    std::size_t n_grid = 1;
    for (int size : shape) {
        if (size <= 0) throw std::invalid_argument("richardson_lucy_events: empty grid");
        n_grid *= static_cast<std::size_t>(size);
    }
    std::size_t n_psf = 1;
    for (int size : psf_shape) n_psf *= static_cast<std::size_t>(size);

    // Normalise so that the kernel sampled at *grid* spacing sums to one. That
    // is the condition flux conservation rests on -- `sum_x h(u - x) = 1` for a
    // photon at `u` -- and with an oversampled PSF it is not the same as the
    // array summing to one.
    //
    // Specifically it is not the Riemann sum `array_total / K^rank` either. That
    // differs from the comb sum by the PSF's truncation and by aliasing, which
    // is small but not zero: normalising by it left flux off by 1e-4 and pulled
    // reconstructed centroids 8e-4 px away from the photon that produced them.
    // So the comb is summed directly, taking every K-th sample outward from the
    // centre -- which for K = 1 is just the whole array, so the simple path is
    // unchanged.
    std::vector<double> kernel(psf, psf + n_psf);
    double kernel_total = 0.0;
    {
        // Offsets from the centre in whole grid pixels, on every axis at once.
        std::vector<int> lower(rank), upper(rank);
        bool on_grid = true;
        for (int axis = 0; axis < rank; ++axis) {
            if ((psf_shape[axis] - 1) % 2 != 0 && psf_oversampling > 1) on_grid = false;
            const int centre = (psf_shape[axis] - 1) / 2;
            lower[axis] = -(centre / psf_oversampling);
            upper[axis] = (psf_shape[axis] - 1 - centre) / psf_oversampling;
        }
        if (on_grid) {
            std::vector<int> step = lower;
            for (;;) {
                std::size_t flat = 0;
                for (int axis = 0; axis < rank; ++axis)
                    flat = flat * static_cast<std::size_t>(psf_shape[axis])
                           + static_cast<std::size_t>((psf_shape[axis] - 1) / 2
                                                      + step[axis] * psf_oversampling);
                kernel_total += kernel[flat];
                int axis = rank - 1;
                for (; axis >= 0; --axis) {
                    if (++step[axis] <= upper[axis]) break;
                    step[axis] = lower[axis];
                }
                if (axis < 0) break;
            }
        } else {
            // An even-sided PSF has no sample at its centre, so there is no comb
            // to sum; the Riemann sum is the best available and the caller gave
            // up a little accuracy by choosing that shape.
            kernel_total = std::accumulate(kernel.begin(), kernel.end(), 0.0);
            for (int axis = 0; axis < rank; ++axis) kernel_total /= psf_oversampling;
        }
    }
    if (!(kernel_total > 0.0))
        throw std::invalid_argument("richardson_lucy_events: the PSF sums to zero");
    for (double& value : kernel) value /= kernel_total;

    // Half-widths of the PSF support in *grid pixels*, which bound the loop
    // around each photon. An oversampled PSF spans fewer pixels than samples.
    std::vector<int> reach(rank);
    for (int axis = 0; axis < rank; ++axis)
        reach[axis] = (psf_shape[axis] / 2 + psf_oversampling - 1) / psf_oversampling + 1;

    std::vector<std::size_t> grid_strides(rank, 1);
    for (int axis = rank - 2; axis >= 0; --axis)
        grid_strides[axis] = grid_strides[axis + 1] * static_cast<std::size_t>(shape[axis + 1]);

    std::size_t n_support = 1;
    for (int axis = 0; axis < rank; ++axis)
        n_support *= static_cast<std::size_t>(2 * reach[axis] + 1);

    // Sensitivity: how much of the PSF centred on each grid point lands inside
    // the frame at all. Without dividing by it, the border is driven up to
    // account for photons that could never have been detected there. It is the
    // adjoint of the forward operator applied to a frame of ones, so it is
    // computed through the same `psf_at` rather than by summing the array --
    // that way it cannot drift out of step with the projection when the
    // sampling or the interpolation changes.
    std::vector<double> sensitivity(n_grid, 1.0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (long long flat = 0; flat < static_cast<long long>(n_grid); ++flat) {
        std::vector<int> index(rank);
        std::size_t remainder = static_cast<std::size_t>(flat);
        for (int axis = 0; axis < rank; ++axis) {
            index[axis] = static_cast<int>(remainder / grid_strides[axis]);
            remainder %= grid_strides[axis];
        }
        double total = 0.0;
        std::vector<int> step(rank, 0);
        std::vector<double> offset(rank);
        for (std::size_t k = 0; k < n_support; ++k) {
            bool inside = true;
            for (int axis = 0; axis < rank; ++axis) {
                const int neighbour = index[axis] + step[axis] - reach[axis];
                if (neighbour < 0 || neighbour >= shape[axis]) { inside = false; break; }
                offset[axis] = static_cast<double>(neighbour - index[axis]);
            }
            if (inside) total += psf_at(kernel.data(), psf_shape, offset, psf_oversampling);
            for (int axis = rank - 1; axis >= 0; --axis) {
                if (++step[axis] <= 2 * reach[axis]) break;
                step[axis] = 0;
            }
        }
        sensitivity[static_cast<std::size_t>(flat)] = total > 0.0 ? total : 1.0;
    }

    std::vector<double> estimate(n_grid, 1.0);
    std::vector<double> backprojection(n_grid);
    std::vector<double> forward(static_cast<std::size_t>(n_events), 0.0);

    for (int iteration = 0; iteration < n_iter; ++iteration) {
        // Forward: what the current estimate predicts at each photon's position.
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int event = 0; event < n_events; ++event) {
            const double* position = coordinates + static_cast<std::size_t>(event) * rank;
            std::vector<int> centre(rank);
            for (int axis = 0; axis < rank; ++axis)
                centre[axis] = static_cast<int>(std::floor(position[axis] + 0.5));
            double predicted = 0.0;
            std::vector<int> step(rank, 0);
            std::vector<double> offset(rank);
            for (std::size_t k = 0; k < n_support; ++k) {
                bool inside = true;
                std::size_t flat = 0;
                for (int axis = 0; axis < rank; ++axis) {
                    const int grid = centre[axis] + step[axis] - reach[axis];
                    if (grid < 0 || grid >= shape[axis]) { inside = false; break; }
                    flat += static_cast<std::size_t>(grid) * grid_strides[axis];
                    offset[axis] = position[axis] - grid;
                }
                if (inside) predicted += psf_at(kernel.data(), psf_shape, offset, psf_oversampling)
                                         * estimate[flat];
                for (int axis = rank - 1; axis >= 0; --axis) {
                    if (++step[axis] <= 2 * reach[axis]) break;
                    step[axis] = 0;
                }
            }
            forward[static_cast<std::size_t>(event)] = predicted;
        }

        // Backward: spread each photon's ratio over the PSF around it.
        std::fill(backprojection.begin(), backprojection.end(), 0.0);
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            std::vector<double> local(n_grid, 0.0);
#ifdef _OPENMP
#pragma omp for schedule(static) nowait
#endif
            for (int event = 0; event < n_events; ++event) {
                const double predicted = forward[static_cast<std::size_t>(event)];
                if (!(predicted > 0.0)) continue;
                const double weight =
                    (weights ? weights[event] : 1.0) / predicted;
                const double* position = coordinates + static_cast<std::size_t>(event) * rank;
                std::vector<int> centre(rank);
                for (int axis = 0; axis < rank; ++axis)
                    centre[axis] = static_cast<int>(std::floor(position[axis] + 0.5));
                std::vector<int> step(rank, 0);
                std::vector<double> offset(rank);
                for (std::size_t k = 0; k < n_support; ++k) {
                    bool inside = true;
                    std::size_t flat = 0;
                    for (int axis = 0; axis < rank; ++axis) {
                        const int grid = centre[axis] + step[axis] - reach[axis];
                        if (grid < 0 || grid >= shape[axis]) { inside = false; break; }
                        flat += static_cast<std::size_t>(grid) * grid_strides[axis];
                        offset[axis] = position[axis] - grid;
                    }
                    if (inside)
                        local[flat] += weight * psf_at(kernel.data(), psf_shape, offset, psf_oversampling);
                    for (int axis = rank - 1; axis >= 0; --axis) {
                        if (++step[axis] <= 2 * reach[axis]) break;
                        step[axis] = 0;
                    }
                }
            }
#ifdef _OPENMP
#pragma omp critical
#endif
            for (std::size_t i = 0; i < n_grid; ++i) backprojection[i] += local[i];
        }

        for (std::size_t i = 0; i < n_grid; ++i)
            estimate[i] *= backprojection[i] / sensitivity[i];
    }
    return estimate;
}

std::vector<double> scan_blur_kernel(double dwell_seconds, double jitter_seconds,
                                     double resolution_seconds, int oversampling,
                                     bool include_dwell) {
    if (!(dwell_seconds > 0.0))
        throw std::invalid_argument("scan_blur_kernel: the dwell time must be positive");
    if (jitter_seconds < 0.0 || resolution_seconds < 0.0)
        throw std::invalid_argument("scan_blur_kernel: jitter and resolution must not be negative");
    if (oversampling < 1)
        throw std::invalid_argument("scan_blur_kernel: oversampling must be at least 1");

    // Everything in pixels. The dwell rectangle is one pixel wide by definition:
    // that is what a pixel *is* on a scanning instrument -- the interval the beam
    // swept while that pixel was counting.
    const double dwell_width = include_dwell ? 1.0 : 0.0;
    const double jitter_sigma = jitter_seconds / dwell_seconds;
    const double quantisation_width = resolution_seconds / dwell_seconds;

    const double sample = 1.0 / oversampling;
    // The whole thing is built on a grid much finer than the output and
    // integrated down at the end. That is not fastidiousness: a component
    // narrower than one output sample has to be able to contribute its width
    // rather than snapping to a delta or, worse, to one whole sample.
    const int refine = 64;
    const double fine = sample / refine;

    const double variance = dwell_width * dwell_width / 12.0
                            + quantisation_width * quantisation_width / 12.0
                            + jitter_sigma * jitter_sigma;
    const double reach = std::max(4.0 * std::sqrt(variance), sample);
    const int half = static_cast<int>(std::ceil(reach / sample));
    const int n = 2 * half + 1;
    const int fine_half = half * refine + refine;
    const int fine_n = 2 * fine_half + 1;

    // Start from a delta and convolve the three components in.
    std::vector<double> signal(static_cast<std::size_t>(fine_n), 0.0);
    signal[static_cast<std::size_t>(fine_half)] = 1.0;

    auto convolve_rect = [&](double width) {
        if (!(width > 0.0)) return;
        // A rectangle of this width, sampled on the fine grid. Its taps are
        // fractional at the ends, so a width that is not a whole number of fine
        // steps still has exactly that width.
        const double half_width = width / 2.0;
        const int taps = static_cast<int>(std::ceil(half_width / fine));
        std::vector<double> weights(static_cast<std::size_t>(2 * taps + 1), 0.0);
        double total = 0.0;
        for (int d = -taps; d <= taps; ++d) {
            const double lo = std::max((d - 0.5) * fine, -half_width);
            const double hi = std::min((d + 0.5) * fine, half_width);
            const double w = std::max(0.0, hi - lo);
            weights[static_cast<std::size_t>(d + taps)] = w;
            total += w;
        }
        if (!(total > 0.0)) return;
        for (double& w : weights) w /= total;
        std::vector<double> out(signal.size(), 0.0);
        for (std::size_t i = 0; i < signal.size(); ++i) {
            if (signal[i] == 0.0) continue;
            for (int d = -taps; d <= taps; ++d) {
                const long long j = static_cast<long long>(i) + d;
                if (j < 0 || j >= static_cast<long long>(signal.size())) continue;
                out[static_cast<std::size_t>(j)] +=
                    signal[i] * weights[static_cast<std::size_t>(d + taps)];
            }
        }
        signal.swap(out);
    };

    auto convolve_gaussian = [&](double sigma) {
        if (!(sigma > 0.0)) return;
        const int taps = static_cast<int>(std::ceil(4.0 * sigma / fine));
        if (taps < 1) return;
        std::vector<double> weights(static_cast<std::size_t>(2 * taps + 1));
        double total = 0.0;
        for (int d = -taps; d <= taps; ++d) {
            const double x = d * fine / sigma;
            const double w = std::exp(-0.5 * x * x);
            weights[static_cast<std::size_t>(d + taps)] = w;
            total += w;
        }
        for (double& w : weights) w /= total;
        std::vector<double> out(signal.size(), 0.0);
        for (std::size_t i = 0; i < signal.size(); ++i) {
            if (signal[i] == 0.0) continue;
            for (int d = -taps; d <= taps; ++d) {
                const long long j = static_cast<long long>(i) + d;
                if (j < 0 || j >= static_cast<long long>(signal.size())) continue;
                out[static_cast<std::size_t>(j)] +=
                    signal[i] * weights[static_cast<std::size_t>(d + taps)];
            }
        }
        signal.swap(out);
    };

    convolve_rect(dwell_width);
    convolve_rect(quantisation_width);
    convolve_gaussian(jitter_sigma);

    // Integrate the fine grid down onto the output samples, rather than point
    // sampling it: the kernel multiplies pixel values, so each tap is what falls
    // within that pixel.
    //
    // By *overlap*, not by nearest bin. `refine` is even, so one fine sample per
    // output sample lands exactly on a bin boundary, and rounding sends every
    // one of them the same way -- a bias of half a fine step, which showed up as
    // a kernel whose mean was 1/4096 of a pixel off centre instead of zero. A
    // symmetric kernel that is not quite symmetric shifts the whole
    // reconstruction, so it is worth the few lines.
    std::vector<double> kernel(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < fine_n; ++i) {
        const double weight = signal[static_cast<std::size_t>(i)];
        if (weight == 0.0) continue;
        const double position = (i - fine_half) * fine;
        const double lo = position - fine / 2.0;
        const double hi = position + fine / 2.0;
        const int first = static_cast<int>(std::floor(lo / sample + 0.5));
        const int last = static_cast<int>(std::floor(hi / sample + 0.5));
        for (int b = first; b <= last; ++b) {
            const double edge_lo = (b - 0.5) * sample;
            const double edge_hi = (b + 0.5) * sample;
            const double overlap =
                std::max(0.0, std::min(hi, edge_hi) - std::max(lo, edge_lo));
            if (overlap <= 0.0) continue;
            const int bin = b + half;
            if (bin < 0 || bin >= n) continue;
            kernel[static_cast<std::size_t>(bin)] += weight * overlap / fine;
        }
    }
    const double total = std::accumulate(kernel.begin(), kernel.end(), 0.0);
    if (total > 0.0) for (double& value : kernel) value /= total;
    else kernel[static_cast<std::size_t>(half)] = 1.0;
    return kernel;
}

// ---------------------------------------------------------------------------
// Flat entry points
// ---------------------------------------------------------------------------

namespace {

double* to_buffer(const std::vector<double>& values) {
    const std::size_t bytes = values.size() * sizeof(double);
    auto* buffer = static_cast<double*>(std::malloc(bytes == 0 ? sizeof(double) : bytes));
    if (buffer == nullptr) throw std::bad_alloc();
    if (bytes) std::memcpy(buffer, values.data(), bytes);
    return buffer;
}

}  // namespace

void richardson_lucy_2d(double* input, int n_input1, int n_input2, double* psf,
                        int n_psf1, int n_psf2, int n_iter, bool clip,
                        double filter_epsilon, bool acceleration, double** output,
                        int* n_output1, int* n_output2) {
    const std::vector<int> shape{n_input1, n_input2};
    const std::vector<int> psf_shape{n_psf1, n_psf2};
    const std::vector<double> result = richardson_lucy(
        input, shape, psf, psf_shape, n_iter, clip, filter_epsilon, acceleration);
    *n_output1 = n_input1;
    *n_output2 = n_input2;
    *output = to_buffer(result);
}

void richardson_lucy_3d(double* input, int n_input1, int n_input2, int n_input3,
                        double* psf, int n_psf1, int n_psf2, int n_psf3, int n_iter,
                        bool clip, double filter_epsilon, bool acceleration,
                        double** output, int* n_output1, int* n_output2,
                        int* n_output3) {
    const std::vector<int> shape{n_input1, n_input2, n_input3};
    const std::vector<int> psf_shape{n_psf1, n_psf2, n_psf3};
    const std::vector<double> result = richardson_lucy(
        input, shape, psf, psf_shape, n_iter, clip, filter_epsilon, acceleration);
    *n_output1 = n_input1;
    *n_output2 = n_input2;
    *n_output3 = n_input3;
    *output = to_buffer(result);
}

void wiener_deconvolve_2d(double* input, int n_input1, int n_input2, double* psf,
                          int n_psf1, int n_psf2, double balance, double** output,
                          int* n_output1, int* n_output2) {
    const std::vector<int> shape{n_input1, n_input2};
    const std::vector<int> psf_shape{n_psf1, n_psf2};
    const std::vector<double> result =
        wiener_deconvolve(input, shape, psf, psf_shape, balance);
    *n_output1 = n_input1;
    *n_output2 = n_input2;
    *output = to_buffer(result);
}

void richardson_lucy_events_2d(double* input, int n_input1, int n_input2, double* psf,
                               int n_psf1, int n_psf2, int n_output1, int n_output2,
                               int n_iter, int psf_oversampling,
                               double** output, int* n_out1, int* n_out2) {
    if (n_input2 != 2)
        throw std::invalid_argument(
            "richardson_lucy_events_2d: coordinates must be (n_events, 2)");
    const std::vector<int> shape{n_output1, n_output2};
    const std::vector<int> psf_shape{n_psf1, n_psf2};
    const std::vector<double> result = richardson_lucy_events(
        input, n_input1, 2, nullptr, shape, psf, psf_shape, n_iter, psf_oversampling);
    *n_out1 = n_output1;
    *n_out2 = n_output2;
    *output = to_buffer(result);
}

void scan_blur_kernel_1d(double dwell_seconds, double jitter_seconds,
                         double resolution_seconds, int oversampling, bool include_dwell,
                         double** output, int* n_output) {
    const std::vector<double> kernel = scan_blur_kernel(
        dwell_seconds, jitter_seconds, resolution_seconds, oversampling, include_dwell);
    *n_output = static_cast<int>(kernel.size());
    *output = to_buffer(kernel);
}

}  // namespace tttrlib
