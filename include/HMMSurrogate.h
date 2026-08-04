// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_HMMSURROGATE_H
#define TTTRLIB_HMMSURROGATE_H

#include <string>
#include <vector>

#include "HMM.h"
#include "NeuralNet.h"

namespace tttrlib {

/**
 * @brief Amortised ("surrogate") neural estimator for H2MM.
 *
 * Instead of iterating Baum-Welch EM to the maximum-likelihood estimate, a
 * network is trained **once** on data drawn from the HMM generative model and
 * then estimates the parameters of a real dataset in a **single forward pass**
 * — the simulation-based / amortised-inference idea.
 *
 * Why a *replacement* for EM rather than an initialiser: seeding EM near the
 * optimum does not cut its iteration count much, because EM spends its effort
 * on the finite-sample "last mile" to *this dataset's* MLE.  The surrogate
 * therefore earns its speed by returning an estimate directly, optionally
 * polished by a few EM maps.
 *
 * The estimate is **approximate** — like fitting on a subsample, it trades a
 * little statistical precision for a large speed-up, which is safe when the
 * data over-determine the model.  A trained surrogate is specific to a
 * ``(n_states, n_streams)`` pair and to the burst-length / inter-photon-@f$\Delta t@f$
 * regime it was trained on.
 *
 * This class is a thin adapter: it owns the feature extractor and the output
 * decoder, while the network itself is the domain-agnostic :class:`NeuralNet`.
 * Models interoperate with the scikit-learn implementation in ChiSurf through a
 * shared JSON format.
 */
class HmmSurrogate {
public:
    /// Feature-layout version; bumped when :func:`extract_features` changes so a
    /// stale cached model is rejected rather than silently mis-fed.
    static const int FEATURES_VERSION = 1;
    /// Length of the vector produced by :func:`extract_features`.
    static const int N_FEATURES = 24;

    HmmSurrogate() = default;
    HmmSurrogate(NeuralNet net, int n_states, int n_streams,
                  int features_version = FEATURES_VERSION);

    // --- serialisation ----------------------------------------------------
    /// Parse a ``tttrlib.hmm_surrogate`` JSON document.
    static HmmSurrogate from_json_string(const std::string& json);
    /// Read a ``tttrlib.hmm_surrogate`` JSON file.
    static HmmSurrogate from_json_file(const std::string& path);
    /// Serialise to JSON; ``indent < 0`` emits the compact form.
    std::string to_json_string(int indent = -1) const;
    /// Write the JSON document to ``path``.
    void to_json_file(const std::string& path, int indent = 2) const;

    // --- estimation -------------------------------------------------------
    /**
     * @brief Fixed-length, permutation-invariant summary of a dataset.
     *
     * Summarises the emission structure (windowed local-FRET histogram and
     * quantiles), the kinetics (photon-lag autocorrelation of the per-photon
     * FRET signal), and the inter-photon timing — everything an amortised
     * estimator needs to recover @f$(\pi, A, B)@f$ without seeing the raw
     * sequence.  Independent of burst order and burst count.
     *
     * Numerically identical to the ChiSurf/NumPy implementation: the histogram
     * is density-normalised over in-range samples, quantiles use linear
     * interpolation on the sorted values, and the @f$\Delta t@f$ spread is a
     * population standard deviation.
     */
    static std::vector<double> extract_features(const HMM& data);

    /**
     * @brief Estimate an HMM model from ``data`` in a single forward pass.
     * @throws std::runtime_error if ``data`` has a different stream count.
     */
    HmmModel predict(const HMM& data) const;

    // --- training ---------------------------------------------------------
    /**
     * @brief Simulate labelled datasets and fit the amortised estimator.
     *
     * Draws ``n_samples`` random models over a realistic FRET/kinetics range,
     * simulates a burst dataset from each, and regresses the model parameters
     * on the extracted features.
     *
     * @param n_states Hidden-state count the surrogate is specialised to.
     * @param n_streams Photon-stream (detector) count.
     * @param n_samples Number of simulated datasets to train on.
     * @param n_bursts Bursts per simulated dataset.
     * @param burst_len Photons per simulated burst.
     * @param mean_dt Mean inter-photon gap in macro-time ticks.
     * @param options Network hyper-parameters.
     * @param seed Seed for the simulation and the network initialisation.
     */
    static HmmSurrogate train(
        int n_states, int n_streams,
        int n_samples = 2500,
        int n_bursts = 150,
        int burst_len = 80,
        double mean_dt = 4.0,
        const TrainOptions& options = TrainOptions(),
        int seed = 0
    );

    /**
     * @brief Generate a labelled training set without fitting anything.
     *
     * Exposed so callers can inspect, cache, or extend the simulated data — and
     * so the C++ generator can be checked against the Python one.  Fills
     * ``X`` (``n_samples x N_FEATURES``) and ``Y`` (``n_samples x n_targets``),
     * both row-major.
     */
    static void generate_training_set(
        int n_states, int n_streams, int n_samples,
        int n_bursts, int burst_len, double mean_dt, int seed,
        std::vector<double>& X, std::vector<double>& Y
    );

    /// Length of the regression target: ``n*p + n*(n-1) + n``.
    static int n_targets(int n_states, int n_streams);

    /// Flatten a model to the regression target vector (canonical state order).
    static std::vector<double> encode(const HmmModel& model);
    /// Rebuild a valid model from a (possibly noisy) target vector.
    static HmmModel decode(const std::vector<double>& vec, int n_states, int n_streams);

    // --- introspection ----------------------------------------------------
    const NeuralNet& get_net() const { return net_; }
    int get_n_states() const { return n_states_; }
    int get_n_streams() const { return n_streams_; }
    int get_features_version() const { return features_version_; }

private:
    NeuralNet net_;
    int n_states_ = 0;
    int n_streams_ = 0;
    int features_version_ = FEATURES_VERSION;
};

} // namespace tttrlib

#endif // TTTRLIB_HMMSURROGATE_H
