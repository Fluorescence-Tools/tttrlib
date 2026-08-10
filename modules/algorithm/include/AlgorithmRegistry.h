// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_ALGORITHM_REGISTRY_H
#define TTTRLIB_ALGORITHM_REGISTRY_H

#include <string>
#include <vector>

/// PRD-027 Part 1 — one descriptor and one registration call for every
/// algorithm, whether it is compiled into the core library or arrives in a
/// plugin.
///
/// The problem this solves first: FCS, HMM and PDA *work*, and are invisible.
/// The registry does not list them, so a UI cannot offer them, the `.pto`
/// provenance system has no schema to validate or replay them against, and no
/// plugin can contribute a competing implementation. They were invisible for
/// the same reason burst searches and decay fits were visible — someone wrote a
/// JSON literal for those two by hand, and nobody wrote one for these.
///
/// So the descriptor is the algorithm's own declaration, made where the
/// algorithm lives, and the registry is assembled from what actually
/// registered rather than from a literal that has to be kept in sync.

namespace tttrlib {

/*!
 * \brief What an algorithm declares about itself.
 *
 * Every field except `impl` is data the registry serves to consumers: a UI
 * renders `display_name`, `summary`, `description` and `references_json`; a
 * form builder renders `settings_schema`; the provenance system reads
 * `operation_type`, `row_grain`, `inputs_json`, `outputs_json` and
 * `can_replay`.
 *
 * `impl` is opaque to the host. A capability registrar (PRD-027 Part 2) knows
 * how to turn it into the C++ object or call for its capability — which is the
 * seam that lets a C ABI plugin table and a C++ built-in factory travel one
 * registration path.
 */
struct AlgorithmDescriptor {
    // -- identity (mmfdb / flrCIF canonical names) --
    std::string operation_type;   ///< unique key, e.g. "fcs_correlation"
    std::string display_name;     ///< human label
    std::string summary;          ///< one line, for lists and tooltips

    // -- human documentation --
    /// Full prose: what it does, when it is the right choice, what it assumes
    /// and where it stops being valid. This is what a user reads instead of the
    /// source when deciding whether the algorithm suits their data.
    std::string description;
    /// JSON array of citation objects, so a user knows what to cite. Fields:
    /// type, authors, title, year, and where applicable journal, volume,
    /// pages, doi, url.
    std::string references_json;

    // -- classification --
    std::string capability;       ///< "burst_search", "decay_fit", "fcs", ...

    /// The method a caller invokes to run this, when one exists as an attribute
    /// on the object -- emitted as `method`, which is the key the burst_search
    /// and fit categories have always carried and which a UI dispatches on. An
    /// algorithm reachable only by name (a plugin's) leaves this empty, and
    /// *that absence is the signal* those consumers already read.
    std::string dispatch_name;
    /// "builtin" unless a plugin supplied it. Emitted as `provider`.
    std::string provider = "builtin";

    // -- contract --
    std::string settings_schema;  ///< JSON Schema of the parameters
    std::string inputs_json;      ///< required/optional inputs
    std::string outputs_json;     ///< produced columns (mmfdb items)
    std::string row_grain;        ///< "burst", "curve_point", "photon", ...
    bool can_replay = false;      ///< re-executable from settings + inputs

    // -- dispatch --
    void* impl = nullptr;         ///< capability-specific, opaque here
};

/*!
 * \brief Register one algorithm.
 *
 * \return false if the descriptor is incomplete (no operation_type or no
 *         capability) or if `operation_type` is already registered. A
 *         duplicate is rejected rather than overwritten: two algorithms
 *         answering to one name is not a preference the registry can resolve,
 *         and silently keeping the last one registered makes the result depend
 *         on load order.
 */
bool register_algorithm(const AlgorithmDescriptor& desc);

/// Every registered algorithm of one capability, as a JSON object keyed by
/// `operation_type`. `{}` when nothing of that capability is registered.
std::string algorithms_json(const std::string& capability);

/// The capabilities that have at least one registration, in registration order.
std::vector<std::string> algorithm_capabilities();

/// Look up one registration. Null when it is not registered.
const AlgorithmDescriptor* find_algorithm(const std::string& operation_type);

/// The `can_replay` registrations, in the shape the `operation` registry
/// category uses, so the provenance system reads live registrations and
/// hand-authored entries through one accessor.
std::string algorithm_operations_json();

/*!
 * \brief Register the algorithms compiled into this library.
 *
 * Called explicitly rather than from a static initialiser. The lesson is
 * already recorded in `DecayFitModelRegistration.h`: a static initialiser in a
 * translation unit that nothing else references is dropped when the library is
 * linked as a static archive, and the algorithm then silently does not exist.
 * Idempotent — calling it twice registers nothing twice.
 */
void register_builtin_algorithms();

} // namespace tttrlib

#endif // TTTRLIB_ALGORITHM_REGISTRY_H
