// SPDX-License-Identifier: BSD-3-Clause
#include "HmmLattice.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace tttrlib {

namespace {

constexpr double NEG_INF = -std::numeric_limits<double>::infinity();

void require(bool ok, const char* what) {
    if (!ok) throw std::invalid_argument(std::string("hmm lattice: ") + what);
}

// The shape check every entry point shares. Kept in one place because the
// failure it catches -- a frame matrix transposed into (K, T) -- reads as
// garbage numbers rather than as an error if it is not caught here.
void check_model(int n_startprob, int n_trans1, int n_trans2, int n_states) {
    require(n_states > 0, "n_states must be positive");
    require(n_startprob == n_states,
            "log_startprob has a different length than log_frameprob has columns");
    require(n_trans1 == n_states && n_trans2 == n_states,
            "log_transmat must be (n_states, n_states)");
}

double logsumexp(const double* v, int n) {
    double vmax = NEG_INF;
    for (int i = 0; i < n; ++i)
        if (v[i] > vmax) vmax = v[i];
    // -inf in, -inf out. Without this the subtraction below is inf - inf.
    if (vmax == NEG_INF) return NEG_INF;
    double acc = 0.0;
    for (int i = 0; i < n; ++i)
        acc += std::exp(v[i] - vmax);
    return std::log(acc) + vmax;
}

double forward(
        const double* log_startprob, const double* log_transmat,
        const double* log_frameprob, int T, int K, double* fwd) {
    std::vector<double> work(static_cast<size_t>(K));
    for (int j = 0; j < K; ++j)
        fwd[j] = log_startprob[j] + log_frameprob[j];
    for (int t = 1; t < T; ++t) {
        const double* prev = fwd + static_cast<size_t>(t - 1) * K;
        double* row = fwd + static_cast<size_t>(t) * K;
        const double* frame = log_frameprob + static_cast<size_t>(t) * K;
        for (int j = 0; j < K; ++j) {
            for (int i = 0; i < K; ++i)
                work[static_cast<size_t>(i)] = prev[i] + log_transmat[static_cast<size_t>(i) * K + j];
            row[j] = logsumexp(work.data(), K) + frame[j];
        }
    }
    return logsumexp(fwd + static_cast<size_t>(T - 1) * K, K);
}

void backward_posteriors_xi(
        const double* log_transmat, const double* log_frameprob,
        const double* fwd, double log_prob, int T, int K,
        double* posteriors, double* xi_sum) {
    std::vector<double> bwd_next(static_cast<size_t>(K), 0.0);
    std::vector<double> bwd_current(static_cast<size_t>(K));
    std::vector<double> work(static_cast<size_t>(K));
    const double uniform = 1.0 / static_cast<double>(K);

    {
        double* last = posteriors + static_cast<size_t>(T - 1) * K;
        const double* fwd_last = fwd + static_cast<size_t>(T - 1) * K;
        double total = 0.0;
        for (int j = 0; j < K; ++j) {
            last[j] = std::exp(fwd_last[j] - log_prob);
            total += last[j];
        }
        // total == 0 is a frame no state can explain. Dividing would be 0/0.
        for (int j = 0; j < K; ++j)
            last[j] = total > 0.0 ? last[j] / total : uniform;
    }

    for (int t = T - 2; t >= 0; --t) {
        const double* frame_next = log_frameprob + static_cast<size_t>(t + 1) * K;
        const double* fwd_t = fwd + static_cast<size_t>(t) * K;
        for (int i = 0; i < K; ++i) {
            double maximum = NEG_INF;
            for (int j = 0; j < K; ++j) {
                const double value = log_transmat[static_cast<size_t>(i) * K + j]
                                   + frame_next[j] + bwd_next[static_cast<size_t>(j)];
                work[static_cast<size_t>(j)] = value;
                if (value > maximum) maximum = value;
            }
            if (maximum == NEG_INF) {
                // No reachable successor: the state is dead from here on, and
                // it contributes no transition counts.
                bwd_current[static_cast<size_t>(i)] = NEG_INF;
                continue;
            }
            // exp(work - maximum) serves both the log-sum-exp below and the
            // transition counts, which differ from it by a constant factor.
            //
            // An impossible sequence (log_prob == -inf) contributes no expected
            // transitions at all. Without the guard the scale is
            // exp(-inf + -inf - -inf) = exp(nan) = nan, and since xi_sum is the
            // accumulator shared by every sequence in an E-step, that one
            // sequence turns the whole transition matrix into nan -- and then
            // the M-step, and then every iteration after it. The posteriors
            // survive it (their uniform fallback catches the nan total), which
            // is what makes it invisible. Found in the numba original this was
            // ported from, and fixed there first (chisurf f6e960190).
            double accumulated = 0.0;
            const double scale = (log_prob == NEG_INF)
                                     ? 0.0
                                     : std::exp(maximum + fwd_t[i] - log_prob);
            for (int j = 0; j < K; ++j) {
                const double shifted = std::exp(work[static_cast<size_t>(j)] - maximum);
                accumulated += shifted;
                xi_sum[static_cast<size_t>(i) * K + j] += shifted * scale;
            }
            bwd_current[static_cast<size_t>(i)] = std::log(accumulated) + maximum;
        }

        double* post_t = posteriors + static_cast<size_t>(t) * K;
        double total = 0.0;
        for (int j = 0; j < K; ++j) {
            post_t[j] = std::exp(fwd_t[j] + bwd_current[static_cast<size_t>(j)] - log_prob);
            total += post_t[j];
        }
        for (int j = 0; j < K; ++j)
            post_t[j] = total > 0.0 ? post_t[j] / total : uniform;

        for (int j = 0; j < K; ++j)
            bwd_next[static_cast<size_t>(j)] = bwd_current[static_cast<size_t>(j)];
    }
}

}  // namespace

double hmm_logsumexp(double* values, int n_values) {
    require(values != nullptr || n_values == 0, "values is null");
    return logsumexp(values, n_values);
}

double hmm_forward_log(
        double* log_startprob, int n_startprob,
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        double* fwd, int n_fwd1, int n_fwd2) {
    require(n_samples > 0, "n_samples must be positive");
    check_model(n_startprob, n_trans1, n_trans2, n_states);
    require(n_fwd1 == n_samples && n_fwd2 == n_states,
            "fwd must have the shape of log_frameprob");
    return forward(log_startprob, log_transmat, log_frameprob, n_samples, n_states, fwd);
}

void hmm_backward_log(
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        double* bwd, int n_bwd1, int n_bwd2) {
    require(n_samples > 0, "n_samples must be positive");
    require(n_states > 0, "n_states must be positive");
    require(n_trans1 == n_states && n_trans2 == n_states,
            "log_transmat must be (n_states, n_states)");
    require(n_bwd1 == n_samples && n_bwd2 == n_states,
            "bwd must have the shape of log_frameprob");

    const int K = n_states;
    std::vector<double> work(static_cast<size_t>(K));
    double* last = bwd + static_cast<size_t>(n_samples - 1) * K;
    for (int i = 0; i < K; ++i) last[i] = 0.0;
    for (int t = n_samples - 2; t >= 0; --t) {
        const double* frame_next = log_frameprob + static_cast<size_t>(t + 1) * K;
        const double* bwd_next = bwd + static_cast<size_t>(t + 1) * K;
        double* row = bwd + static_cast<size_t>(t) * K;
        for (int i = 0; i < K; ++i) {
            for (int j = 0; j < K; ++j)
                work[static_cast<size_t>(j)] =
                        log_transmat[static_cast<size_t>(i) * K + j] + frame_next[j] + bwd_next[j];
            row[i] = logsumexp(work.data(), K);
        }
    }
}

void hmm_backward_posteriors_xi(
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        double* fwd, int n_fwd1, int n_fwd2,
        double log_prob,
        double* posteriors, int n_post1, int n_post2,
        double* xi_sum, int n_xi1, int n_xi2) {
    require(n_samples > 0, "n_samples must be positive");
    require(n_states > 0, "n_states must be positive");
    require(n_trans1 == n_states && n_trans2 == n_states,
            "log_transmat must be (n_states, n_states)");
    require(n_fwd1 == n_samples && n_fwd2 == n_states,
            "fwd must have the shape of log_frameprob");
    require(n_post1 == n_samples && n_post2 == n_states,
            "posteriors must have the shape of log_frameprob");
    require(n_xi1 == n_states && n_xi2 == n_states,
            "xi_sum must be (n_states, n_states)");
    backward_posteriors_xi(log_transmat, log_frameprob, fwd, log_prob,
                           n_samples, n_states, posteriors, xi_sum);
}

double hmm_viterbi_log(
        double* log_startprob, int n_startprob,
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        long long* state_sequence, int n_state_sequence) {
    require(n_samples > 0, "n_samples must be positive");
    check_model(n_startprob, n_trans1, n_trans2, n_states);
    require(n_state_sequence == n_samples,
            "state_sequence must have one entry per sample");

    const int T = n_samples, K = n_states;
    std::vector<double> delta(static_cast<size_t>(T) * K);
    std::vector<int> psi(static_cast<size_t>(T) * K, 0);

    for (int j = 0; j < K; ++j)
        delta[static_cast<size_t>(j)] = log_startprob[j] + log_frameprob[j];
    for (int t = 1; t < T; ++t) {
        const double* prev = delta.data() + static_cast<size_t>(t - 1) * K;
        const double* frame = log_frameprob + static_cast<size_t>(t) * K;
        for (int j = 0; j < K; ++j) {
            double best = NEG_INF;
            int best_i = 0;
            for (int i = 0; i < K; ++i) {
                const double score = prev[i] + log_transmat[static_cast<size_t>(i) * K + j];
                // Strict >, so a tie keeps the lowest state index.
                if (score > best) { best = score; best_i = i; }
            }
            delta[static_cast<size_t>(t) * K + j] = best + frame[j];
            psi[static_cast<size_t>(t) * K + j] = best_i;
        }
    }
    double best = NEG_INF;
    int best_i = 0;
    for (int j = 0; j < K; ++j) {
        const double d = delta[static_cast<size_t>(T - 1) * K + j];
        if (d > best) { best = d; best_i = j; }
    }
    state_sequence[T - 1] = best_i;
    for (int t = T - 2; t >= 0; --t)
        state_sequence[t] = psi[static_cast<size_t>(t + 1) * K
                               + static_cast<size_t>(state_sequence[t + 1])];
    return best;
}

double hmm_estep_log(
        double* log_startprob, int n_startprob,
        double* log_transmat, int n_trans1, int n_trans2,
        double* log_frameprob, int n_samples, int n_states,
        long long* lengths, int n_lengths,
        double* fwd, int n_fwd1, int n_fwd2,
        double* posteriors, int n_post1, int n_post2,
        double* xi_sum, int n_xi1, int n_xi2,
        double* log_prob_per_seq, int n_log_prob_per_seq) {
    require(n_samples > 0, "n_samples must be positive");
    check_model(n_startprob, n_trans1, n_trans2, n_states);
    require(n_fwd1 == n_samples && n_fwd2 == n_states,
            "fwd must have the shape of log_frameprob");
    require(n_post1 == n_samples && n_post2 == n_states,
            "posteriors must have the shape of log_frameprob");
    require(n_xi1 == n_states && n_xi2 == n_states,
            "xi_sum must be (n_states, n_states)");

    // A null `lengths` is one sequence. Spelling that here keeps every caller
    // that has only one sequence from building a one-element array.
    std::vector<long long> owned;
    if (lengths == nullptr || n_lengths <= 0) {
        owned.push_back(static_cast<long long>(n_samples));
        lengths = owned.data();
        n_lengths = 1;
    }
    long long total = 0;
    for (int s = 0; s < n_lengths; ++s) {
        require(lengths[s] > 0, "every sequence length must be positive");
        total += lengths[s];
    }
    require(total == static_cast<long long>(n_samples),
            "the sequence lengths do not sum to the number of rows in log_frameprob");
    // An empty array means "do not report the per-sequence values". A binding
    // whose array typemap cannot pass a null pointer needs some way to say
    // that, and a zero-length array is the one it always has.
    const bool want_per_seq = log_prob_per_seq != nullptr && n_log_prob_per_seq > 0;
    require(!want_per_seq || n_log_prob_per_seq == n_lengths,
            "log_prob_per_seq must have one entry per sequence");

    const int K = n_states;
    double log_prob_total = 0.0;
    size_t offset = 0;
    for (int s = 0; s < n_lengths; ++s) {
        const int T = static_cast<int>(lengths[s]);
        const double* frame = log_frameprob + offset * K;
        double* fwd_s = fwd + offset * K;
        double* post_s = posteriors + offset * K;

        const double log_prob = forward(log_startprob, log_transmat, frame, T, K, fwd_s);
        backward_posteriors_xi(log_transmat, frame, fwd_s, log_prob, T, K, post_s, xi_sum);

        if (want_per_seq) log_prob_per_seq[s] = log_prob;
        log_prob_total += log_prob;
        offset += static_cast<size_t>(T);
    }
    return log_prob_total;
}

}  // namespace tttrlib
