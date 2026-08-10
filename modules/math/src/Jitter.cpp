// SPDX-License-Identifier: BSD-3-Clause
#include "Jitter.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

#include "Random.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tttrlib {

namespace {

double* to_buffer(const std::vector<double>& values) {
    const std::size_t bytes = values.size() * sizeof(double);
    auto* buffer = static_cast<double*>(std::malloc(bytes == 0 ? sizeof(double) : bytes));
    if (buffer == nullptr) throw std::bad_alloc();
    if (bytes) std::memcpy(buffer, values.data(), bytes);
    return buffer;
}

/// The draw index for one photon on one axis.
///
/// Distinct per (photon, axis) and independent of how the loop is scheduled --
/// that is what makes a jittered analysis reproduce across thread counts. The
/// axis is folded in with a large odd stride rather than by `event * rank +
/// axis` so that two axes of one photon are far apart in the counter, which
/// matters for the weaker engines the library also offers.
inline std::uint64_t draw_index(std::size_t event, int axis) {
    return static_cast<std::uint64_t>(event)
           + static_cast<std::uint64_t>(axis) * 0x9E3779B97F4A7C15ULL;
}

}  // namespace

void jitter_coordinates(double* coordinates, std::size_t n_events, int rank,
                        const double* widths, std::uint32_t seed) {
    if (rank <= 0) throw std::invalid_argument("jitter_coordinates: rank must be positive");
    if (n_events > 0 && coordinates == nullptr)
        throw std::invalid_argument("jitter_coordinates: no coordinates");
    if (widths == nullptr) throw std::invalid_argument("jitter_coordinates: no bin widths");
    for (int axis = 0; axis < rank; ++axis)
        if (widths[axis] < 0.0)
            throw std::invalid_argument("jitter_coordinates: a bin width cannot be negative");

    const std::uint32_t key = seed != 0 ? seed : global_rng_seed();

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (long long event = 0; event < static_cast<long long>(n_events); ++event) {
        for (int axis = 0; axis < rank; ++axis) {
            const double width = widths[axis];
            if (width == 0.0) continue;
            // Uniform on [-w/2, w/2): the bin is a rectangle, so the dither is.
            const double u = Random::deterministic(
                key, draw_index(static_cast<std::size_t>(event), axis));
            coordinates[static_cast<std::size_t>(event) * rank + axis] += (u - 0.5) * width;
        }
    }
}

std::vector<double> events_from_counts(const double* counts,
                                       const std::vector<int>& shape,
                                       std::uint32_t seed) {
    if (shape.empty()) throw std::invalid_argument("events_from_counts: the grid has no axes");
    const int rank = static_cast<int>(shape.size());
    std::size_t n_grid = 1;
    for (int size : shape) {
        if (size <= 0) throw std::invalid_argument("events_from_counts: empty grid");
        n_grid *= static_cast<std::size_t>(size);
    }

    // Total first, so the output is allocated once.
    std::size_t total = 0;
    for (std::size_t flat = 0; flat < n_grid; ++flat) {
        const double value = counts[flat];
        if (value < -0.5)
            throw std::invalid_argument("events_from_counts: a negative count is not a photon count");
        total += static_cast<std::size_t>(std::llround(value < 0.0 ? 0.0 : value));
    }

    const std::uint32_t key = seed != 0 ? seed : global_rng_seed();
    std::vector<double> coordinates(total * static_cast<std::size_t>(rank));

    std::vector<int> index(static_cast<std::size_t>(rank), 0);
    std::size_t written = 0;
    for (std::size_t flat = 0; flat < n_grid; ++flat) {
        const double value = counts[flat];
        const long long n = std::llround(value < 0.0 ? 0.0 : value);
        for (long long k = 0; k < n; ++k) {
            for (int axis = 0; axis < rank; ++axis) {
                const double u = Random::deterministic(key, draw_index(written, axis));
                coordinates[written * static_cast<std::size_t>(rank) + axis] =
                    index[axis] + (u - 0.5);
            }
            ++written;
        }
        for (int axis = rank - 1; axis >= 0; --axis) {
            if (++index[static_cast<std::size_t>(axis)] < shape[static_cast<std::size_t>(axis)])
                break;
            index[static_cast<std::size_t>(axis)] = 0;
        }
    }
    return coordinates;
}

std::vector<double> counts_from_events(const double* coordinates, std::size_t n_events,
                                       int rank, const std::vector<int>& shape) {
    if (rank <= 0 || static_cast<std::size_t>(rank) != shape.size())
        throw std::invalid_argument("counts_from_events: coordinates and grid must share a rank");
    std::size_t n_grid = 1;
    for (int size : shape) {
        if (size <= 0) throw std::invalid_argument("counts_from_events: empty grid");
        n_grid *= static_cast<std::size_t>(size);
    }
    std::vector<double> counts(n_grid, 0.0);
    for (std::size_t event = 0; event < n_events; ++event) {
        std::size_t flat = 0;
        bool inside = true;
        for (int axis = 0; axis < rank; ++axis) {
            const double position = coordinates[event * static_cast<std::size_t>(rank) + axis];
            const long long bin = std::llround(position);
            if (bin < 0 || bin >= shape[static_cast<std::size_t>(axis)]) { inside = false; break; }
            flat = flat * static_cast<std::size_t>(shape[static_cast<std::size_t>(axis)])
                   + static_cast<std::size_t>(bin);
        }
        if (inside) counts[flat] += 1.0;
    }
    return counts;
}

void jitter_coordinates_2d(double* input, int n_input1, int n_input2,
                           double* widths, int n_widths, int seed,
                           double** output, int* n_output1, int* n_output2) {
    if (n_widths != n_input2)
        throw std::invalid_argument(
            "jitter_coordinates_2d: one bin width per coordinate axis");
    std::vector<double> copy(
        input, input + static_cast<std::size_t>(n_input1) * n_input2);
    jitter_coordinates(copy.data(), static_cast<std::size_t>(n_input1), n_input2,
                       widths, static_cast<std::uint32_t>(seed));
    *n_output1 = n_input1;
    *n_output2 = n_input2;
    *output = to_buffer(copy);
}

void events_from_counts_2d(double* input, int n_input1, int n_input2, int seed,
                           double** output, int* n_output1, int* n_output2) {
    const std::vector<int> shape{n_input1, n_input2};
    const std::vector<double> result =
        events_from_counts(input, shape, static_cast<std::uint32_t>(seed));
    *n_output1 = static_cast<int>(result.size() / 2);
    *n_output2 = 2;
    *output = to_buffer(result);
}

void counts_from_events_2d(double* input, int n_input1, int n_input2,
                           int n_output1, int n_output2,
                           double** output, int* n_out1, int* n_out2) {
    if (n_input2 != 2)
        throw std::invalid_argument("counts_from_events_2d: coordinates must be (n_events, 2)");
    const std::vector<int> shape{n_output1, n_output2};
    const std::vector<double> result = counts_from_events(
        input, static_cast<std::size_t>(n_input1), 2, shape);
    *n_out1 = n_output1;
    *n_out2 = n_output2;
    *output = to_buffer(result);
}

}  // namespace tttrlib
