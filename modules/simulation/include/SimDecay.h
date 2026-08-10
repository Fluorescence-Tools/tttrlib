/*!
 * \file SimDecay.h
 * \brief Per-species decay pattern for micro-time (FLIM) sampling.
 *
 * A decay pattern is an arbitrary probability density over micro-time (its own axis:
 * bin width `dt`, offset `t0`, arbitrary length — so the micro-time *range* can vary
 * per species). It is the first-class representation: a `SimDecay` is always built
 * `from_pattern(...)` — the pattern can be an experimental/simulated decay or an array
 * produced by the optional helpers below (multi-exponential density, an IRF pattern,
 * convolution). The IRF is itself an arbitrary pattern, not only Gaussian. Micro-times
 * are drawn by O(1) alias sampling; the engine maps them onto the instrument's
 * micro-time channel axis. Header-only. Additive; existing tttrlib untouched.
 */
#ifndef TTTRLIB_SIMDECAY_H
#define TTTRLIB_SIMDECAY_H

#include <cstdint>
#include <cmath>
#include <vector>

namespace tttrlib {

class SimDecay {
public:
    double t0 = 0.0;   ///< micro-time of pattern bin 0 (ns)
    double dt = 0.008; ///< pattern bin width (ns) — the pattern's own resolution/range

    SimDecay() = default;

    bool empty() const { return prob_.empty(); }
    int n_bins() const { return int(pdf_.size()); }

    /*!
     * \brief Normalised probability of micro-time bin `bin` (0 outside the axis).
     *
     * The alias table samples in O(1) but cannot be read back as a density, so
     * the normalised pattern is kept beside it. That is what makes one
     * `SimDecay` both *draw* a micro-time and *score* one, which is the point:
     * a simulator and a likelihood that share an object cannot drift apart the
     * way two implementations of the same decay silently do.
     */
    double pdf(int bin) const {
        return (bin < 0 || bin >= int(pdf_.size())) ? 0.0 : pdf_[bin];
    }
    /// The whole normalised density, summing to 1 (empty when the pattern was).
    const std::vector<double>& pdf() const { return pdf_; }

    // --- pattern builders (static helpers operate on plain arrays) --------------

    /// Unnormalised multi-exponential density Σ a_c·exp(-t/τ_c) over `n_bins`.
    static std::vector<double> multi_exponential_pattern(
        const std::vector<double>& amplitudes, const std::vector<double>& lifetimes,
        int n_bins, double dt, double t0 = 0.0) {
        std::vector<double> p(n_bins > 0 ? n_bins : 0, 0.0);
        for (int k = 0; k < n_bins; ++k) {
            double t = t0 + k * dt;
            if (t < 0.0) continue;
            double v = 0.0;
            for (size_t c = 0; c < lifetimes.size() && c < amplitudes.size(); ++c)
                if (lifetimes[c] > 0.0) v += amplitudes[c] * std::exp(-t / lifetimes[c]);
            p[k] = v > 0.0 ? v : 0.0;
        }
        return p;
    }

    /// A Gaussian IRF pattern (one convenient IRF shape; any array works as an IRF).
    static std::vector<double> gaussian_irf(int n_bins, double dt,
                                            double mean = 0.0, double fwhm = 0.1) {
        std::vector<double> irf(n_bins > 0 ? n_bins : 0, 0.0);
        if (fwhm <= 0.0) { if (n_bins > 0) irf[0] = 1.0; return irf; }
        double sigma = fwhm * 0.4246609 / dt, mu = mean / dt;
        for (int k = 0; k < n_bins; ++k) {
            double z = (k - mu) / sigma;
            irf[k] = std::exp(-0.5 * z * z);
        }
        return irf;
    }

    /// Full (linear) convolution of a decay with an IRF pattern, truncated to `sig`'s length.
    static std::vector<double> convolve(const std::vector<double>& sig,
                                        const std::vector<double>& irf) {
        size_t n = sig.size(), m = irf.size();
        std::vector<double> out(n, 0.0);
        if (m == 0) return sig;
        for (size_t k = 0; k < n; ++k) {
            double acc = 0.0;
            size_t dmax = k < m - 1 ? k : m - 1;
            for (size_t d = 0; d <= dmax; ++d) acc += sig[k - d] * irf[d];
            out[k] = acc;
        }
        return out;
    }

    // --- factories -------------------------------------------------------------

    /// Build from an arbitrary (unnormalised) density over micro-time bins.
    static SimDecay from_pattern(const std::vector<double>& pdf, double dt, double t0 = 0.0) {
        SimDecay d; d.dt = dt; d.t0 = t0; d.build_alias(pdf); return d;
    }

    /// Multi-exponential decay (no IRF).
    static SimDecay multi_exponential(const std::vector<double>& amplitudes,
                                      const std::vector<double>& lifetimes,
                                      int n_bins, double dt, double t0 = 0.0) {
        return from_pattern(multi_exponential_pattern(amplitudes, lifetimes, n_bins, dt, t0), dt, t0);
    }

    /// Multi-exponential convolved with an arbitrary IRF pattern (bin width `dt`, starts at bin 0).
    static SimDecay multi_exponential_with_irf(const std::vector<double>& amplitudes,
                                               const std::vector<double>& lifetimes,
                                               int n_bins, double dt,
                                               const std::vector<double>& irf, double t0 = 0.0) {
        auto decay = multi_exponential_pattern(amplitudes, lifetimes, n_bins, dt, t0);
        return from_pattern(convolve(decay, irf), dt, t0);
    }

    /// Draw a micro-time (ns) from the pattern (alias sampling + sub-bin jitter).
    template <class Rng>
    double sample_ns(Rng& rng) const {
        uint32_t n = uint32_t(prob_.size());
        if (n == 0) return 0.0;
        uint32_t i = uint32_t(rng.random0i1e() * n); if (i >= n) i = n - 1;
        uint32_t bin = (rng.random0i1e() < prob_[i]) ? i : alias_[i];
        return t0 + (double(bin) + rng.random0i1e()) * dt;
    }

private:
    // Vose's alias method for O(1) sampling from a discrete distribution.
    void build_alias(const std::vector<double>& w) {
        size_t n = w.size();
        double sum = 0.0;
        for (double x : w) if (x > 0.0) sum += x;
        if (n == 0 || sum <= 0.0) { prob_.clear(); alias_.clear(); pdf_.clear(); return; }
        pdf_.assign(n, 0.0);
        for (size_t i = 0; i < n; ++i) pdf_[i] = (w[i] > 0.0 ? w[i] : 0.0) / sum;
        prob_.assign(n, 0.0); alias_.assign(n, 0);
        std::vector<double> scaled(n);
        std::vector<uint32_t> small, large;
        for (size_t i = 0; i < n; ++i) {
            scaled[i] = (w[i] > 0.0 ? w[i] : 0.0) * double(n) / sum;
            (scaled[i] < 1.0 ? small : large).push_back(uint32_t(i));
        }
        while (!small.empty() && !large.empty()) {
            uint32_t s = small.back(); small.pop_back();
            uint32_t l = large.back(); large.pop_back();
            prob_[s] = scaled[s]; alias_[s] = l;
            scaled[l] = (scaled[l] + scaled[s]) - 1.0;
            (scaled[l] < 1.0 ? small : large).push_back(l);
        }
        while (!large.empty()) { uint32_t l = large.back(); large.pop_back(); prob_[l] = 1.0; alias_[l] = l; }
        while (!small.empty()) { uint32_t s = small.back(); small.pop_back(); prob_[s] = 1.0; alias_[s] = s; }
    }

    std::vector<double> prob_;
    std::vector<uint32_t> alias_;
    std::vector<double> pdf_;   ///< normalised density, for scoring
};

} // namespace tttrlib

#endif // TTTRLIB_SIMDECAY_H
