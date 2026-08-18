// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BURST_SEARCH_DISPATCH_H
#define TTTRLIB_BURST_SEARCH_DISPATCH_H

// Validation: A/B-TESTED 2026-08-17 -- the dispatched sliding window vs FRETBursts bsearch (compiled and
//   pure Python, live, bit-identical) and a NumPy transcription; the dual-channel
//   composition burst_search_coincident vs FRETBursts Bursts.and_gate (identical after
//   merging overlapping gate output); CUSUM/SPRT vs Zhang & Yang 2005 in NumPy (exact)
//   and PAM's discretised search in Octave (every burst matched, Jaccard >= 0.85). test/python/burstfilter/test_ab_burst_reference.py.
//   Register: okf/testing/algorithm-validation.md

#include <functional>
#include <string>
#include <vector>

#include "AlgorithmRegistry.h"

class TTTR;

/// `TTTR::burst_search` dispatches on a name through a
/// table, not through a chain of `if (mode == "...")`.
///
/// The chain was not merely inelegant. It lived inside the one function every
/// burst search has to be reachable from, so adding a search meant editing that
/// function — and a search contributed from anywhere else could not be reached
/// by name at all, however completely it was implemented and registered. The
/// table moves the decision to load time: a search declares itself, and the
/// dispatcher never learns its name.
///
/// The signature is the narrow `(L, m, T, alpha, beta)` entry point, which is
/// what `TTTR::burst_search` publishes in three languages. A search with
/// parameters that do not fit it keeps its own full-fidelity entry point and
/// reinterprets what it can here — as `kalman`, `maxtree` and
/// `bayesian_blocks` already do with `T`. That reinterpretation is a property
/// of this narrow signature, not of the algorithms, and is documented per
/// registration rather than in a comment inside a branch nobody reads.

namespace tttrlib {

/// A burst search reachable by name from `TTTR::burst_search`.
/// Returns flat [start, stop, start, stop, ...] photon indices, stop inclusive,
/// as every burst search in this library does.
using BurstSearchFn = std::function<std::vector<long long>(
        TTTR& tttr, int L, int m, double T, double alpha, double beta)>;

/*!
 * \brief Make a burst search reachable by name.
 * \return false if the name is empty, the function is empty, or the name is
 *         already registered. A duplicate is rejected rather than replaced:
 *         which of two searches answers to one name should not depend on which
 *         translation unit initialised first.
 */
bool register_burst_search(const std::string& name, BurstSearchFn fn);

/*!
 * \brief Declare a burst search once: what it is, and the function that runs it.
 *
 * The name-only overload above registers dispatch and nothing else, so
 * a search registered through it runs but is invisible to the registry — which
 * is how the built-ins came to be described in one file and dispatched from
 * another, and how two of them ended up advertising a `method` the dispatcher
 * had never heard of.
 *
 * This overload takes both. `desc.operation_type` is the dispatch name, so the
 * two cannot disagree; `desc.capability` is forced to `burst_search`.
 *
 * \return false if the descriptor is unusable or either registration is a
 *         duplicate. Registration is all-or-nothing: a search that is described
 *         but not dispatchable is exactly the failure this replaces.
 */
bool register_burst_search(const AlgorithmDescriptor& desc, BurstSearchFn fn);

/// The registered search, or null. Registers the built-ins on first call, and
/// falls back to the plugin host — a search a plugin contributed is reachable
/// by name through `TTTR::burst_search` like any other, which is what
/// the registry requires. A plugin's search is memoised into the table
/// on first lookup.
const BurstSearchFn* find_burst_search(const std::string& name);

/// Every registered name, in registration order.
std::vector<std::string> burst_search_dispatch_names();

/// Register the searches compiled into this library. Explicit rather than a
/// static initialiser, which a static-archive link drops — the same lesson as
/// `DecayFitModelRegistration.h`. Idempotent.
///
/// Defined in `BurstSearchRegistry.cpp`, beside the descriptions, because that
/// is where each search now declares itself.
void register_builtin_burst_searches();

/// Register the burst module's pipeline-operation registry entries
/// (`burst_selection`, `bva`, `kde_cde`, `burst_fusion`) -- each declared next
/// to the code that performs it. Idempotent.
void register_burst_operations();

} // namespace tttrlib

#endif // TTTRLIB_BURST_SEARCH_DISPATCH_H
