// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFitNExp.h"

#include "DecayConvolution.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

constexpr double kProbabilityFloor = 1.0e-300;
constexpr double kWeightFloor = 1.0e-15;
constexpr int kCoordinateGridIntervals = 24;

unsigned int batch_thread_count(std::size_t n_rows) {
    if (n_rows < 128) return 1;

    const char* enabled = std::getenv("TTTRLIB_USE_OPENMP");
    if (enabled != nullptr &&
        (enabled[0] == '0' || enabled[0] == 'f' || enabled[0] == 'F' ||
         enabled[0] == 'n' || enabled[0] == 'N')) {
        return 1;
    }

    unsigned int requested = 0;
    for (const char* name : {"TTTRLIB_NUM_THREADS", "OMP_NUM_THREADS"}) {
        const char* value = std::getenv(name);
        if (value == nullptr) continue;
        const int parsed = std::atoi(value);
        if (parsed > 0) {
            requested = static_cast<unsigned int>(parsed);
            break;
        }
    }
    if (requested == 0) requested = 4;
    const unsigned int useful = static_cast<unsigned int>((n_rows + 63) / 64);
    return std::max(1u, std::min(requested, useful));
}

struct ProfileResult {
    double nll = 0.0;
    int em_iterations = 0;
    bool em_converged = true;
    std::vector<double> weights;
    std::vector<double> probability;
};

void require_nonnegative_finite(const std::vector<double>& values,
                                const char* name) {
    for (double value : values) {
        if (!std::isfinite(value) || value < 0.0) {
            throw std::invalid_argument(std::string(name) +
                                        " must contain finite nonnegative values");
        }
    }
}

void normalize_probability(std::vector<double>& values, const char* name) {
    double sum = 0.0;
    for (double& value : values) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(std::string(name) +
                                     " produced a non-finite value");
        }
        value = std::max(0.0, value);
        sum += value;
    }
    if (!(sum > 0.0) || !std::isfinite(sum)) {
        throw std::invalid_argument(std::string(name) +
                                    " must have positive finite area");
    }
    const double inverse = 1.0 / sum;
    for (double& value : values) value *= inverse;
}

std::vector<double> pooled_counts(const std::vector<double>& data,
                                  std::size_t n_bins) {
    if (data.size() != n_bins && data.size() != 2 * n_bins) {
        throw std::invalid_argument(
                "data length must equal irf length or twice irf length (Jordi VV+VH)");
    }
    require_nonnegative_finite(data, "data");
    std::vector<double> pooled(n_bins, 0.0);
    if (data.size() == n_bins) {
        pooled = data;
    } else {
        for (std::size_t i = 0; i < n_bins; ++i)
            pooled[i] = data[i] + data[i + n_bins];
    }
    return pooled;
}

std::vector<double> convolved_component(double tau,
                                        const std::vector<double>& irf,
                                        const DecayFitNExpOptions& options) {
    const int n_bins = static_cast<int>(irf.size());
    const double period = options.period > 0.0
                              ? options.period
                              : static_cast<double>(n_bins) * options.dt;
    const int convolution_stop = options.convolution_stop < 0
                                     ? n_bins - 1
                                     : std::min(options.convolution_stop,
                                                n_bins - 1);
    std::vector<double> component(irf.size(), 0.0);
    double spectrum[2] = {1.0, tau};
    fconv_per_cs(component.data(), spectrum, const_cast<double *>(irf.data()), 1,
                 n_bins - 1, n_bins, period, convolution_stop, options.dt);
    normalize_probability(component, "reconvolved exponential");
    return component;
}

std::vector<double> initial_weights(const std::vector<double>& amplitudes,
                                    bool has_background,
                                    double background_fraction) {
    const std::size_t n_components = amplitudes.size() +
                                     (has_background ? 1u : 0u);
    std::vector<double> weights(n_components, kWeightFloor);
    double amplitude_sum = 0.0;
    for (double amplitude : amplitudes) {
        if (!std::isfinite(amplitude) || amplitude < 0.0)
            throw std::invalid_argument(
                    "initial_amplitudes must be finite and nonnegative");
        amplitude_sum += amplitude;
    }
    if (!std::isfinite(background_fraction))
        throw std::invalid_argument(
                "initial_background_fraction must be finite");
    const double bg = has_background
                          ? std::max(0.0, std::min(background_fraction, 1.0))
                          : 0.0;
    const double lifetime_mass = 1.0 - bg;
    if (amplitude_sum > 0.0) {
        for (std::size_t k = 0; k < amplitudes.size(); ++k)
            weights[k] = lifetime_mass * amplitudes[k] / amplitude_sum;
    } else {
        const double equal = lifetime_mass /
                             static_cast<double>(amplitudes.size());
        std::fill(weights.begin(), weights.begin() + amplitudes.size(), equal);
    }
    if (has_background) weights.back() = bg;

    double sum = 0.0;
    for (double& weight : weights) {
        weight = std::max(weight, kWeightFloor);
        sum += weight;
    }
    for (double& weight : weights) weight /= sum;
    return weights;
}

ProfileResult profile_amplitudes(
        const std::vector<double>& counts,
        const std::vector<std::vector<double>>& components,
        const std::vector<double>& seed_weights,
        const DecayFitNExpOptions& options) {
    ProfileResult result;
    result.weights = seed_weights;
    result.probability.assign(counts.size(), 0.0);
    const double photons = std::accumulate(counts.begin(), counts.end(), 0.0);
    if (photons <= 0.0) return result;

    std::vector<double> next(result.weights.size(), 0.0);
    for (int iteration = 0; iteration < options.max_em_iterations; ++iteration) {
        std::fill(result.probability.begin(), result.probability.end(), 0.0);
        for (std::size_t k = 0; k < components.size(); ++k)
            for (std::size_t i = 0; i < counts.size(); ++i)
                result.probability[i] += result.weights[k] * components[k][i];

        std::fill(next.begin(), next.end(), 0.0);
        for (std::size_t k = 0; k < components.size(); ++k) {
            double responsibility = 0.0;
            for (std::size_t i = 0; i < counts.size(); ++i) {
                if (counts[i] <= 0.0) continue;
                const double probability = std::max(result.probability[i],
                                                    kProbabilityFloor);
                responsibility += counts[i] * components[k][i] / probability;
            }
            next[k] = result.weights[k] * responsibility / photons;
        }

        double next_sum = 0.0;
        for (double& weight : next) {
            weight = std::max(weight, kWeightFloor);
            next_sum += weight;
        }
        for (double& weight : next) weight /= next_sum;

        double largest_change = 0.0;
        for (std::size_t k = 0; k < next.size(); ++k)
            largest_change = std::max(largest_change,
                                      std::fabs(next[k] - result.weights[k]));
        result.weights.swap(next);
        result.em_iterations = iteration + 1;
        if (largest_change <= options.em_tolerance) {
            result.em_converged = true;
            break;
        }
        result.em_converged = false;
    }

    std::fill(result.probability.begin(), result.probability.end(), 0.0);
    for (std::size_t k = 0; k < components.size(); ++k)
        for (std::size_t i = 0; i < counts.size(); ++i)
            result.probability[i] += result.weights[k] * components[k][i];

    result.nll = 0.0;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] > 0.0)
            result.nll -= counts[i] *
                          std::log(std::max(result.probability[i],
                                            kProbabilityFloor));
    }
    return result;
}

ProfileResult evaluate_profile(const std::vector<double>& counts,
                               const std::vector<double>& irf,
                               const std::vector<double>& background,
                               const std::vector<double>& lifetimes,
                               const std::vector<double>& seed_weights,
                               const DecayFitNExpOptions& options) {
    std::vector<std::vector<double>> components;
    components.reserve(lifetimes.size() + (background.empty() ? 0u : 1u));
    for (double lifetime : lifetimes)
        components.push_back(convolved_component(lifetime, irf, options));
    if (!background.empty()) components.push_back(background);
    return profile_amplitudes(counts, components, seed_weights, options);
}

template <typename Function>
double brent_minimize(double lower, double upper, Function&& function,
                      double tolerance, int max_iterations) {
    const double golden = 0.5 * (3.0 - std::sqrt(5.0));
    const double epsilon = std::sqrt(std::numeric_limits<double>::epsilon());
    double x = lower + golden * (upper - lower);
    double w = x;
    double v = x;
    double fx = function(x);
    double fw = fx;
    double fv = fx;
    double d = 0.0;
    double e = 0.0;

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        const double midpoint = 0.5 * (lower + upper);
        const double tol1 = epsilon * std::fabs(x) + tolerance;
        const double tol2 = 2.0 * tol1;
        if (std::fabs(x - midpoint) <= tol2 - 0.5 * (upper - lower)) break;

        bool use_golden = true;
        if (std::fabs(e) > tol1) {
            double r = (x - w) * (fx - fv);
            double q = (x - v) * (fx - fw);
            double p = (x - v) * q - (x - w) * r;
            q = 2.0 * (q - r);
            if (q > 0.0) p = -p;
            q = std::fabs(q);
            const double previous_e = e;
            e = d;
            if (std::fabs(p) < std::fabs(0.5 * q * previous_e) &&
                p > q * (lower - x) && p < q * (upper - x)) {
                d = p / q;
                const double candidate = x + d;
                if (candidate - lower < tol2 || upper - candidate < tol2)
                    d = midpoint > x ? tol1 : -tol1;
                use_golden = false;
            }
        }
        if (use_golden) {
            e = x < midpoint ? upper - x : lower - x;
            d = golden * e;
        }

        const double u = x + (std::fabs(d) >= tol1
                                  ? d
                                  : (d > 0.0 ? tol1 : -tol1));
        const double fu = function(u);
        if (fu <= fx) {
            if (u < x) upper = x; else lower = x;
            v = w; fv = fw;
            w = x; fw = fx;
            x = u; fx = fu;
        } else {
            if (u < x) lower = u; else upper = u;
            if (fu <= fw || w == x) {
                v = w; fv = fw;
                w = u; fw = fu;
            } else if (fu <= fv || v == x || v == w) {
                v = u; fv = fu;
            }
        }
    }
    return x;
}


struct CoordinateMinimum {
    double lifetime;
    double nll;
};


template <typename Function>
CoordinateMinimum multistart_coordinate_minimize(
        double lower, double upper, double current_lifetime,
        double current_nll, Function&& function, double tolerance,
        int max_iterations, int grid_intervals) {
    if (grid_intervals < 1) grid_intervals = 1;
    std::vector<double> lifetimes;
    lifetimes.reserve(grid_intervals + 2);
    const double log_lower = std::log(lower);
    const double log_span = std::log(upper) - log_lower;
    for (int i = 0; i <= grid_intervals; ++i) {
        const double fraction = static_cast<double>(i) /
                                static_cast<double>(grid_intervals);
        lifetimes.push_back(std::exp(log_lower + fraction * log_span));
    }
    lifetimes.push_back(current_lifetime);
    std::sort(lifetimes.begin(), lifetimes.end());
    lifetimes.erase(std::unique(lifetimes.begin(), lifetimes.end()),
                    lifetimes.end());

    std::vector<double> values;
    values.reserve(lifetimes.size());
    CoordinateMinimum best{current_lifetime, current_nll};
    for (double lifetime : lifetimes) {
        const double value = lifetime == current_lifetime
                                 ? current_nll
                                 : function(lifetime);
        values.push_back(value);
        if (value < best.nll) best = {lifetime, value};
    }

    // A profiled mixture likelihood need not be unimodal in one lifetime.
    // Refine every basin visible on the logarithmic scan instead of applying
    // one Brent search to the whole bound interval.
    for (std::size_t i = 1; i + 1 < lifetimes.size(); ++i) {
        const bool local_minimum =
                values[i] <= values[i - 1] && values[i] <= values[i + 1] &&
                (values[i] < values[i - 1] || values[i] < values[i + 1]);
        if (!local_minimum) continue;
        const double candidate = brent_minimize(
                lifetimes[i - 1], lifetimes[i + 1], function, tolerance,
                max_iterations);
        const double value = function(candidate);
        if (value < best.nll) best = {candidate, value};
    }
    return best;
}

void validate_options(const DecayFitNExpOptions& options) {
    if (!(options.dt > 0.0) || !std::isfinite(options.dt))
        throw std::invalid_argument("dt must be positive and finite");
    if (!(options.tau_min > 0.0) ||
        !(options.tau_max > options.tau_min) ||
        !std::isfinite(options.tau_min) || !std::isfinite(options.tau_max))
        throw std::invalid_argument("tau bounds must be finite and satisfy 0 < min < max");
    if (!std::isfinite(options.period) || options.period < 0.0)
        throw std::invalid_argument("period must be finite and nonnegative");
    if (!(options.lifetime_tolerance > 0.0) ||
        !(options.likelihood_tolerance >= 0.0) ||
        !(options.em_tolerance >= 0.0) ||
        !std::isfinite(options.lifetime_tolerance) ||
        !std::isfinite(options.likelihood_tolerance) ||
        !std::isfinite(options.em_tolerance) ||
        options.max_outer_iterations < 1 || options.max_em_iterations < 1)
        throw std::invalid_argument("iteration limits and tolerances are invalid");
}

} // namespace


DecayFitNExpResult DecayFitNExp::fit_buffers(
        double* data, int n_data,
        double* irf, int n_irf,
        double* background, int n_background,
        double* initial_lifetimes, int n_lifetimes,
        double* initial_amplitudes, int n_amplitudes,
        int* lifetime_fixed, int n_fixed,
        const DecayFitNExpOptions& options) {
    return fit(
            std::vector<double>(data, data + n_data),
            std::vector<double>(irf, irf + n_irf),
            std::vector<double>(background, background + n_background),
            std::vector<double>(initial_lifetimes, initial_lifetimes + n_lifetimes),
            std::vector<double>(initial_amplitudes, initial_amplitudes + n_amplitudes),
            std::vector<int>(lifetime_fixed, lifetime_fixed + n_fixed),
            options);
}

DecayFitNExpResult DecayFitNExp::fit_fixed_lifetimes_buffers(
        double* fdata, int n_fdata,
        double* firf, int n_firf,
        double* fbackground, int n_fbackground,
        double* flifetimes, int n_flifetimes,
        double* famplitudes, int n_famplitudes,
        const DecayFitNExpOptions& options) {
    return fit_fixed_lifetimes(
            std::vector<double>(fdata, fdata + n_fdata),
            std::vector<double>(firf, firf + n_firf),
            std::vector<double>(fbackground, fbackground + n_fbackground),
            std::vector<double>(flifetimes, flifetimes + n_flifetimes),
            std::vector<double>(famplitudes, famplitudes + n_famplitudes),
            options);
}

DecayFitNExpResult DecayFitNExp::fit(
        const std::vector<double>& data,
        const std::vector<double>& irf,
        const std::vector<double>& background,
        const std::vector<double>& initial_lifetimes,
        const std::vector<double>& initial_amplitudes,
        const std::vector<int>& lifetime_fixed,
        const DecayFitNExpOptions& options) {
    validate_options(options);
    if (irf.empty()) throw std::invalid_argument("irf must not be empty");
    require_nonnegative_finite(irf, "irf");
    if (initial_lifetimes.empty())
        throw std::invalid_argument("at least one lifetime is required");
    if (initial_amplitudes.size() != initial_lifetimes.size() ||
        lifetime_fixed.size() != initial_lifetimes.size())
        throw std::invalid_argument(
                "lifetimes, amplitudes, and fixed mask must have equal length");

    std::vector<double> background_probability = background;
    if (!background_probability.empty()) {
        if (background_probability.size() != irf.size())
            throw std::invalid_argument("background and irf lengths must match");
        require_nonnegative_finite(background_probability, "background");
        const double area = std::accumulate(background_probability.begin(),
                                            background_probability.end(), 0.0);
        if (area > 0.0) normalize_probability(background_probability, "background");
        else background_probability.clear();
    }

    std::vector<double> lifetimes = initial_lifetimes;
    for (std::size_t k = 0; k < lifetimes.size(); ++k) {
        double& lifetime = lifetimes[k];
        if (!std::isfinite(lifetime))
            throw std::invalid_argument("lifetimes must be finite");
        if (lifetime_fixed[k] != 0) {
            if (lifetime < options.tau_min || lifetime > options.tau_max)
                throw std::invalid_argument(
                        "fixed lifetimes must lie within tau bounds");
        } else {
            lifetime = std::max(options.tau_min,
                                std::min(lifetime, options.tau_max));
        }
    }
    const std::vector<double> counts = pooled_counts(data, irf.size());
    const double photons = std::accumulate(counts.begin(), counts.end(), 0.0);
    if (!(photons > 0.0) || !std::isfinite(photons))
        throw std::invalid_argument(
                "data must have a positive finite photon count");
    const std::vector<double> seed_weights = initial_weights(
            initial_amplitudes, !background_probability.empty(),
            options.initial_background_fraction);

    ProfileResult profile = evaluate_profile(counts, irf, background_probability,
                                             lifetimes, seed_weights, options);
    bool outer_converged = true;
    int outer_iterations = 0;
    const bool any_free = std::any_of(lifetime_fixed.begin(), lifetime_fixed.end(),
                                      [](int fixed) { return fixed == 0; });
    if (any_free && photons > 0.0) {
        outer_converged = false;
        for (int outer = 0; outer < options.max_outer_iterations; ++outer) {
            bool sweep_improved = false;
            for (std::size_t k = 0; k < lifetimes.size(); ++k) {
                if (lifetime_fixed[k] != 0) continue;
                std::vector<double> trial = lifetimes;
                auto objective = [&](double tau) {
                    trial[k] = tau;
                    return evaluate_profile(counts, irf, background_probability,
                                            trial, seed_weights, options).nll;
                };
                const CoordinateMinimum candidate =
                        multistart_coordinate_minimize(
                                options.tau_min, options.tau_max, lifetimes[k],
                                profile.nll, objective,
                                options.lifetime_tolerance, 100,
                                options.coordinate_grid_intervals);
                const double threshold = options.likelihood_tolerance *
                                         (1.0 + std::fabs(profile.nll));
                if (profile.nll - candidate.nll <= threshold) continue;
                trial[k] = candidate.lifetime;
                ProfileResult candidate_profile = evaluate_profile(
                        counts, irf, background_probability, trial,
                        seed_weights, options);
                if (profile.nll - candidate_profile.nll > threshold) {
                    sweep_improved = true;
                    lifetimes.swap(trial);
                    profile = std::move(candidate_profile);
                }
            }
            outer_iterations = outer + 1;
            if (!sweep_improved) {
                outer_converged = true;
                break;
            }
        }
    }

    // Re-evaluate once so returned amplitudes/model correspond exactly to the
    // returned lifetimes even if the final coordinate proposal was rejected.
    profile = evaluate_profile(counts, irf, background_probability,
                               lifetimes, seed_weights, options);

    DecayFitNExpResult result;
    result.converged = outer_converged && profile.em_converged;
    result.outer_iterations = outer_iterations;
    result.em_iterations = profile.em_iterations;
    result.negative_log_likelihood = profile.nll;
    result.photon_count = photons;
    result.lifetimes = lifetimes;
    result.amplitudes.assign(profile.weights.begin(),
                             profile.weights.begin() + lifetimes.size());
    if (!background_probability.empty())
        result.background_amplitude = profile.weights.back();

    if (options.include_model) {
        result.model.resize(data.size(), 0.0);
        if (data.size() == irf.size()) {
            for (std::size_t i = 0; i < irf.size(); ++i)
                result.model[i] = photons * profile.probability[i];
        } else {
            const double vv_total = std::accumulate(
                    data.begin(), data.begin() + irf.size(), 0.0);
            const double vh_total = photons - vv_total;
            for (std::size_t i = 0; i < irf.size(); ++i) {
                result.model[i] = vv_total * profile.probability[i];
                result.model[i + irf.size()] =
                        vh_total * profile.probability[i];
            }
        }
    }
    return result;
}


DecayFitNExpResult DecayFitNExp::fit_fixed_lifetimes(
        const std::vector<double>& data,
        const std::vector<double>& irf,
        const std::vector<double>& background,
        const std::vector<double>& lifetimes,
        const std::vector<double>& initial_amplitudes,
        const DecayFitNExpOptions& options) {
    return fit(data, irf, background, lifetimes, initial_amplitudes,
               std::vector<int>(lifetimes.size(), 1), options);
}


std::vector<double> DecayFitNExp::fit_batch_flat(
        const std::vector<double>& data_matrix,
        std::size_t n_rows,
        std::size_t n_cols,
        const std::vector<double>& irf,
        const std::vector<double>& background,
        const std::vector<double>& initial_lifetimes,
        const std::vector<double>& initial_amplitudes,
        const std::vector<int>& lifetime_fixed,
        const DecayFitNExpOptions& options) {
    if (n_rows > 0 && n_cols > std::numeric_limits<std::size_t>::max() / n_rows)
        throw std::invalid_argument("batch shape overflows size_t");
    if (data_matrix.size() != n_rows * n_cols)
        throw std::invalid_argument("data_matrix size does not match n_rows*n_cols");
    if (n_cols != irf.size() && n_cols != 2 * irf.size())
        throw std::invalid_argument("n_cols must equal irf length or twice irf length");

    const std::size_t n_exp = initial_lifetimes.size();
    const std::size_t output_width = 4 + 2 * n_exp;
    std::vector<double> output(n_rows * output_width, 0.0);
    DecayFitNExpOptions batch_options = options;
    batch_options.include_model = false;

    const unsigned int n_threads = batch_thread_count(n_rows);
    std::vector<std::exception_ptr> exceptions(n_threads);
    auto fit_range = [&](unsigned int worker, std::size_t begin,
                         std::size_t end) {
        try {
            std::vector<double> row(n_cols, 0.0);
            for (std::size_t r = begin; r < end; ++r) {
                std::copy(data_matrix.begin() + r * n_cols,
                          data_matrix.begin() + (r + 1) * n_cols,
                          row.begin());
                const DecayFitNExpResult result = fit(
                        row, irf, background, initial_lifetimes,
                        initial_amplitudes, lifetime_fixed, batch_options);
                double* destination = output.data() + r * output_width;
                destination[0] = result.negative_log_likelihood;
                destination[1] = result.background_amplitude;
                destination[2] = result.converged ? 1.0 : 0.0;
                destination[3] = static_cast<double>(result.outer_iterations);
                std::copy(result.lifetimes.begin(), result.lifetimes.end(),
                          destination + 4);
                std::copy(result.amplitudes.begin(), result.amplitudes.end(),
                          destination + 4 + n_exp);
            }
        } catch (...) {
            exceptions[worker] = std::current_exception();
        }
    };

    if (n_threads == 1) {
        fit_range(0, 0, n_rows);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(n_threads);
        const std::size_t block = (n_rows + n_threads - 1) / n_threads;
        for (unsigned int worker = 0; worker < n_threads; ++worker) {
            const std::size_t begin = static_cast<std::size_t>(worker) * block;
            const std::size_t end = std::min(n_rows, begin + block);
            workers.emplace_back(fit_range, worker, begin, end);
        }
        for (std::thread& worker : workers) worker.join();
    }
    for (const std::exception_ptr& error : exceptions)
        if (error) std::rethrow_exception(error);
    return output;
}
