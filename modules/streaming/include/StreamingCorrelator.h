// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_STREAMING_CORRELATOR_H
#define TTTRLIB_STREAMING_CORRELATOR_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace tttrlib {

// StreamingCorrelator — online multi-tau correlator (Schätzel architecture).
//
// Photons are binned into a uniform macro-time intensity trace. Each arriving
// intensity sample feeds a cascade of correlation levels; level b runs at 2^b
// coarser resolution, its samples being pairwise sums of level b-1's. Because
// the bins are aligned to macro time 0, a level-b bin is exactly the batch
// correlator's `t >> b` bin, so the two agree sample for sample.
//
// # The lag axis is not "half_bins per level"
//
// The axis is the same one `CorrelatorCurve::update_axis` builds:
//
//     x[0] = 0;  x[j] = x[j-1] + step;  step doubles when j % n_bins == 0
//
// and block b — output indices b*n_bins+1 .. b*n_bins+n_bins — holds *coarse*
// lags offset(b)+1 .. offset(b)+n_bins, where
//
//     offset(b) = x[b*n_bins] / 2^b
//
// For n_bins = 16 that is 0, 8, 12, 14, 15, 15, ... — it converges to
// n_bins-1, it is not n_bins/2 at every level. Reading every level at a fixed
// n_bins/2 (as this class used to) reports the correlation at a *shorter* lag
// than the axis claims, and since G(tau) falls with tau that reads as an
// inflated G: measured 1.19-2.39x too high from cascade 2 up, growing with the
// cascade because the mismatch grows. Each level therefore carries its own
// first lag, and accumulates only the n_bins lags it will actually be asked
// for.
//
// # Two channels
//
// `push_photon(mt, w)` feeds both channels — the autocorrelation. The
// three-argument form feeds one channel, for a cross-correlation. As in the
// batch correlator, the lag runs from channel 0 to channel 1: the accumulator
// pairs a channel-1 photon with a channel-0 photon that arrived `tau` earlier.
//
// Photons must arrive in non-decreasing macro time, across both channels —
// this is a streaming consumer, and a photon older than the bin already
// emitted cannot be placed.
//
class StreamingCorrelator {
public:
    StreamingCorrelator(int n_bins = 16, int n_casc = 25,
                        double macro_time_resolution = 1.0)
        : n_bins_(n_bins), n_casc_(n_casc),
          macro_time_resolution_(macro_time_resolution) {
        if (n_bins_ < 2 || n_casc_ < 1)
            throw std::invalid_argument("StreamingCorrelator: need n_bins >= 2 and n_casc >= 1");
        build_axis();
        init();
    }

    void clear() {
        for (auto& lvl : levels_) lvl.clear();
        cur1_ = cur2_ = 0.0;
        total1_ = total2_ = 0.0;
        n0_ = 0;
        next_bin_ = 1;
        photon_count_ = 0;
        max_mt_ = 0;
        min_mt_ = 0;
        have_first_ = false;
        finished_ = false;
    }

    /// Feed one photon to both channels — the autocorrelation.
    void push_photon(uint64_t macro_time, double weight = 1.0) {
        advance_to(macro_time);
        cur1_ += weight;
        cur2_ += weight;
        note(macro_time, weight, weight);
    }

    /// Feed one photon to a single channel. The lag runs 0 -> 1.
    void push_photon(uint64_t macro_time, double weight, int channel) {
        advance_to(macro_time);
        if (channel == 0) { cur1_ += weight; note(macro_time, weight, 0.0); }
        else              { cur2_ += weight; note(macro_time, 0.0, weight); }
    }

    void push_photons(const uint64_t* macro_times, const double* weights, int n) {
        for (int i = 0; i < n; ++i)
            push_photon(macro_times[i], weights ? weights[i] : 1.0);
    }

    void push_photons(const uint64_t* macro_times, const double* weights,
                      const int* channels, int n) {
        for (int i = 0; i < n; ++i)
            push_photon(macro_times[i], weights ? weights[i] : 1.0,
                        channels ? channels[i] : 0);
    }

    /// Emit every bin up to and including the one holding the last photon.
    /// Idempotent: calling it twice does not append empty bins.
    void flush() {
        if (finished_ || photon_count_ == 0) return;
        advance_to(max_mt_);               // every bin before the last photon's
        feed_sample(cur1_, cur2_);         // and the bin holding it
        cur1_ = cur2_ = 0.0;
        next_bin_++;
        finished_ = true;
    }

    size_t photon_count() const { return photon_count_; }
    int n_bins() const { return n_bins_; }
    int n_casc() const { return n_casc_; }
    /// Number of correlation channels: n_casc * n_bins + 1, as in the batch
    /// correlator (index 0 is lag zero and is never filled).
    int n_corr() const { return n_bins_ * n_casc_ + 1; }

    std::vector<double> get_x_axis() const {
        std::vector<double> axis(taus_.size());
        for (size_t j = 0; j < taus_.size(); ++j)
            axis[j] = static_cast<double>(taus_[j]) * macro_time_resolution_;
        return axis;
    }

    /// Unnormalised pair sums, one per lag on the axis above.
    std::vector<double> get_correlation() const {
        std::vector<double> out(taus_.size(), 0.0);
        for (int b = 0; b < n_casc_ && b < static_cast<int>(levels_.size()); ++b) {
            const Level& lvl = levels_[b];
            const int base = b * n_bins_;
            for (int i = 0; i < n_bins_; ++i)
                out[base + 1 + i] = lvl.g[i];
        }
        return out;
    }

    /// Normalised correlation, matching `Correlator::normalize_ccf_wahl`:
    /// divide by the coarse bin width, the two count rates, and the overlap
    /// T - tau.
    std::vector<double> get_correlation_normalized() const {
        auto raw = get_correlation();
        std::vector<double> normed(raw.size(), 0.0);
        const double T = duration();
        if (T < 1.0) return normed;
        const double n1 = total1_, n2 = total2_;
        if (n1 <= 0.0 || n2 <= 0.0) return normed;
        const double cr1 = n1 / T, cr2 = n2 / T;

        for (size_t j = 1; j < raw.size(); ++j) {
            const int shift = static_cast<int>((j - 1) / n_bins_);
            const double pw = static_cast<double>(1ULL << shift);
            const double dt = T - static_cast<double>(taus_[j]);
            if (dt > 0.0) normed[j] = raw[j] / (pw * cr1 * cr2 * dt);
        }
        return normed;
    }

private:
    int n_bins_;
    int n_casc_;
    double macro_time_resolution_;

    std::vector<uint64_t> taus_;   // fine-resolution lag axis, n_casc*n_bins+1

    // One cascade level. It accumulates only the n_bins lags the output asks
    // of it, so both the history and the inner loop are as short as the axis
    // allows: g[i] is coarse lag lo + i, and h1[k] is the channel-0 sample k
    // coarse bins ago (h1[0] = newest).
    //
    // `acc` holds the level-0 samples seen since this level last emitted. That
    // is the same quantity the classic pending/phase pair carries, written so
    // that a run of empty bins can be skipped in closed form instead of being
    // stepped through — see skip_samples.
    struct Level {
        int lo = 1;
        std::vector<double> g;
        std::vector<double> h1, h2;
        double acc1 = 0.0, acc2 = 0.0;
        int fill = 0;

        void resize(int first_lag, int n_bins) {
            lo = first_lag;
            g.assign(n_bins, 0.0);
            h1.assign(first_lag + n_bins, 0.0);
            h2.assign(first_lag + n_bins, 0.0);
            clear();
        }
        void clear() {
            std::fill(g.begin(), g.end(), 0.0);
            std::fill(h1.begin(), h1.end(), 0.0);
            std::fill(h2.begin(), h2.end(), 0.0);
            acc1 = acc2 = 0.0; fill = 0;
        }
    };

    std::vector<Level> levels_;
    double cur1_ = 0.0, cur2_ = 0.0;
    double total1_ = 0.0, total2_ = 0.0;
    uint64_t next_bin_ = 1;
    uint64_t n0_ = 0;               // level-0 samples emitted so far
    uint64_t max_mt_ = 0;
    uint64_t min_mt_ = 0;
    bool have_first_ = false;
    bool finished_ = false;
    size_t photon_count_ = 0;

    void build_axis() {
        taus_.assign(static_cast<size_t>(n_bins_) * n_casc_ + 1, 0);
        uint64_t step = 1;
        for (size_t j = 1; j < taus_.size(); ++j) {
            taus_[j] = taus_[j - 1] + step;
            if (j % static_cast<size_t>(n_bins_) == 0) step <<= 1;
        }
    }

    /// First coarse lag of level b — the batch correlator's
    /// `taus[b * n_bins] / 2^b`, integer division included.
    int first_lag(int b) const {
        return static_cast<int>(taus_[static_cast<size_t>(b) * n_bins_] >> b) + 1;
    }

    void init() {
        levels_.assign(n_casc_, Level());
        for (int b = 0; b < n_casc_; ++b) levels_[b].resize(first_lag(b), n_bins_);
        clear();
    }

    void note(uint64_t macro_time, double w1, double w2) {
        photon_count_++;
        total1_ += w1;
        total2_ += w2;
        if (!have_first_) { min_mt_ = macro_time; have_first_ = true; }
        if (macro_time > max_mt_) max_mt_ = macro_time;
    }

    /// Close every bin strictly before the one holding `macro_time`.
    ///
    /// The bins between two photons are empty, and at a native macro-time
    /// resolution there are typically hundreds to thousands of them per photon.
    /// Stepping through them costs the same as a photon each; skipping them in
    /// closed form is what keeps the cost proportional to the photons rather
    /// than to the acquisition length.
    void advance_to(uint64_t macro_time) {
        if (finished_)
            throw std::runtime_error("StreamingCorrelator: push_photon after flush()");
        if (macro_time < next_bin_) return;
        feed_sample(cur1_, cur2_);
        cur1_ = cur2_ = 0.0;
        const uint64_t empties = macro_time - next_bin_;
        next_bin_ += 1 + empties;
        if (empties) skip_samples(empties);
    }

    double duration() const {
        return static_cast<double>(max_mt_ - min_mt_);
    }

    /// Push one sample into level b: shift the histories, then pair the newest
    /// channel-1 sample with the channel-0 samples lo..lo+n_bins-1 bins back.
    void push_level(int b, double v1, double v2) {
        Level& lvl = levels_[b];
        const int hd = static_cast<int>(lvl.h1.size());
        for (int i = hd - 1; i > 0; --i) {
            lvl.h1[i] = lvl.h1[i - 1];
            lvl.h2[i] = lvl.h2[i - 1];
        }
        lvl.h1[0] = v1;
        lvl.h2[0] = v2;
        if (lvl.fill < hd) lvl.fill++;

        if (v2 != 0.0) {
            const int lo = lvl.lo;
            const int kmax = std::min(n_bins_, lvl.fill - lo);
            for (int i = 0; i < kmax; ++i)
                lvl.g[i] += v2 * lvl.h1[lo + i];
        }
    }

    /// Push `m` zero samples into level b. Zeros contribute nothing to g, so
    /// only the history moves — and it moves at most its own depth.
    void skip_level(int b, uint64_t m) {
        if (m == 0) return;
        Level& lvl = levels_[b];
        const int hd = static_cast<int>(lvl.h1.size());
        if (m >= static_cast<uint64_t>(hd)) {
            std::fill(lvl.h1.begin(), lvl.h1.end(), 0.0);
            std::fill(lvl.h2.begin(), lvl.h2.end(), 0.0);
            lvl.fill = hd;
            return;
        }
        const int mi = static_cast<int>(m);
        for (int i = hd - 1; i >= mi; --i) {
            lvl.h1[i] = lvl.h1[i - mi];
            lvl.h2[i] = lvl.h2[i - mi];
        }
        std::fill(lvl.h1.begin(), lvl.h1.begin() + mi, 0.0);
        std::fill(lvl.h2.begin(), lvl.h2.begin() + mi, 0.0);
        lvl.fill = std::min(hd, lvl.fill + mi);
    }

    /// One level-0 sample: every level accumulates it, and the levels whose
    /// period has elapsed emit.
    void feed_sample(double v1, double v2) {
        for (int b = 0; b < n_casc_; ++b) {
            levels_[b].acc1 += v1;
            levels_[b].acc2 += v2;
        }
        n0_++;
        for (int b = 0; b < n_casc_ && b < 63; ++b) {
            if ((n0_ & ((1ULL << b) - 1)) != 0) break;   // not this level's turn
            Level& lvl = levels_[b];
            push_level(b, lvl.acc1, lvl.acc2);
            lvl.acc1 = lvl.acc2 = 0.0;
        }
    }

    /// `k` empty level-0 samples, in O(n_casc * depth) rather than O(k).
    ///
    /// Level b emits `n0/2^b` times, so the number of emissions the run covers
    /// is a difference of two divisions. Only the *first* of them can be
    /// non-zero — it carries the accumulator left over from before the run —
    /// and every later one is a sum of zeros.
    void skip_samples(uint64_t k) {
        if (k == 0) return;
        const uint64_t start = n0_;
        n0_ += k;
        for (int b = 0; b < n_casc_ && b < 63; ++b) {
            const uint64_t period = 1ULL << b;
            const uint64_t cnt = (n0_ / period) - (start / period);
            if (cnt == 0) break;      // a coarser level cannot have emitted either
            Level& lvl = levels_[b];
            push_level(b, lvl.acc1, lvl.acc2);
            lvl.acc1 = lvl.acc2 = 0.0;
            skip_level(b, cnt - 1);
        }
    }
};

} // namespace tttrlib

#endif // TTTRLIB_STREAMING_CORRELATOR_H
