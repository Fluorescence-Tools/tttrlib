// SPDX-License-Identifier: BSD-3-Clause
//
// The burst-search entry points, which are declared as TTTR methods and
// implemented here.
//
// They are members because TTTR.h publishes the burst-search API as its own
// methods, and that is the public interface in three languages -- changing it
// to free functions would break every caller for a tidiness nobody asked for.
// What was wrong was not the declaration but the location: core contained the
// implementations, so "find a burst" and "read a photon file" were the same
// translation unit and the same library.
//
// Defining them here instead costs nothing. A member function may be defined in
// any translation unit that sees the class, private access included, and `burst`
// already depends on `core`. The result is that core no longer contains a line
// of burst-search code, and anything that wants one links `burst` -- which is
// what the dependency was supposed to say all along.
#include "TTTR.h"

#include "BurstSearchDispatch.h"
#include "BurstSearchBayesianBlocks.h"
#include "BurstSearchKalman.h"
#include "BurstSearchMaxTree.h"
#include "Verbose.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

std::vector<long long> TTTR::burst_search(
    int L, int m, double T, 
    const std::string& mode,
    double alpha, 
    double beta
) {
    // Table lookup, not a chain of `if (mode == "...")`. The chain lived inside
    // the one function every burst search has to be reachable from, so adding a
    // search meant editing this function -- and a search contributed from
    // anywhere else could not be reached by name at all, however completely it
    // was implemented. See BurstSearchDispatch.h.
    if (const tttrlib::BurstSearchFn* fn = tttrlib::find_burst_search(mode))
        return (*fn)(*this, L, m, T, alpha, beta);
    // An unrecognised mode has always run the sliding window rather than
    // failing, and callers rely on that; the name is not validated here.
    return burst_search_sliding_window(L, m, T);
}

std::vector<long long> TTTR::burst_search_sliding_window(int L, int m, double T)
{
    if (static_cast<int64_t>(size()) < m) {
        return {};
    }

    bool in_burst = false;
    int64_t i_start = 0, i_stop = 0;
    const long long Ti = static_cast<long long>(T / header->get_macro_time_resolution());

    std::vector<long long> bursts;
    bursts.reserve(1000);

    int64_t n_events = static_cast<int64_t>(size());
    for (int64_t i = 0; i <= n_events - m; ++i) {
        if (get_macro_time_at(i + m - 1) - get_macro_time_at(i) <= static_cast<unsigned long long>(Ti)) {
            if (!in_burst) {
                in_burst = true;
                i_start = i;
            }
        }
        else {
            if (in_burst) {
                in_burst = false;
                i_stop = i + m - 2;
                if (i_stop - i_start + 1 >= L) {
                    bursts.push_back(i_start);
                    bursts.push_back(i_stop);
                }
            }
        }
    }

    if (in_burst) {
        i_stop = static_cast<int64_t>(size()) - 1;
        if (i_stop - i_start + 1 >= L) {
            bursts.push_back(i_start);
            bursts.push_back(i_stop);
        }
    }

    return bursts;
}

std::vector<long long> TTTR::burst_search_cusum_sprt(
    int min_photons, 
    double background_cps, 
    double signal_to_background_ratio,
    double alpha, 
    double beta
) {
    size_t N = size();
    if (N == 0) {
        return {};
    }

    double IB, I0, I1;
    double macro_res_ms = header->get_macro_time_resolution() * 1000.0;
    
    // Auto-estimate background if not provided (m=0)
    if (background_cps <= 0) {
        // Estimate background from data using binning
        const int BIN_SIZE = 100;
        std::vector<double> rates;
        double bin_t = 0.0;
        int bin_count = 0;
        
        for (size_t i = 1; i < N; ++i) {
            double dt = (get_macro_time_at(i) - get_macro_time_at(i-1)) * macro_res_ms;
            bin_t += dt;
            bin_count++;
            
            if (bin_count >= BIN_SIZE) {
                if (bin_t > 0) {
                    rates.push_back(bin_count / bin_t);  // counts per ms
                }
                bin_t = 0.0;
                bin_count = 0;
            }
        }
        
        // Use median as background estimate (robust to bursts)
        if (!rates.empty()) {
            std::sort(rates.begin(), rates.end());
            IB = rates[rates.size() / 2];
            background_cps = IB * 1000.0;  // Convert back to cps
        } else {
            IB = 0.001;  // Fallback: 1 count/s
            background_cps = 1.0;
        }
    } else {
        IB = background_cps / 1000.0;
    }
    
    // Auto-estimate S/B ratio if not provided (T=0)
    if (signal_to_background_ratio <= 0) {
        const int BIN_SIZE = 30;
        I0 = 0.0;
        double bin_t = 0.0;
        
        for (size_t i = 1; i <= N; ++i) {
            if (i > 1) {
                double dt = (get_macro_time_at(i) - get_macro_time_at(i-1)) * macro_res_ms;
                bin_t += dt;
            }
            if (i % BIN_SIZE == 0) {
                double tmp = BIN_SIZE / bin_t;
                if (tmp > I0) I0 = tmp;
                bin_t = 0.0;
            }
        }
        I1 = I0 / exp(2.0) + IB;
        signal_to_background_ratio = (I0 + IB) / IB;
    } else {
        // Signal (in-burst) intensity hypothesis for the SPRT: total rate during a burst is
        // background + excess signal = (S/B) * IB. (The earlier `I0/exp(2) + IB` left I1 ~ IB,
        // so the SPRT could not discriminate signal from background — bursts were missed and the
        // S/B dependence was inverted.)
        I0 = (signal_to_background_ratio - 1.0) * IB;
        I1 = I0 + IB;
    }
    
    double KL_disc = (IB - I1) / I1 + log(I1 / IB);
    double A = (1.0 - beta) / alpha;
    double B = beta / (1.0 - alpha);
    double hA = log(A) / (I1 - IB);
    double hB = log(B) / (I1 - IB);
    double hC = log(I1 / IB) / (I1 - IB);
    
    double tmp = alpha / 3.0 / (KL_disc + 1.0) / (KL_disc + 1.0) * log(1.0 / alpha);
    double h = -log(tmp);
    double Sa = log(I1 / IB);
    double Sb = (I1 - IB);
    size_t nd = static_cast<size_t>(std::round(log(1.0 / alpha) / KL_disc));
    
    std::vector<double> dt(N);
    dt[0] = 0.0;
    for (size_t i = 1; i < N; ++i) {
        dt[i] = (get_macro_time_at(i) - get_macro_time_at(i-1)) * macro_res_ms;
    }
    
    auto cusum = [&](size_t i, size_t f) -> size_t {
        int dj = (i < f) ? 1 : -1;
        size_t j = i;
        size_t k = f;
        double Sn = 0.0;
        
        while (j != f) {
            Sn += Sa - dt[j] * Sb;
            if (Sn < 0) {
                Sn = 0.0;
            } else if (Sn >= h) {
                k = j;
                break;
            }
            j += dj;
        }
        return k;
    };
    
    auto sprt = [&](size_t i, size_t N_max) -> size_t {
        size_t j = i;
        size_t f = N_max;
        size_t n = 0;
        double Sn = 0.0;
        
        while (j < N_max) {
            Sn += dt[j];
            n++;
            if (Sn <= (static_cast<double>(n) * hC - hA)) {
                Sn = 0.0;
                n = 0;
            } else if (Sn > (static_cast<double>(n) * hC - hB)) {
                f = j;
                break;
            }
            j++;
        }
        return f;
    };
    
    std::vector<long long> bursts;
    bursts.reserve(1000);
    
    size_t kl = cusum(1, N);
    
    while (kl < N) {
        size_t krp = sprt(kl, N);
        if (krp >= N) break;
        
        size_t kl1 = cusum(krp, N);
        if (kl1 >= N) break;
        
        size_t kr = (kl1 > nd) ? cusum(kl1 - nd, kl) : cusum(0, kl);
        
        if (kr >= kl) {
            int64_t burst_size = static_cast<int64_t>(kr - kl + 1);
            if (burst_size >= min_photons) {
                bursts.push_back(static_cast<long long>(kl));
                bursts.push_back(static_cast<long long>(kr));
            }
        }
        
        kl = kl1;
    }
    
    return bursts;
}
