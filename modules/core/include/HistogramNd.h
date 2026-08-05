// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_HISTOGRAMND_H
#define TTTRLIB_HISTOGRAMND_H

/*!
 * \file HistogramNd.h
 * \brief A general N-dimensional histogram: axis kinds, flow bins, variances.
 *
 * histogram1D and histogram2D in Histogram.h are the fast, narrow path: two
 * arrays in, one array of counts out. They stay, because that is what the
 * photon code calls a million times.
 *
 * This is the other thing a histogram library has to be -- the one that can say
 * "detector channel, which is categorical" or "phase, which wraps at 2pi", keep
 * the points that fell off the ends instead of dropping them, carry the variance
 * of a weighted fill, and then project a 3-D histogram down to the two axes you
 * want to plot. Written against boost-histogram's feature set, because that is
 * the library it is being compared with and the one users are coming from.
 *
 * \section nd_flow Flow bins are the behavioural difference
 *
 * histogram1D/2D silently drop a point outside the axis. That is defensible for
 * photon arrival times and indefensible for a measurement: "3% of my data is
 * off-scale" is a fact about the data, and a histogram that does not record it
 * cannot tell you. Axes carry underflow and overflow bins by default here, as
 * they do in boost, and \ref Axis::index reports -1 and `size()` for the two
 * cases rather than "no".
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "Histogram.h"

namespace tttrlib {
namespace hist {

/// What kind of thing an axis bins.
enum class AxisKind {
    Regular,    ///< n equal-width bins between lo and hi
    Log,        ///< n bins equal in log(x); lo and hi must be positive
    Sqrt,       ///< n bins equal in sqrt(x)
    Pow,        ///< n bins equal in x^p
    Variable,   ///< arbitrary, not necessarily uniform, edges
    Integer,    ///< one bin per integer in [lo, hi)
    Category,   ///< one bin per listed value; everything else is "other"
    Boolean     ///< two bins, false and true, no flow
};

/*!
 * \brief Per-axis options.
 *
 * `underflow`/`overflow` add the bins that catch what falls off each end.
 * `circular` wraps instead, which is what an angle or a phase wants -- and is
 * why it is mutually exclusive with flow bins: nothing can fall off a circle.
 * `growth` extends the axis to fit whatever arrives.
 */
struct AxisOptions {
    bool underflow = true;
    bool overflow = true;
    bool circular = false;
    bool growth = false;

    // Written out rather than brace-initialised: SWIG's C++ parser does not
    // accept AxisOptions{...} in a default argument, and having two spellings
    // of the same thing in one header invites the next person to use the wrong
    // one.
    static AxisOptions none() {
        AxisOptions o; o.underflow = false; o.overflow = false; return o;
    }
    static AxisOptions flow() { return AxisOptions(); }
    static AxisOptions circular_() {
        AxisOptions o; o.underflow = false; o.overflow = false; o.circular = true; return o;
    }
    static AxisOptions growing() {
        AxisOptions o; o.underflow = false; o.overflow = false; o.growth = true; return o;
    }
};

/*!
 * \brief What a bin accumulates.
 *
 * Counts and Weight answer "how much landed here". Mean and WeightedMean
 * answer a different question -- "what was the average VALUE of something else
 * for the points that landed here" -- which is a profile, not a histogram, and
 * is the reason a fill can carry a sample as well as a weight.
 */
enum class HistStorage {
    Counts,        ///< sum of 1
    Weight,        ///< sum of w, and of w^2 so the result has an error bar
    Mean,          ///< count, mean of the sample, and its variance
    WeightedMean   ///< as Mean, weighted
};

/// Returned by Axis::index for a value below the axis.
constexpr int AXIS_UNDERFLOW = -1;

/*!
 * \brief One axis of a histogram.
 *
 * Value semantics on purpose: an axis is a description, small enough to copy,
 * and making it a class hierarchy would put a virtual call in the inner loop of
 * every fill for the sake of a polymorphism nobody asked for.
 */
class Axis {
public:
    // --- construction -----------------------------------------------------

    static Axis regular(int n_bins, double lo, double hi,
                        AxisOptions opt = AxisOptions(),
                        std::string label = "") {
        Axis a; a.kind_ = AxisKind::Regular; a.n_ = n_bins; a.lo_ = lo; a.hi_ = hi;
        a.opt_ = opt; a.label_ = std::move(label); a.finish(); return a;
    }

    static Axis log(int n_bins, double lo, double hi,
                    AxisOptions opt = AxisOptions(), std::string label = "") {
        if (!(lo > 0.0) || !(hi > 0.0))
            throw std::invalid_argument("log axis needs positive bounds");
        Axis a; a.kind_ = AxisKind::Log; a.n_ = n_bins; a.lo_ = lo; a.hi_ = hi;
        a.opt_ = opt; a.label_ = std::move(label); a.finish(); return a;
    }

    static Axis sqrt_axis(int n_bins, double lo, double hi,
                          AxisOptions opt = AxisOptions(), std::string label = "") {
        if (lo < 0.0) throw std::invalid_argument("sqrt axis needs non-negative bounds");
        Axis a; a.kind_ = AxisKind::Sqrt; a.n_ = n_bins; a.lo_ = lo; a.hi_ = hi;
        a.opt_ = opt; a.label_ = std::move(label); a.finish(); return a;
    }

    static Axis pow_axis(int n_bins, double lo, double hi, double power,
                         AxisOptions opt = AxisOptions(), std::string label = "") {
        Axis a; a.kind_ = AxisKind::Pow; a.n_ = n_bins; a.lo_ = lo; a.hi_ = hi;
        a.power_ = power; a.opt_ = opt; a.label_ = std::move(label); a.finish(); return a;
    }

    /// \param edges n_edges values in ascending order; the axis has n_edges-1 bins.
    static Axis variable(const double* edges, int n_edges,
                         AxisOptions opt = AxisOptions(), std::string label = "") {
        if (edges == nullptr || n_edges < 2)
            throw std::invalid_argument("variable axis needs at least two edges");
        Axis a; a.kind_ = AxisKind::Variable;
        a.edges_.assign(edges, edges + n_edges);
        a.n_ = n_edges - 1; a.lo_ = a.edges_.front(); a.hi_ = a.edges_.back();
        a.opt_ = opt; a.label_ = std::move(label); a.finish(); return a;
    }

    /// One bin per integer in [lo, hi).
    static Axis integer(int lo, int hi, AxisOptions opt = AxisOptions(),
                        std::string label = "") {
        Axis a; a.kind_ = AxisKind::Integer; a.n_ = hi - lo;
        a.lo_ = lo; a.hi_ = hi; a.opt_ = opt; a.label_ = std::move(label);
        a.finish(); return a;
    }

    /*!
     * One bin per listed value, in the order given.
     *
     * The overflow bin means "not one of these" rather than "above the last
     * one" -- categories are not ordered, so there is no above.
     */
    static Axis category(const int* values, int n_values,
                         AxisOptions opt = AxisOptions(), std::string label = "") {
        if (values == nullptr || n_values < 1)
            throw std::invalid_argument("category axis needs at least one value");
        Axis a; a.kind_ = AxisKind::Category;
        a.categories_.assign(values, values + n_values);
        a.n_ = n_values;
        a.opt_ = opt;
        a.opt_.underflow = false;   // nothing is below a category
        a.opt_.circular = false;
        a.label_ = std::move(label); a.finish(); return a;
    }

    static Axis boolean(std::string label = "") {
        Axis a; a.kind_ = AxisKind::Boolean; a.n_ = 2;
        a.opt_ = AxisOptions::none(); a.label_ = std::move(label);
        a.finish(); return a;
    }

    // --- description ------------------------------------------------------

    AxisKind kind() const { return kind_; }
    /// Number of value bins, not counting flow.
    int size() const { return n_; }
    /// Number of stored bins, including flow.
    int extent() const { return n_ + (opt_.underflow ? 1 : 0) + (opt_.overflow ? 1 : 0); }
    const AxisOptions& options() const { return opt_; }
    const std::string& label() const { return label_; }
    void set_label(std::string s) { label_ = std::move(s); }
    double lo() const { return lo_; }
    double hi() const { return hi_; }

    /// Lower edge of bin i. For Category and Boolean this is the bin number.
    double bin_lower(int i) const {
        switch (kind_) {
            case AxisKind::Variable: return edges_[i];
            case AxisKind::Integer:  return lo_ + i;
            case AxisKind::Category: return i < (int) categories_.size() ? categories_[i] : 0;
            case AxisKind::Boolean:  return i;
            default: return inverse(t_lo_ + i * t_width_);
        }
    }
    double bin_upper(int i) const {
        switch (kind_) {
            case AxisKind::Variable: return edges_[i + 1];
            case AxisKind::Integer:  return lo_ + i + 1;
            case AxisKind::Category: return bin_lower(i);
            case AxisKind::Boolean:  return i;
            default: return inverse(t_lo_ + (i + 1) * t_width_);
        }
    }
    double bin_center(int i) const {
        if (kind_ == AxisKind::Category || kind_ == AxisKind::Boolean) return bin_lower(i);
        return 0.5 * (bin_lower(i) + bin_upper(i));
    }

    /*!
     * \brief The bin for `x`.
     *
     * \return `0 .. size()-1` for a value on the axis, \ref AXIS_UNDERFLOW for
     *         one below it, and `size()` for one above. The caller decides what
     *         to do with the two flow answers; the axis does not silently
     *         discard them.
     */
    int index(double x) const {
        switch (kind_) {
            case AxisKind::Boolean:
                return x != 0.0 ? 1 : 0;

            case AxisKind::Category: {
                const int v = static_cast<int>(x);
                for (std::size_t i = 0; i < categories_.size(); i++)
                    if (categories_[i] == v) return static_cast<int>(i);
                return n_;      // "not one of these"
            }

            case AxisKind::Integer: {
                const double f = std::floor(x);
                if (f < lo_) return wrap_or(AXIS_UNDERFLOW);
                if (f >= hi_) return wrap_or(n_);
                return static_cast<int>(f - lo_);
            }

            case AxisKind::Variable: {
                if (!(x >= edges_.front())) return wrap_or(AXIS_UNDERFLOW);
                if (x >= edges_.back()) return wrap_or(n_);
                // upper_bound gives the first edge strictly greater than x, so
                // the bin is one before it. Bins are [lower, upper).
                const auto it = std::upper_bound(edges_.begin(), edges_.end(), x);
                return static_cast<int>(it - edges_.begin()) - 1;
            }

            default: {   // Regular, Log, Sqrt, Pow -- all affine after transform
                // NaN is not below the axis, it is not on it either. boost puts
                // it in the overflow bin and so does this, so a fill of the same
                // data agrees bin for bin. -inf underflows, +inf overflows.
                if (x != x) return n_;
                if (!std::isfinite(x)) return x > 0 ? n_ : AXIS_UNDERFLOW;
                const double t = transform(x);
                if (!std::isfinite(t)) return AXIS_UNDERFLOW;
                const double f = std::floor((t - t_lo_) * t_inv_width_);
                if (f < 0.0) return wrap_or(AXIS_UNDERFLOW, f);
                if (f >= n_) return wrap_or(n_, f);
                return static_cast<int>(f);
            }
        }
    }

    /// Storage slot for the result of index(), or -1 when there is no bin for it.
    int slot(int idx) const {
        const int shift = opt_.underflow ? 1 : 0;
        if (idx == AXIS_UNDERFLOW) return opt_.underflow ? 0 : -1;
        if (idx >= n_) return opt_.overflow ? n_ + shift : -1;
        return idx + shift;
    }

    /*!
     * \brief The n+1 edges of the axis, into a caller-owned array.
     *
     * A front end that asked for "64 bins from 0 to 100" still has to label the
     * axis it got, and reconstructing the edges by hand is where a half-bin
     * shift gets in. For Category and Boolean the "edges" are the bin numbers,
     * because those axes have no width.
     */
    void get_edges(double** out, int* n) const {
        *n = n_ + 1;
        *out = static_cast<double*>(std::malloc((n_ + 1) * sizeof(double)));
        if (*out == nullptr) return;
        for (int i = 0; i < n_; i++) (*out)[i] = bin_lower(i);
        (*out)[n_] = bin_upper(n_ - 1);
    }
    void get_centers(double** out, int* n) const {
        *n = n_;
        *out = static_cast<double*>(std::malloc(std::max(1, n_) * sizeof(double)));
        if (*out == nullptr) return;
        for (int i = 0; i < n_; i++) (*out)[i] = bin_center(i);
    }
    void get_widths(double** out, int* n) const {
        *n = n_;
        *out = static_cast<double*>(std::malloc(std::max(1, n_) * sizeof(double)));
        if (*out == nullptr) return;
        for (int i = 0; i < n_; i++) (*out)[i] = bin_upper(i) - bin_lower(i);
    }

    /// Can this axis grow to include `x`? See AxisOptions::growth.
    bool needs_growth(double x) const {
        if (!opt_.growth) return false;
        const int i = index(x);
        return i == AXIS_UNDERFLOW || i >= n_;
    }

private:
    void finish() {
        if (n_ < 1) throw std::invalid_argument("an axis needs at least one bin");
        if (opt_.circular) { opt_.underflow = false; opt_.overflow = false; }
        if (kind_ == AxisKind::Regular || kind_ == AxisKind::Log ||
            kind_ == AxisKind::Sqrt || kind_ == AxisKind::Pow) {
            t_lo_ = transform(lo_);
            const double t_hi = transform(hi_);
            t_width_ = (t_hi - t_lo_) / n_;
            t_inv_width_ = t_width_ != 0.0 ? 1.0 / t_width_ : 0.0;
        }
    }

    double transform(double x) const {
        switch (kind_) {
            case AxisKind::Log:  return std::log(x);
            case AxisKind::Sqrt: return std::sqrt(x);
            case AxisKind::Pow:  return std::pow(x, power_);
            default:             return x;
        }
    }
    double inverse(double t) const {
        switch (kind_) {
            case AxisKind::Log:  return std::exp(t);
            case AxisKind::Sqrt: return t * t;
            case AxisKind::Pow:  return std::pow(t, 1.0 / power_);
            default:             return t;
        }
    }

    /// A circular axis wraps rather than flowing over.
    int wrap_or(int flow_answer, double f = 0.0) const {
        if (!opt_.circular) return flow_answer;
        int i = static_cast<int>(std::floor(std::fmod(f, static_cast<double>(n_))));
        if (i < 0) i += n_;
        return i;
    }

    AxisKind kind_ = AxisKind::Regular;
    AxisOptions opt_;
    int n_ = 0;
    double lo_ = 0.0, hi_ = 0.0, power_ = 1.0;
    double t_lo_ = 0.0, t_width_ = 1.0, t_inv_width_ = 1.0;
    std::vector<double> edges_;
    std::vector<int> categories_;
    std::string label_;
};



/*!
 * \brief An N-dimensional histogram over \ref Axis axes.
 *
 * Storage is row-major with the LAST axis fastest, which is C order and so what
 * numpy reshapes to without a copy. It includes the flow bins, so the stored
 * shape is `extent()` per axis rather than `size()`; \ref values_without_flow
 * hands back the interior when that is what a caller wants to plot.
 *
 * \section nd_weights Variance
 *
 * A weighted fill accumulates the sum of weights AND the sum of squared
 * weights, because the first without the second is a number nobody can put an
 * error bar on. An unweighted fill does not pay for it -- for counts the
 * variance equals the value, so it is not stored.
 */
class HistogramNd {
public:
    HistogramNd() = default;

    explicit HistogramNd(std::vector<Axis> axes, bool track_variance = false)
            : axes_(std::move(axes)),
              storage_(track_variance ? HistStorage::Weight : HistStorage::Counts),
              track_variance_(track_variance) {
        if (axes_.empty()) throw std::invalid_argument("a histogram needs at least one axis");
        reset_storage();
    }

    HistogramNd(std::vector<Axis> axes, HistStorage storage)
            : axes_(std::move(axes)), storage_(storage),
              track_variance_(storage != HistStorage::Counts) {
        if (axes_.empty()) throw std::invalid_argument("a histogram needs at least one axis");
        reset_storage();
    }

    HistStorage storage() const { return storage_; }
    bool is_profile() const {
        return storage_ == HistStorage::Mean || storage_ == HistStorage::WeightedMean;
    }

    // --- description ------------------------------------------------------

    int rank() const { return static_cast<int>(axes_.size()); }
    const Axis& axis(int i) const { return axes_.at(i); }
    Axis& axis(int i) { return axes_.at(i); }
    /// Stored size of each axis, flow bins included.
    std::vector<int> shape() const {
        std::vector<int> s;
        s.reserve(axes_.size());
        for (const Axis& a : axes_) s.push_back(a.extent());
        return s;
    }
    std::size_t size() const { return values_.size(); }
    bool tracks_variance() const { return track_variance_; }

    const std::vector<double>& values() const { return values_; }
    std::vector<double>& values() { return values_; }
    /// Sum of squared weights, empty unless tracking variance.
    const std::vector<double>& variances() const { return variances_; }

    // --- filling ----------------------------------------------------------

    /*!
     * \brief Add `n_points` points.
     *
     * \param columns one array per axis, each `n_points` long
     * \param weights per-point weights, or nullptr for unit weights
     * \param n_threads 0 to decide, 1 to force serial, or an explicit count
     */
    void fill(const double* const* columns, long long n_points,
              const double* weights = nullptr, int n_threads = 0) {
        fill_with([&](int d, long long i) { return columns[d][i]; },
                  [](long long) { return true; }, n_points, weights, n_threads);
    }

    /*!
     * \brief Fill from one row-major (n_rows, n_cols) array, a point per row.
     *
     * What a caller coming from numpy has -- `np.column_stack([x, y])` -- and
     * what a language binding can pass without building an array of pointers.
     * `n_cols` must equal rank().
     */
    void fill_rows(const double* data, int n_rows, int n_cols, int n_threads = 0) {
        check_cols(n_cols);
        const int nc = n_cols;
        fill_with([data, nc](int d, long long i) { return data[i * nc + d]; },
                  [](long long) { return true; }, n_rows, nullptr, n_threads);
    }

    /// \see fill_rows
    void fill_rows_weighted(const double* data, int n_rows, int n_cols,
                            const double* weights, int n_weights, int n_threads = 0) {
        check_cols(n_cols);
        const int nc = n_cols;
        fill_with([data, nc](int d, long long i) { return data[i * nc + d]; },
                  [](long long) { return true; },
                  std::min<long long>(n_rows, n_weights), weights, n_threads);
    }

    /*!
     * \brief Fill a 1-D histogram from a plain array.
     *
     * Weighted and unweighted are separate entry points rather than one with a
     * defaulted weights argument, because a language binding folds a
     * (pointer, length) pair into a single array parameter and there is no
     * spelling of "no array" left to default to.
     */
    void fill_1d(const double* x, int n_x, int n_threads = 0) {
        if (rank() != 1) throw std::invalid_argument("fill_1d: rank is not 1");
        const double* cols[1] = {x};
        fill(cols, n_x, nullptr, n_threads);
    }

    /// \see fill_1d
    void fill_1d_weighted(const double* x, int n_x,
                          const double* weights, int n_weights, int n_threads = 0) {
        if (rank() != 1) throw std::invalid_argument("fill_1d: rank is not 1");
        const double* cols[1] = {x};
        fill(cols, std::min(n_x, n_weights), weights, n_threads);
    }

    /// Fill a 2-D histogram from two plain arrays.
    void fill_2d(const double* x, int n_x, const double* y, int n_y, int n_threads = 0) {
        if (rank() != 2) throw std::invalid_argument("fill_2d: rank is not 2");
        const double* cols[2] = {x, y};
        fill(cols, std::min(n_x, n_y), nullptr, n_threads);
    }

    /// \see fill_2d
    void fill_2d_weighted(const double* x, int n_x, const double* y, int n_y,
                          const double* weights, int n_weights, int n_threads = 0) {
        if (rank() != 2) throw std::invalid_argument("fill_2d: rank is not 2");
        const double* cols[2] = {x, y};
        fill(cols, std::min(std::min(n_x, n_y), n_weights), weights, n_threads);
    }

    /*!
     * \brief Add points carrying a SAMPLE, for a profile histogram.
     *
     * The bin accumulates the mean of `sample` over the points that landed in
     * it, rather than how many there were. What a caller wants when the
     * question is "what is the average lifetime at this position", where a
     * count answers nothing.
     *
     * Serial on purpose. Welford's update is a read-modify-write of three
     * numbers per bin whose result depends on the order they are applied in,
     * so it cannot be split across private copies and summed the way a count
     * can. Correct beats fast for the rarer operation.
     */
    void fill_1d_sample(const double* x, int n_x,
                        const double* sample, int n_sample) {
        if (rank() != 1) throw std::invalid_argument("fill_1d_sample: rank is not 1");
        const double* cols[1] = {x};
        fill_sample(cols, std::min(n_x, n_sample), sample, nullptr);
    }

    /// \see fill_1d_sample. Separate from the unweighted form for the same
    /// reason fill_1d is: a binding folds (pointer, length) into one array
    /// parameter, leaving no spelling of "no array" to default to.
    void fill_1d_sample_weighted(const double* x, int n_x,
                                 const double* sample, int n_sample,
                                 const double* weights, int n_weights) {
        if (rank() != 1) throw std::invalid_argument("fill_1d_sample: rank is not 1");
        const double* cols[1] = {x};
        fill_sample(cols, std::min(std::min(n_x, n_sample), n_weights), sample, weights);
    }

    /// \see fill_1d_sample
    void fill_2d_sample(const double* x, int n_x, const double* y, int n_y,
                        const double* sample, int n_sample) {
        if (rank() != 2) throw std::invalid_argument("fill_2d_sample: rank is not 2");
        const double* cols[2] = {x, y};
        fill_sample(cols, std::min(std::min(n_x, n_y), n_sample), sample, nullptr);
    }

    /// \see fill_1d_sample_weighted
    void fill_2d_sample_weighted(const double* x, int n_x, const double* y, int n_y,
                                 const double* sample, int n_sample,
                                 const double* weights, int n_weights) {
        if (rank() != 2) throw std::invalid_argument("fill_2d_sample: rank is not 2");
        const double* cols[2] = {x, y};
        fill_sample(cols, std::min(std::min(std::min(n_x, n_y), n_sample), n_weights),
                    sample, weights);
    }

    /*!
     * \brief The per-bin mean of a profile, into a caller-owned array.
     *
     * For a non-profile histogram this is just the bin value.
     */
    void get_means(double** out, int* n) const { copy_out(values_, out, n); }

    /*!
     * \brief The SAMPLE variance of the values that landed in each bin.
     *
     * How spread out the samples were: sum of squared deviations over
     * count-1. A bin with fewer than two entries has none and reports 0
     * rather than dividing by zero.
     */
    void get_sample_variances(double** out, int* n) const {
        copy_out(derived_variance(/*of_the_mean=*/false), out, n);
    }

    /*!
     * \brief The variance OF THE MEAN -- the squared standard error.
     *
     * The sample variance divided by the count again. This is the one to put
     * an error bar on a profile point with, and it is what boost-histogram's
     * `variances()` returns for a Mean or WeightedMean storage; the two differ
     * by a factor of the count, which is large, so the distinction is worth the
     * two method names.
     */
    void get_mean_variances(double** out, int* n) const {
        copy_out(derived_variance(/*of_the_mean=*/true), out, n);
    }

    /// Profiles: the entry count (Mean) or the sum of weights (WeightedMean).
    void get_counts(double** out, int* n) const { copy_out(counts_, out, n); }

    /*!
     * \brief Fill from an arbitrary accessor, skipping rows `keep` rejects.
     *
     * Public and templated so a caller holding columns in their own types --
     * float32, int32, dictionary codes -- can fill without converting them to
     * double first. `get(d, i)` returns the value of axis `d` for row `i`;
     * `keep(i)` returns false for a row that should not be counted, which is
     * how a selection and a validity mask are applied without inventing a
     * sentinel value that later gets binned by accident.
     */
    template<typename Get, typename Keep>
    void fill_with(Get get, Keep keep, long long n_points,
                   const double* weights, int n_threads) {
        if (n_points <= 0) return;
        grow_to_fit(get, n_points);

        const int r = rank();
        const int n_cells = static_cast<int>(values_.size());

        // Variance needs a second accumulator per cell, and the shared fast
        // paths accumulate one array. Two passes over the same partition would
        // desynchronise from them for no gain, so the weighted-variance fill is
        // its own serial loop -- it is the rarer case, and the pairing of a
        // value with its variance matters more than its speed.
        if (track_variance_) {
            const std::vector<int> strides = compute_strides();
            for (long long i = 0; i < n_points; i++) {
                if (!keep(i)) continue;
                int flat = 0;
                bool ok = true;
                for (int d = 0; d < r && ok; d++) {
                    const int slot = axes_[d].slot(axes_[d].index(get(d, i)));
                    if (slot < 0) ok = false; else flat += slot * strides[d];
                }
                if (!ok) continue;
                const double w = weights ? weights[i] : 1.0;
                values_[flat] += w;
                variances_[flat] += w * w;
            }
            return;
        }

        // Rank 1 and 2 over plain ranges get their own closures, with the axis
        // constants captured BY VALUE. Reading them through a pointer into a
        // heap vector costs a load per axis per point, which the compiler
        // cannot hoist because it cannot prove the vector is not aliased by the
        // histogram being written -- and that alone made the general fill 1.4x
        // slower than boost's while the arithmetic was identical.
        if (all_axes_affine() && r > 2 && r <= kMaxFastRank) {
            // Rank 3 and above, still over plain ranges. The axis constants go
            // into a fixed-size array captured BY VALUE for the same reason as
            // below -- read through a pointer they cost a load per axis per
            // point, and a 3-D fill was 2x slower than boost's that way.
            const std::vector<FastAxis> fa = fast_axes();
            std::array<FastAxis, kMaxFastRank> a{};
            for (int d = 0; d < r; d++) a[d] = fa[d];
            dispatch_fill(n_points, n_cells, weights, n_threads,
                          [a, r, get, keep](long long i) -> int {
                if (!keep(i)) return -1;
                int flat = 0;
                for (int d = 0; d < r; d++) {
                    const FastAxis& ad = a[d];
                    const int s = ad.slot_of(get(d, i));
                    if (s < 0) return -1;
                    flat += s * ad.stride;
                }
                return flat;
            });
            return;
        }
        if (all_axes_affine() && (r == 1 || r == 2)) {
            const std::vector<FastAxis> fa = fast_axes();
            if (r == 1) {
                const FastAxis a = fa[0];
                dispatch_fill(n_points, n_cells, weights, n_threads,
                              [a, get, keep](long long i) -> int {
                    if (!keep(i)) return -1;
                    const int s = a.slot_of(get(0, i));
                    return s < 0 ? -1 : s * a.stride;
                });
            } else {
                const FastAxis a0 = fa[0], a1 = fa[1];
                dispatch_fill(n_points, n_cells, weights, n_threads,
                              [a0, a1, get, keep](long long i) -> int {
                    if (!keep(i)) return -1;
                    const int s0 = a0.slot_of(get(0, i));
                    if (s0 < 0) return -1;
                    const int s1 = a1.slot_of(get(1, i));
                    if (s1 < 0) return -1;
                    return s0 * a0.stride + s1 * a1.stride;
                });
            }
            return;
        }

        // Everything else -- transformed, categorical, circular, growing, or
        // rank 3 and above -- goes through Axis::index, which is general and
        // slower and is not on anybody's inner loop.
        const std::vector<int> strides = compute_strides();
        dispatch_fill(n_points, n_cells, weights, n_threads,
                      [&](long long i) -> int {
            if (!keep(i)) return -1;
            int flat = 0;
            for (int d = 0; d < r; d++) {
                const int slot = axes_[d].slot(axes_[d].index(get(d, i)));
                if (slot < 0) return -1;
                flat += slot * strides[d];
            }
            return flat;
        });
    }

    // --- output, for language bindings ------------------------------------

    /*!
     * \brief The value buffer itself, not a copy.
     *
     * Zero copy: a language binding wraps this pointer, so a 1024x1024
     * histogram costs nothing to look at rather than 8 MB. Reshaping to
     * \ref shape and slicing the flow bins off are both views in numpy, so the
     * whole read path stays copy-free.
     *
     * \section nd_view_safety What keeps this safe
     *
     * Two things can invalidate the pointer, and both are handled rather than
     * documented away:
     *
     * - The histogram being destroyed while a view is alive. The Python wrapper
     *   keeps a reference to the owner ON the array it hands back, so the owner
     *   cannot be collected first.
     * - The buffer being reallocated. Only one operation does that -- a fill on
     *   an axis declared with growth -- and \ref can_view reports it, so a
     *   caller can be handed a copy instead. Nothing else resizes: reset, add
     *   and scale work in place, and slice, rebin and project return new
     *   histograms rather than modifying this one.
     */
    void get_values_view(double** view, int* n) {
        if (!can_view()) { *view = nullptr; *n = 0; return; }
        *view = values_.data();
        *n = static_cast<int>(values_.size());
    }
    /// \see get_values_view
    void get_variances_view(double** view, int* n) {
        if (!can_view() || variances_.empty()) { *view = nullptr; *n = 0; return; }
        *view = variances_.data();
        *n = static_cast<int>(variances_.size());
    }

    /*!
     * \brief Whether a zero-copy view can be handed out safely.
     *
     * False when any axis can grow, because the next fill may reallocate the
     * buffer out from under a view that has already been handed out -- and
     * there is no way to reach back and invalidate a numpy array. A copy is
     * cheap next to a dangling pointer.
     */
    bool can_view() const {
        for (const Axis& a : axes_) if (a.options().growth) return false;
        return true;
    }

    /// Values including flow bins, as a fresh malloc'd array the caller owns.
    void get_values(double** out, int* n) const { copy_out(values_, out, n); }
    /// Sum of squared weights; empty unless tracking variance.
    void get_variances(double** out, int* n) const { copy_out(variances_, out, n); }
    /// Values with the flow bins removed.
    void get_values_without_flow(double** out, int* n) const {
        copy_out(values_without_flow(), out, n);
    }
    /// Stored size of each axis, flow included.
    void get_shape(int** out, int* n) const {
        const std::vector<int> s = shape();
        *n = static_cast<int>(s.size());
        *out = static_cast<int*>(std::malloc(s.size() * sizeof(int)));
        if (*out != nullptr) std::memcpy(*out, s.data(), s.size() * sizeof(int));
    }

private:
    /// Welford, and its weighted form. One pass, numerically stable, and the
    /// reason a profile fill cannot be parallelised the way a count fill is.
    void fill_sample(const double* const* columns, long long n_points,
                     const double* sample, const double* weights) {
        if (!is_profile())
            throw std::invalid_argument("a sample needs a Mean or WeightedMean histogram");
        const int r = rank();
        const std::vector<int> strides = compute_strides();
        for (long long i = 0; i < n_points; i++) {
            int flat = 0;
            bool ok = true;
            for (int d = 0; d < r && ok; d++) {
                const int slot = axes_[d].slot(axes_[d].index(columns[d][i]));
                if (slot < 0) ok = false; else flat += slot * strides[d];
            }
            if (!ok) continue;
            const double s = sample[i];
            if (storage_ == HistStorage::Mean) {
                counts_[flat] += 1.0;
                const double delta = s - values_[flat];
                values_[flat] += delta / counts_[flat];
                variances_[flat] += delta * (s - values_[flat]);
            } else {
                const double w = weights ? weights[i] : 1.0;
                counts_[flat] += w;
                aux_[flat] += w * w;
                if (counts_[flat] != 0.0) {
                    const double delta = s - values_[flat];
                    values_[flat] += w * delta / counts_[flat];
                    variances_[flat] += w * delta * (s - values_[flat]);
                }
            }
        }
    }

    std::vector<double> derived_variance(bool of_the_mean) const {
        std::vector<double> v(values_.size(), 0.0);
        for (std::size_t i = 0; i < v.size(); i++) {
            if (storage_ == HistStorage::Mean) {
                if (counts_[i] > 1.0) {
                    v[i] = variances_[i] / (counts_[i] - 1.0);
                    if (of_the_mean) v[i] /= counts_[i];
                }
            } else if (storage_ == HistStorage::WeightedMean) {
                const double sw = counts_[i];
                if (sw > 0.0) {
                    const double eff = sw - aux_[i] / sw;
                    if (eff > 0.0) {
                        v[i] = variances_[i] / eff;
                        if (of_the_mean) v[i] /= sw;
                    }
                }
            } else if (i < variances_.size()) {
                v[i] = variances_[i];
            }
        }
        return v;
    }

    void check_cols(int n_cols) const {
        if (n_cols != rank())
            throw std::invalid_argument("fill_rows: column count is not the rank");
    }

    static void copy_out(const std::vector<double>& v, double** out, int* n) {
        *n = static_cast<int>(v.size());
        *out = static_cast<double*>(std::malloc(std::max<std::size_t>(1, v.size()) * sizeof(double)));
        if (*out != nullptr && !v.empty())
            std::memcpy(*out, v.data(), v.size() * sizeof(double));
    }

    /*!
     * One axis, reduced to the numbers an affine bin lookup needs.
     *
     * Axis::index switches on the axis kind for every value of every axis. That
     * is the right shape for a description and the wrong one for an inner loop:
     * with two axes and ten million points it is twenty million jump-table
     * dispatches, and it made the general fill twice as slow as boost's while
     * the specialised histogram2D was faster than it. Axes that are plain
     * ranges -- which is nearly all of them -- collapse to this instead.
     */
    /// Ranks above this fall back to the general Axis::index path. Nothing
    /// real histograms in nine dimensions.
    static constexpr int kMaxFastRank = 8;

    struct FastAxis {
        double offset = 0.0, inv_width = 1.0;
        int n = 0, shift = 0, stride = 1;
        bool has_under = false, has_over = false;

        /*!
         * The storage slot for `v`, or -1 when there is no bin for it.
         *
         * The range is checked BEFORE the conversion to int, not after. Casting
         * floor(NaN) to int is undefined behaviour, and on this machine it came
         * out as 0 -- so every NaN in the data was silently counted in the first
         * bin, which is both wrong and a disagreement with boost. NaN is neither
         * below the axis nor on it; boost puts it in the overflow bin and so
         * does this.
         */
        inline int slot_of(double v) const {
            const double f = std::floor((v - offset) * inv_width);
            if (f >= 0.0 && f < static_cast<double>(n)) {
                return static_cast<int>(f) + shift;
            }
            if (f != f) return has_over ? n + shift : -1;      // NaN
            if (f < 0.0) return has_under ? 0 : -1;
            return has_over ? n + shift : -1;
        }
    };

    /// True when every axis is a plain untransformed range, so FastAxis applies.
    bool all_axes_affine() const {
        for (const Axis& a : axes_) {
            if (a.kind() != AxisKind::Regular) return false;
            if (a.options().circular || a.options().growth) return false;
        }
        return true;
    }

    std::vector<FastAxis> fast_axes() const {
        const std::vector<int> strides = compute_strides();
        std::vector<FastAxis> f(axes_.size());
        for (std::size_t d = 0; d < axes_.size(); d++) {
            const Axis& a = axes_[d];
            f[d].offset = a.lo();
            f[d].inv_width = a.size() / (a.hi() - a.lo());
            f[d].n = a.size();
            f[d].has_under = a.options().underflow;
            f[d].has_over = a.options().overflow;
            f[d].shift = f[d].has_under ? 1 : 0;
            f[d].stride = strides[d];
        }
        return f;
    }

    /// Pick a fill strategy for `cell_of` and run it. \see Histogram.h
    template<typename CellFn>
    void dispatch_fill(long long n_points, int n_cells, const double* weights,
                       int n_threads, CellFn cell_of) {
        const unsigned threads = histogram_fill_threads(n_points, n_cells, n_threads);
        double* out0 = values_.data();
        if (threads > 1) {
            if (histogram_should_partition(n_cells, threads)) {
                histogram_partitioned_fill(out0, n_cells, n_points, threads,
                                           weights, weights != nullptr, cell_of);
            } else {
                histogram_parallel_fill(
                        out0, n_cells, n_points, threads,
                        [&](long long a, long long b, double* out) {
                            for (long long i = a; i < b; i++) {
                                const int c = cell_of(i);
                                if (c >= 0) out[c] += weights ? weights[i] : 1.0;
                            }
                        });
            }
            return;
        }
        for (long long i = 0; i < n_points; i++) {
            const int c = cell_of(i);
            if (c >= 0) out0[c] += weights ? weights[i] : 1.0;
        }
    }

public:
    /// Add one point.
    void operator()(const double* point, double weight = 1.0) {
        const double* cols[64];
        if (rank() > 64) throw std::invalid_argument("rank > 64");
        for (int d = 0; d < rank(); d++) cols[d] = point + d;
        fill(cols, 1, &weight, 1);
    }

    // --- access -----------------------------------------------------------

    /// Value at per-axis bin indices, which may be AXIS_UNDERFLOW or size().
    double at(const int* indices) const {
        const int c = flat_of(indices);
        return c < 0 ? 0.0 : values_[c];
    }
    void set_at(const int* indices, double v) {
        const int c = flat_of(indices);
        if (c >= 0) values_[c] = v;
    }

    // --- algorithms -------------------------------------------------------

    /// \param flow include the flow bins.
    double sum(bool flow = true) const {
        if (flow) {
            double s = 0.0;
            for (double v : values_) s += v;
            return s;
        }
        double s = 0.0;
        for_each_interior([&](int, const std::vector<int>&, int c) { s += values_[c]; });
        return s;
    }

    bool empty(bool flow = true) const { return sum(flow) == 0.0; }

    /// The interior of the histogram -- flow bins removed -- in C order.
    std::vector<double> values_without_flow() const {
        std::vector<double> out;
        std::size_t n = 1;
        for (const Axis& a : axes_) n *= static_cast<std::size_t>(a.size());
        out.resize(n, 0.0);
        std::size_t k = 0;
        for_each_interior([&](int, const std::vector<int>&, int c) { out[k++] = values_[c]; });
        return out;
    }

    /*!
     * \brief Sum over every axis except the listed ones.
     *
     * The kept axes stay in the order given, so project({1, 0}) transposes as
     * well as projecting.
     */
    HistogramNd project(const int* keep, int n_keep) const {
        if (n_keep < 1 || n_keep > rank())
            throw std::invalid_argument("project: bad number of axes");
        std::vector<Axis> kept;
        for (int i = 0; i < n_keep; i++) {
            if (keep[i] < 0 || keep[i] >= rank())
                throw std::invalid_argument("project: axis out of range");
            kept.push_back(axes_[keep[i]]);
        }
        HistogramNd out(kept, track_variance_);

        const std::vector<int> strides = compute_strides();
        const std::vector<int> out_strides = out.compute_strides();
        std::vector<int> slots(rank(), 0);
        const std::size_t n = values_.size();
        for (std::size_t c = 0; c < n; c++) {
            // decompose c into per-axis slots
            std::size_t rest = c;
            for (int d = 0; d < rank(); d++) {
                slots[d] = static_cast<int>(rest / strides[d]);
                rest %= strides[d];
            }
            int oc = 0;
            for (int i = 0; i < n_keep; i++) oc += slots[keep[i]] * out_strides[i];
            out.values_[oc] += values_[c];
            if (track_variance_) out.variances_[oc] += variances_[c];
        }
        return out;
    }

    /*!
     * \brief Merge every `group` adjacent bins of axis `dim`.
     *
     * Flow bins are preserved as flow bins. A trailing partial group is merged
     * into the overflow bin rather than dropped, so the total is unchanged --
     * `sum()` before and after a rebin must agree, or the operation has quietly
     * lost data.
     */
    HistogramNd rebin(int dim, int group) const {
        if (dim < 0 || dim >= rank()) throw std::invalid_argument("rebin: axis out of range");
        if (group < 1) throw std::invalid_argument("rebin: group must be >= 1");
        if (group == 1) return *this;

        const Axis& a = axes_[dim];
        const int new_n = a.size() / group;
        if (new_n < 1) throw std::invalid_argument("rebin: group larger than the axis");

        std::vector<Axis> na = axes_;
        na[dim] = rebinned_axis(a, group, new_n);
        HistogramNd out(na, track_variance_);

        const std::vector<int> strides = compute_strides();
        const std::vector<int> out_strides = out.compute_strides();
        std::vector<int> slots(rank(), 0);
        const std::size_t n = values_.size();
        for (std::size_t c = 0; c < n; c++) {
            std::size_t rest = c;
            for (int d = 0; d < rank(); d++) {
                slots[d] = static_cast<int>(rest / strides[d]);
                rest %= strides[d];
            }
            const int shift = a.options().underflow ? 1 : 0;
            const int idx = slots[dim] - shift;          // -1 = underflow, size = overflow
            int new_idx;
            if (idx < 0) new_idx = -1;
            else if (idx >= a.size()) new_idx = new_n;
            else {
                new_idx = idx / group;
                // whatever did not fill a whole group goes to overflow
                if (new_idx >= new_n) new_idx = new_n;
            }
            const int new_slot = out.axes_[dim].slot(new_idx);
            if (new_slot < 0) continue;
            int oc = 0;
            for (int d = 0; d < rank(); d++)
                oc += (d == dim ? new_slot : slots[d]) * out_strides[d];
            out.values_[oc] += values_[c];
            if (track_variance_) out.variances_[oc] += variances_[c];
        }
        return out;
    }

    /*!
     * \brief Keep bins [begin, end) of axis `dim`, discarding the rest.
     *
     * What was cut off is added to the flow bins rather than dropped, so
     * `sum(true)` is unchanged -- a slice that loses counts is a crop, and
     * boost distinguishes them for the same reason. \see crop
     */
    HistogramNd slice(int dim, int begin, int end) const {
        return sliced(dim, begin, end, /*keep_outside=*/true);
    }

    /// Like \ref slice, but what falls outside is discarded, flow included.
    HistogramNd crop(int dim, int begin, int end) const {
        return sliced(dim, begin, end, /*keep_outside=*/false);
    }

    /*!
     * \brief Restrict axis `dim` to the bins overlapping [lo, hi).
     *
     * The value form of \ref slice: a caller who knows the range of interest
     * should not have to convert it to bin indices and get the rounding right.
     */
    HistogramNd shrink(int dim, double lo, double hi) const {
        if (dim < 0 || dim >= rank()) throw std::invalid_argument("shrink: axis out of range");
        const Axis& a = axes_[dim];
        int begin = a.index(lo);
        int end = a.index(hi);
        if (begin == AXIS_UNDERFLOW) begin = 0;
        if (end == AXIS_UNDERFLOW) end = 0;
        if (begin > a.size()) begin = a.size();
        if (end >= a.size()) end = a.size(); else end += 1;
        if (end <= begin) throw std::invalid_argument("shrink: empty range");
        return slice(dim, begin, end);
    }

    /// Zero every bin, keeping the axes.
    void reset() {
        std::fill(values_.begin(), values_.end(), 0.0);
        std::fill(variances_.begin(), variances_.end(), 0.0);
        std::fill(counts_.begin(), counts_.end(), 0.0);
        std::fill(aux_.begin(), aux_.end(), 0.0);
    }

    /*!
     * \brief Add another histogram bin by bin.
     *
     * The axes must match exactly. Adding histograms with different binning is
     * not a merge, it is a mistake that happens to typecheck.
     */
    void add(const HistogramNd& other) {
        if (other.values_.size() != values_.size())
            throw std::invalid_argument("add: histograms have different shapes");
        for (std::size_t i = 0; i < values_.size(); i++) values_[i] += other.values_[i];
        if (track_variance_ && other.track_variance_)
            for (std::size_t i = 0; i < variances_.size(); i++)
                variances_[i] += other.variances_[i];
    }

    /// Multiply every bin by `factor`. Variances scale by the square.
    void scale(double factor) {
        for (double& v : values_) v *= factor;
        for (double& v : variances_) v *= factor * factor;
    }

private:
    friend class HistogramNdAccess;

    HistogramNd sliced(int dim, int begin, int end, bool keep_outside) const {
        if (dim < 0 || dim >= rank()) throw std::invalid_argument("slice: axis out of range");
        const Axis& a = axes_[dim];
        if (begin < 0 || end > a.size() || end <= begin)
            throw std::invalid_argument("slice: range outside the axis");

        std::vector<Axis> na = axes_;
        na[dim] = sub_axis(a, begin, end);
        HistogramNd out(na, track_variance_);

        const std::vector<int> strides = compute_strides();
        const std::vector<int> out_strides = out.compute_strides();
        std::vector<int> slots(rank(), 0);
        const int shift = a.options().underflow ? 1 : 0;
        for (std::size_t c = 0; c < values_.size(); c++) {
            std::size_t rest = c;
            for (int d = 0; d < rank(); d++) {
                slots[d] = static_cast<int>(rest / strides[d]);
                rest %= strides[d];
            }
            const int idx = slots[dim] - shift;
            int new_idx;
            if (idx >= begin && idx < end) new_idx = idx - begin;
            else if (!keep_outside) continue;
            else new_idx = (idx < begin) ? AXIS_UNDERFLOW : (end - begin);
            const int new_slot = out.axes_[dim].slot(new_idx);
            if (new_slot < 0) continue;
            int oc = 0;
            for (int d = 0; d < rank(); d++)
                oc += (d == dim ? new_slot : slots[d]) * out_strides[d];
            out.values_[oc] += values_[c];
            if (track_variance_) out.variances_[oc] += variances_[c];
        }
        return out;
    }

    static Axis sub_axis(const Axis& a, int begin, int end) {
        AxisOptions o = a.options();
        const int n = end - begin;
        switch (a.kind()) {
            case AxisKind::Regular:
                return Axis::regular(n, a.bin_lower(begin), a.bin_upper(end - 1), o, a.label());
            case AxisKind::Log:
                return Axis::log(n, a.bin_lower(begin), a.bin_upper(end - 1), o, a.label());
            case AxisKind::Integer:
                return Axis::integer(static_cast<int>(a.lo()) + begin,
                                     static_cast<int>(a.lo()) + end, o, a.label());
            default: {
                std::vector<double> edges;
                edges.reserve(n + 1);
                for (int i = begin; i < end; i++) edges.push_back(a.bin_lower(i));
                edges.push_back(a.bin_upper(end - 1));
                return Axis::variable(edges.data(), static_cast<int>(edges.size()), o, a.label());
            }
        }
    }

    void reset_storage() {
        std::size_t n = 1;
        for (const Axis& a : axes_) n *= static_cast<std::size_t>(a.extent());
        values_.assign(n, 0.0);
        variances_.assign(track_variance_ ? n : 0, 0.0);
        counts_.assign(is_profile() ? n : 0, 0.0);
        aux_.assign(storage_ == HistStorage::WeightedMean ? n : 0, 0.0);
    }

    std::vector<int> compute_strides() const {
        std::vector<int> s(axes_.size(), 1);
        for (int d = rank() - 2; d >= 0; d--) s[d] = s[d + 1] * axes_[d + 1].extent();
        return s;
    }

    int flat_of(const int* indices) const {
        const std::vector<int> strides = compute_strides();
        int flat = 0;
        for (int d = 0; d < rank(); d++) {
            const int slot = axes_[d].slot(indices[d]);
            if (slot < 0) return -1;
            flat += slot * strides[d];
        }
        return flat;
    }

    template<typename Fn>
    void for_each_interior(Fn fn) const {
        const std::vector<int> strides = compute_strides();
        std::vector<int> idx(axes_.size(), 0);
        const int r = rank();
        for (;;) {
            int c = 0;
            for (int d = 0; d < r; d++) c += axes_[d].slot(idx[d]) * strides[d];
            fn(r, idx, c);
            int d = r - 1;
            for (; d >= 0; d--) {
                if (++idx[d] < axes_[d].size()) break;
                idx[d] = 0;
            }
            if (d < 0) break;
        }
    }

    static Axis rebinned_axis(const Axis& a, int group, int new_n) {
        AxisOptions o = a.options();
        switch (a.kind()) {
            case AxisKind::Regular:
                return Axis::regular(new_n, a.lo(), a.bin_upper(new_n * group - 1), o, a.label());
            case AxisKind::Log:
                return Axis::log(new_n, a.lo(), a.bin_upper(new_n * group - 1), o, a.label());
            case AxisKind::Integer:
                return Axis::integer(static_cast<int>(a.lo()),
                                     static_cast<int>(a.lo()) + new_n, o, a.label());
            default: {
                // Everything else becomes a Variable axis: taking every group-th
                // edge is exactly right for arbitrary edges and is also correct
                // for the transformed axes, just less descriptive.
                std::vector<double> edges;
                edges.reserve(new_n + 1);
                for (int i = 0; i <= new_n; i++)
                    edges.push_back(a.bin_lower(std::min(i * group, a.size() - 1)));
                edges.back() = a.bin_upper(new_n * group - 1);
                return Axis::variable(edges.data(), static_cast<int>(edges.size()), o, a.label());
            }
        }
    }

    std::vector<Axis> axes_;
    std::vector<double> values_;
    std::vector<double> variances_;
    /// Profiles only: the running count (Mean) or sum of weights (WeightedMean).
    std::vector<double> counts_;
    /// WeightedMean only: the sum of squared weights.
    std::vector<double> aux_;
    HistStorage storage_ = HistStorage::Counts;
    bool track_variance_ = false;

    template<typename Get>
    void grow_to_fit(Get get, long long n_points) {
        bool any_growth = false;
        for (const Axis& a : axes_) any_growth = any_growth || a.options().growth;
        if (!any_growth) return;

        // Growth is rare and a fill that triggers it is already paying for a
        // reallocation, so this scans rather than trying to be clever. What it
        // must not do is lose the counts already stored, so the old contents are
        // re-placed into the new storage by bin index.
        bool changed = false;
        std::vector<Axis> grown = axes_;
        for (int d = 0; d < rank(); d++) {
            if (!axes_[d].options().growth) continue;
            double lo = axes_[d].lo(), hi = axes_[d].hi();
            for (long long i = 0; i < n_points; i++) {
                const double x = get(d, i);
                if (!std::isfinite(x)) continue;
                if (x < lo) lo = x;
                if (x >= hi) hi = std::nextafter(x, x + 1.0);
            }
            if (lo != axes_[d].lo() || hi != axes_[d].hi()) {
                const double w = (axes_[d].hi() - axes_[d].lo()) / axes_[d].size();
                const int n = std::max(1, static_cast<int>(std::ceil((hi - lo) / w)));
                grown[d] = Axis::regular(n, lo, lo + n * w,
                                         axes_[d].options(), axes_[d].label());
                changed = true;
            }
        }
        if (!changed) return;

        HistogramNd bigger(grown, track_variance_);
        std::vector<int> idx(rank(), 0);
        for_each_interior([&](int r, const std::vector<int>& at, int c) {
            if (values_[c] == 0.0) return;
            for (int d = 0; d < r; d++) {
                idx[d] = axes_[d].options().growth
                         ? bigger.axes_[d].index(axes_[d].bin_center(at[d]))
                         : at[d];
            }
            const int nc = bigger.flat_of(idx.data());
            if (nc >= 0) {
                bigger.values_[nc] += values_[c];
                if (track_variance_) bigger.variances_[nc] += variances_[c];
            }
        });
        *this = std::move(bigger);
    }
};

}  // namespace hist
}  // namespace tttrlib

#endif  // TTTRLIB_HISTOGRAMND_H
