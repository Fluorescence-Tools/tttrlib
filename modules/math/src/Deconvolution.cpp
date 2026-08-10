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

}  // namespace tttrlib
