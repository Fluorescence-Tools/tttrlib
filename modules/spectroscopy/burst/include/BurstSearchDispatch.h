// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_BURST_SEARCH_DISPATCH_H
#define TTTRLIB_BURST_SEARCH_DISPATCH_H

#include <functional>
#include <string>
#include <vector>

#include "AlgorithmRegistry.h"

class TTTR;

/// PRD-027 criterion 6 — `TTTR::burst_search` dispatches on a name through a
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
 * PRD-032. The name-only overload above registers dispatch and nothing else, so
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
/// PRD-032 criterion 4 asks for. A plugin's search is memoised into the table
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

} // namespace tttrlib

#endif // TTTRLIB_BURST_SEARCH_DISPATCH_H
