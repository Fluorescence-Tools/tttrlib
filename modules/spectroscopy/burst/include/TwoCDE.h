// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_TWOCDE_H
#define TTTRLIB_TWOCDE_H

#include <vector>
#include <memory>
#include <utility>

#include "BurstFeature.h"

namespace tttrlib {

/**
 * @brief Two-Channel Kernel Density Estimator dynamics features (2CDE).
 *
 * 2CDE (Tomov et al., Biophys. J. 2012, doi:10.1016/j.bpj.2011.11.4025) is a
 * per-burst *feature* that quantifies within-burst kinetics without a kinetic
 * model, from per-photon kernel-density estimates of two photon streams.  This
 * class only *computes* the feature; selecting/filtering bursts on the returned
 * values happens downstream.  Two variants are provided:
 *
 *  - **FRET-2CDE** flags fluctuating FRET efficiency:
 *    @f$ 110 - 100\,[(E)_D + (1-E)_A] @f$, @f$ \approx 10 @f$ for static bursts,
 *    rising (30..100) under ms dynamics.  Streams: donor (DexDem) and acceptor
 *    (DexAem).  The Laplace kernel applies the Tomov nbKDE self-correction
 *    @f$ (1 + 2/N)(\mathrm{KDE}-1) @f$; the Gaussian kernel uses the raw KDE.
 *
 *  - **ALEX-2CDE** flags acceptor blinking / donor-only or acceptor-only
 *    contamination in ALEX/PIE data:
 *    @f$ 100 - 50\,[BR_{Dex} - BR_{Aex}] @f$.  Streams: donor-excitation
 *    (DexDem+DexAem) and acceptor-excitation (AexAem).  Uses the raw KDE.
 *
 * This is a bit-exact C++ port of the reference FRETBursts implementation:
 * KDEs are computed on the full photon stream (avoiding burst-edge artefacts)
 * with an ascending-index two-pointer sliding window (@f$ 5\tau @f$ Laplace,
 * @f$ 3\tau @f$ Gaussian), then sliced per burst.  Both the global KDE and the
 * per-burst reduction are parallelised (see tttrlib::BurstFeature).
 */
class TwoCDE : public BurstFeature {
public:
    /// Which 2CDE quantity to compute.
    enum Variant {
        FRET_2CDE = 0,  ///< donor/acceptor FRET dynamics
        ALEX_2CDE = 1   ///< ALEX/PIE excitation-scheme dynamics (acceptor blinking)
    };

    explicit TwoCDE(std::shared_ptr<TTTR> tttr) : BurstFeature(std::move(tttr)) {}
    explicit TwoCDE(std::shared_ptr<BurstFilter> burst_filter)
        : BurstFeature(std::move(burst_filter)) {}

    /// FRET-2CDE donor stream (DexDem). Convenience wrapper over ::set_stream.
    void set_donor(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    ) { set_stream(DONOR, channels, micro_time_ranges); }

    /// FRET-2CDE acceptor stream (DexAem).
    void set_acceptor(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    ) { set_stream(ACCEPTOR, channels, micro_time_ranges); }

    /// ALEX-2CDE donor-excitation stream (all donor-excitation photons, DexDem+DexAem).
    void set_donor_excitation(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    ) { set_stream(DONOR_EXC, channels, micro_time_ranges); }

    /// ALEX-2CDE acceptor-excitation stream (AexAem).
    void set_acceptor_excitation(
        const std::vector<int>& channels,
        const std::vector<std::pair<int, int>>& micro_time_ranges = {}
    ) { set_stream(ACCEPTOR_EXC, channels, micro_time_ranges); }

    /**
     * @brief Compute the per-burst 2CDE feature value into ::get_result.
     * @param bursts Interleaved inclusive index ranges ``[s0, e0, s1, e1, ...]``
     *        as an (n_bursts, 2) array (as produced by every burst search).
     * @param tau Kernel time constant in **seconds** (converted to macro-time
     *        ticks via the header resolution).
     * @param variant ::FRET_2CDE or ::ALEX_2CDE.
     * @param kernel ::LAPLACE (Tomov original) or ::GAUSSIAN (smooth variant).
     *
     * Bursts lacking photons in a required stream yield NaN.
     */
    void compute(
        long long* bursts, int n_bursts, int n_cols,
        double tau = 100e-6,
        int variant = FRET_2CDE,
        int kernel = LAPLACE
    );

    /// Compute using the bursts of the source BurstFilter (throws if none).
    void compute(
        double tau = 100e-6,
        int variant = FRET_2CDE,
        int kernel = LAPLACE
    );

    /// Per-burst 2CDE value (alias of ::get_result).
    const std::vector<double>& get_two_cde() const { return get_result(); }

private:
    // Internal stream keys for the base's named-stream map (role identifiers,
    // not user-facing — streams are configured through the setters above).
    static constexpr const char* DONOR = "donor";
    static constexpr const char* ACCEPTOR = "acceptor";
    static constexpr const char* DONOR_EXC = "donor_excitation";
    static constexpr const char* ACCEPTOR_EXC = "acceptor_excitation";

    std::function<double(int, int64_t, int64_t)> make_reducer(int variant, int kernel) const;
};

} // namespace tttrlib

#endif // TTTRLIB_TWOCDE_H
