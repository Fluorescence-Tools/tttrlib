// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file DecayFitProblem.h
 * \brief Everything a decay fit needs that is not an optimised parameter.
 *
 * One container for every decay model, replacing the per-model arrangements that
 * grew up around them: `DecayFitData` (two-channel Jordi integer counts plus a
 * positional `corrections` vector) served Fit23/24/25/26, while `DecayFitNExp`
 * took loose `std::vector<double>` arguments alongside a typed options struct,
 * and neither could describe a model carrying extra reference patterns. Three
 * arrangements meant three call shapes, and a caller could not build a fit
 * without first knowing *which* model it was building.
 *
 * The split this file draws is between **what is being fitted** (here) and
 * **what is being optimised** (a flat parameter vector, described by the
 * registry). Nothing in this container is optimised; nothing in the parameter
 * vector describes the measurement.
 *
 * Three things are deliberately different from what came before:
 *
 *  - **`data` is `double`, not `int`.** Counts are integers, but pooled,
 *    background-subtracted or rebinned data is not, and `DecayFitNExp` already
 *    worked in `double`. The Poisson objectives take the count interpretation
 *    where they need it rather than forcing it on storage.
 *  - **`n_channels` is stored, never inferred.** `DecayFitData::n_channels()`
 *    returned `data.size() / 2`, silently correct only for two-channel Jordi
 *    data and silently *wrong* for a single-channel decay — which
 *    `DecayFitNExp` supports. An explicit count cannot be quietly misread.
 *  - **`setup` is a flat vector laid out by the registry**, the same mechanism
 *    as the parameter vector, rather than a positional `corrections` array whose
 *    meaning lived in a comment. Adding a model-specific setup value is a
 *    registry edit plus a slot, not a new struct only one model understands.
 *
 * Bulk arrays stay here rather than travelling in results: `model` is
 * `n_channels * n_bins` doubles, so putting it in a result vector would make a
 * batch result matrix thousands of columns wide instead of a handful.
 */
#ifndef TTTRLIB_DECAYFITPROBLEM_H
#define TTTRLIB_DECAYFITPROBLEM_H

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

using json = nlohmann::json;


/*!
 * \brief The measurement a decay model is fitted to.
 *
 * Layout is channel-major: channel `c`'s bin `i` lives at `c * n_bins + i`, for
 * `data`, `model` and (when they carry one entry per channel) `irf` and
 * `background`. A one-channel problem is simply `n_channels == 1`; the
 * historical two-channel "Jordi" arrangement (parallel followed by
 * perpendicular) is `n_channels == 2` and needs no special case.
 */
class DecayFitProblem {

public:

    /*! Experimental histogram, `n_channels * n_bins`, channel-major. */
    std::vector<double> data;

    /*!
     * Instrument response, either `n_bins` (shared by every channel) or
     * `n_channels * n_bins` (one per channel). Both are accepted because a
     * polarisation-resolved setup may or may not have measured the two
     * detectors' responses separately; `irf_is_per_channel()` reports which.
     */
    std::vector<double> irf;

    /*!
     * Background/scatter pattern, sized like `irf`. Not normalised here — a
     * model normalises it the way its own objective requires.
     */
    std::vector<double> background;

    /*!
     * Additional fixed reference patterns, each sized like `irf`.
     *
     * A model that mixes in a measured shape rather than a parameterised one
     * reads it from here: an autofluorescence decay, a donor-only reference, a
     * scatter pattern measured separately. The *amplitude* of each pattern is an
     * optimised parameter; the pattern itself is data and belongs in this
     * container. Which index means what is the model's own business and is
     * documented in its registry entry.
     */
    std::vector<std::vector<double>> patterns;

    /*!
     * Model evaluated at the parameters of the last `fit()` or `evaluate()`,
     * sized and laid out exactly like `data`.
     */
    std::vector<double> model;

    /*! Number of detection channels. Stored, never inferred from a size. */
    int n_channels = 1;

    /*! Number of micro-time bins per channel. */
    int n_bins = 0;

    /*! Width of one micro-time bin, in the same unit as the model's lifetimes. */
    double dt = 1.0;

    /*!
     * Construction/correction inputs, flat, in the order given by the model's
     * `setup` entry in the registry (`fit_setup` category).
     *
     * These are set once and are not optimised — excitation period, g-factor,
     * detector mixing terms, convolution stop, objective selection, and whatever
     * else a particular model declares. The registry is the authority on which
     * slot is which, exactly as it is for the parameter vector.
     */
    std::vector<double> setup;

    /*!
     * First bin of each channel included in the objective.
     *
     * One concept replacing three that meant almost the same thing: Fit2x's
     * `convolution_stop` buried in `corrections`, `DecayFitNExp`'s `tail_start`,
     * and the time-range selection a Fourier-domain model applies before
     * comparing to data. A tail fit is `fit_start > 0`.
     */
    int fit_start = 0;

    /*!
     * One past the last bin of each channel included in the objective; negative
     * means "to the end of the channel".
     */
    int fit_stop = -1;

    DecayFitProblem() = default;

    /*!
     * \brief Build a problem and size `model` to match `data`.
     *
     * \param n_channels Number of detection channels.
     * \param n_bins Number of micro-time bins per channel.
     * \param dt Width of one micro-time bin.
     */
    DecayFitProblem(int n_channels, int n_bins, double dt = 1.0)
        : n_channels(n_channels), n_bins(n_bins), dt(dt) {
        const std::size_t n = total_size();
        data.assign(n, 0.0);
        model.assign(n, 0.0);
    }

    /*! Total number of samples, `n_channels * n_bins`. */
    std::size_t total_size() const {
        if (n_channels <= 0 || n_bins <= 0) return 0;
        return static_cast<std::size_t>(n_channels) * static_cast<std::size_t>(n_bins);
    }

    /*! True when `irf` holds one response per channel rather than one shared. */
    bool irf_is_per_channel() const {
        return n_bins > 0 && irf.size() == total_size() && n_channels > 1;
    }

    /*!
     * \brief Start of channel `c`'s IRF, whether shared or per-channel.
     *
     * Callers index the IRF through this rather than assuming a layout, so a
     * model works unchanged with a shared response and with per-detector ones.
     */
    const double *irf_of(int c) const {
        if (irf.empty()) return nullptr;
        return irf_is_per_channel() ? irf.data() + static_cast<std::size_t>(c) * n_bins
                                    : irf.data();
    }

    /*! Start of channel `c`'s background, whether shared or per-channel. */
    const double *background_of(int c) const {
        if (background.empty()) return nullptr;
        const bool per_channel =
            n_bins > 0 && background.size() == total_size() && n_channels > 1;
        return per_channel ? background.data() + static_cast<std::size_t>(c) * n_bins
                           : background.data();
    }

    /*! First bin included in the objective, clamped into range. */
    int start_bin() const {
        return fit_start < 0 ? 0 : (fit_start > n_bins ? n_bins : fit_start);
    }

    /*! One past the last bin included in the objective, clamped into range. */
    int stop_bin() const {
        if (fit_stop < 0 || fit_stop > n_bins) return n_bins;
        return fit_stop < start_bin() ? start_bin() : fit_stop;
    }

    /*!
     * \brief Reason the problem cannot be fitted, or an empty string.
     *
     * Checked before any model touches the arrays. The failure this exists to
     * prevent is real and was hit before: growing `data` to a longer decay while
     * `irf`/`background` kept their original length made the objective read past
     * the end of them and crash. Every language binding reaches native code
     * through the same entry point, so validating once here fails safely
     * everywhere rather than segfaulting in whichever binding got there first.
     */
    std::string validation_error() const {
        if (n_channels <= 0) return "n_channels must be positive";
        if (n_bins <= 0) return "n_bins must be positive";
        if (dt <= 0.0) return "dt must be positive";
        const std::size_t n = total_size();
        if (data.size() != n) {
            std::stringstream s;
            s << "data has " << data.size() << " samples, expected "
              << n << " (n_channels * n_bins)";
            return s.str();
        }
        // A response sized for *one* channel is accepted as "shared", but only
        // when there is one channel to share it with. A two-channel model reads
        // `2 * n_bins` samples straight out of `irf.data()`, so a half-length
        // response there is an out-of-bounds read, not a shorthand — and it is
        // silent, because the memory just past a heap vector is usually mapped.
        // This was reached in practice: `model_curve` on a two-channel fit23
        // with a one-channel IRF returned plausible numbers and corrupted the
        // heap, surfacing as a crash in an unrelated test much later.
        if (!irf.empty() && irf.size() != n &&
            !(n_channels == 1 && irf.size() == static_cast<std::size_t>(n_bins))) {
            std::stringstream s;
            s << "irf has " << irf.size() << " samples, expected " << n
              << " (n_channels * n_bins)";
            return s.str();
        }
        if (!background.empty() && background.size() != n &&
            !(n_channels == 1 && background.size() == static_cast<std::size_t>(n_bins))) {
            std::stringstream s;
            s << "background has " << background.size() << " samples, expected "
              << n << " (n_channels * n_bins)";
            return s.str();
        }
        for (std::size_t k = 0; k < patterns.size(); ++k) {
            if (patterns[k].size() != static_cast<std::size_t>(n_bins) &&
                patterns[k].size() != n) {
                std::stringstream s;
                s << "pattern " << k << " must hold n_bins or n_channels * n_bins samples";
                return s.str();
            }
        }
        return std::string();
    }

    /*! True when the arrays are sized consistently enough to fit. */
    bool is_valid() const { return validation_error().empty(); }

    /*! Throw `std::invalid_argument` describing the first inconsistency. */
    void require_valid() const {
        const std::string e = validation_error();
        if (!e.empty()) throw std::invalid_argument("DecayFitProblem: " + e);
    }

    /*! Size `model` to `data` and zero it. */
    void reset_model() { model.assign(total_size(), 0.0); }

    /*!
     * \brief Replace the data with a new decay of the same shape.
     *
     * Rejects a length change rather than resizing, because a decay of a
     * different length needs an IRF of that length too — silently accepting one
     * is how the out-of-bounds read above used to happen.
     */
    void set_data(const std::vector<double> &values) {
        if (values.size() != total_size()) {
            throw std::invalid_argument(
                "DecayFitProblem::set_data: length differs from n_channels * n_bins; "
                "build a new problem for a decay of a different length");
        }
        data = values;
    }

    /*! Describe the problem (not its arrays) as JSON, for provenance records. */
    json to_json() const;

    /*! Restore the scalar fields written by `to_json`. */
    void from_json(const json &j);

    std::string str() const {
        std::stringstream s;
        s << "DecayFitProblem: " << n_channels << " x " << n_bins
          << " bins, dt = " << dt << ", fit [" << start_bin() << ", " << stop_bin() << ")"
          << ", " << patterns.size() << " pattern(s)";
        return s.str();
    }
};

#endif // TTTRLIB_DECAYFITPROBLEM_H
