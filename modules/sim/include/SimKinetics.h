#ifndef TTTRLIB_SIMKINETICS_H
#define TTTRLIB_SIMKINETICS_H

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "SimRandom.h"

/*!
 * \brief Continuous-time Markov kinetics, without a photon in sight.
 *
 * The simulator already walks a molecule's state between transitions -- that is
 * what `SimEngine::evolve_spontaneous` does outside the focus. But the only way
 * to *reach* that walk from outside was to build a whole photon simulation
 * around it: dark species, immobile molecules, a focus nobody looks through and
 * a box nothing moves in, run for one window, then read the state log. That
 * works, and it is a lot of scaffolding standing between a caller and a Markov
 * chain.
 *
 * A burst analysis needs the chain on its own, repeatedly: how long a molecule
 * spends in each conformational state during an observation window is what a
 * dynamic FRET histogram is made of, and it is a question about kinetics rather
 * than about photons.
 *
 * Rates are row-major **source to target**, matching `SimSystem::set_rate_matrices`,
 * and are per unit of whatever time `window` is measured in. The diagonal is
 * ignored.
 */
namespace tttrlib {
namespace SimKinetics {

/*!
 * \brief Occupation-time fractions of an n-state chain over one window.
 *
 * SWIG flattens the namespace, so this carries the Sim prefix the rest of the
 * subsystem uses rather than claiming a bare `occupation_fractions` in tttrlib.
 *
 * Draws `n_samples` independent windows. Each starts in a state drawn from `p0`
 * and is advanced by the standard Gillespie construction -- an exponential hold
 * whose rate is the state's total exit rate, then a target chosen in proportion
 * to that state's outgoing rates -- accumulating the time spent in each state.
 * The final, truncated sojourn counts: a window that never leaves its starting
 * state has spent all of it there, which is exactly the slow-exchange limit a
 * two-moment approximation cannot represent.
 *
 * @param k row-major source->target rates, `n_states * n_states`; diagonal ignored.
 * @param n_states number of states.
 * @param window observation window, in the reciprocal of the rate unit.
 * @param n_samples independent windows to draw.
 * @param p0 initial state distribution; empty means uniform.
 * @param seed RNG seed.
 * @return `n_samples * n_states` fractions, row-major, each row summing to one.
 */
inline std::vector<double> sim_occupation_fractions(
    const std::vector<double>& k,
    int n_states,
    double window,
    int n_samples,
    const std::vector<double>& p0,
    uint64_t seed)
{
    if (n_states < 1)
        throw std::invalid_argument("sim_occupation_fractions: n_states must be positive");
    if (int(k.size()) != n_states * n_states)
        throw std::invalid_argument(
            "sim_occupation_fractions: rate matrix must have n_states * n_states entries");
    if (!(window > 0.0))
        throw std::invalid_argument("sim_occupation_fractions: window must be positive");
    if (n_samples < 0)
        throw std::invalid_argument("sim_occupation_fractions: n_samples must not be negative");
    if (!p0.empty() && int(p0.size()) != n_states)
        throw std::invalid_argument(
            "sim_occupation_fractions: p0 must have one entry per state");

    // Total exit rate per state, with the diagonal excluded.
    std::vector<double> exit(n_states, 0.0);
    for (int i = 0; i < n_states; ++i) {
        double s = 0.0;
        for (int j = 0; j < n_states; ++j)
            if (j != i) s += (k[size_t(i) * n_states + j] > 0.0)
                                ? k[size_t(i) * n_states + j] : 0.0;
        exit[i] = s;
    }

    // Cumulative initial distribution.
    std::vector<double> start(n_states, 0.0);
    double total = 0.0;
    for (int i = 0; i < n_states; ++i) {
        const double w = p0.empty() ? 1.0 : (p0[i] > 0.0 ? p0[i] : 0.0);
        total += w;
        start[i] = total;
    }
    if (!(total > 0.0))
        throw std::invalid_argument("sim_occupation_fractions: p0 has no positive weight");

    tttrlib::SimRandom rng{uint32_t(seed)};
    std::vector<double> out(size_t(n_samples) * n_states, 0.0);

    for (int s = 0; s < n_samples; ++s) {
        double* row = out.data() + size_t(s) * n_states;

        const double u = rng.random0i1e() * total;
        int state = n_states - 1;
        for (int i = 0; i < n_states; ++i)
            if (u < start[i]) { state = i; break; }

        double t = 0.0;
        for (;;) {
            const double rate = exit[state];
            if (!(rate > 0.0)) {          // absorbing: the rest of the window is here
                row[state] += window - t;
                break;
            }
            const double hold = -std::log(rng.random0e1e()) / rate;
            if (t + hold >= window) {     // the truncated final sojourn still counts
                row[state] += window - t;
                break;
            }
            row[state] += hold;
            t += hold;

            double r = rng.random0i1e() * rate;
            int target = state;
            for (int j = 0; j < n_states; ++j) {
                if (j == state) continue;
                const double kij = k[size_t(state) * n_states + j];
                if (kij <= 0.0) continue;
                r -= kij;
                if (r <= 0.0) { target = j; break; }
                target = j;
            }
            state = target;
        }

        for (int i = 0; i < n_states; ++i) row[i] /= window;
    }
    return out;
}

/*!
 * \brief State of the chain at each of a set of observation times.
 *
 * The companion to \ref sim_occupation_fractions, for when the question is not
 * how long a window spent in each state but *which state it was in* at the
 * moments something was observed -- a photon arriving, say. The chain is walked
 * by the same Gillespie construction and read off at each time.
 *
 * Times must be ascending and are absolute, not gaps. A photon-by-photon
 * analysis has them already.
 *
 * @param k row-major source->target rates, `n_states * n_states`; diagonal ignored.
 * @param n_states number of states.
 * @param times ascending observation times, in the reciprocal of the rate unit.
 * @param p0 initial state distribution; empty means uniform.
 * @param seed RNG seed.
 * @return one state index per entry of `times`.
 */
inline std::vector<int> sim_state_at_times(
    const std::vector<double>& k,
    int n_states,
    const std::vector<double>& times,
    const std::vector<double>& p0,
    uint64_t seed)
{
    if (n_states < 1)
        throw std::invalid_argument("sim_state_at_times: n_states must be positive");
    if (int(k.size()) != n_states * n_states)
        throw std::invalid_argument(
            "sim_state_at_times: rate matrix must have n_states * n_states entries");
    if (!p0.empty() && int(p0.size()) != n_states)
        throw std::invalid_argument(
            "sim_state_at_times: p0 must have one entry per state");

    std::vector<double> exit(n_states, 0.0);
    for (int i = 0; i < n_states; ++i) {
        double s = 0.0;
        for (int j = 0; j < n_states; ++j)
            if (j != i && k[size_t(i) * n_states + j] > 0.0)
                s += k[size_t(i) * n_states + j];
        exit[i] = s;
    }

    std::vector<double> start(n_states, 0.0);
    double total = 0.0;
    for (int i = 0; i < n_states; ++i) {
        total += p0.empty() ? 1.0 : (p0[i] > 0.0 ? p0[i] : 0.0);
        start[i] = total;
    }
    if (!(total > 0.0))
        throw std::invalid_argument("sim_state_at_times: p0 has no positive weight");

    tttrlib::SimRandom rng{uint32_t(seed)};
    const double u = rng.random0i1e() * total;
    int state = n_states - 1;
    for (int i = 0; i < n_states; ++i)
        if (u < start[i]) { state = i; break; }

    std::vector<int> out;
    out.reserve(times.size());
    double clock = 0.0, next = 0.0;
    bool armed = false;
    for (double t : times) {
        for (;;) {
            const double rate = exit[state];
            if (!(rate > 0.0)) break;
            if (!armed) {
                next = clock - std::log(rng.random0e1e()) / rate;
                armed = true;
            }
            if (next >= t) break;
            clock = next;
            armed = false;
            double r = rng.random0i1e() * rate;
            int target = state;
            for (int j = 0; j < n_states; ++j) {
                if (j == state) continue;
                const double kij = k[size_t(state) * n_states + j];
                if (kij <= 0.0) continue;
                r -= kij;
                if (r <= 0.0) { target = j; break; }
                target = j;
            }
            state = target;
        }
        out.push_back(state);
    }
    return out;
}

}  // namespace SimKinetics
}  // namespace tttrlib

#endif  // TTTRLIB_SIMKINETICS_H
