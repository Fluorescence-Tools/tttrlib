// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_ALGORITHM_REGISTRY_H
#define TTTRLIB_ALGORITHM_REGISTRY_H

#include <string>
#include <vector>

/// One descriptor and one registration call for every
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
 * `impl` is opaque to the host. A capability registrar knows
 * how to turn it into the C++ object or call for its capability — which is the
 * seam that lets a C ABI plugin table and a C++ built-in factory travel one
 * registration path.
 */
struct AlgorithmDescriptor {
    // -- identity (mmfdb / flrCIF canonical names) --
    /// The mmfdb `operation_type` this algorithm performs, e.g.
    /// "fcs_correlation". Not necessarily unique: two entries can perform the
    /// same operation type on different inputs (the burst-MLE fit on the green
    /// and on the red detector both are `burst_lifetime_fitting`).
    std::string operation_type;
    /// The registry key -- what a caller names the entry by (`registry("fit")
    /// ["fit23"]`, `burst_search_by_name("maxtree")`). Unique within the
    /// registry. Empty means "same as operation_type", which is the common
    /// case; emitted as `name`.
    std::string name;
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

    // -- capability-specific keys --
    /// A JSON object whose keys are merged into the emitted entry *after* the
    /// generic ones, so a capability can carry what only it needs -- a fit's
    /// `params_schema` / `results_schema` / `setup` link / `n_patterns` /
    /// `supports_lnprob`, an operation's `data_format` / `kind` / structured
    /// `inputs` -- without the descriptor growing a field per capability. Key
    /// order is preserved (ordered_json), which is what the fit parameter
    /// flattening rule depends on. Empty means nothing extra.
    std::string extra_json;

    // -- dispatch --
    void* impl = nullptr;         ///< capability-specific, opaque here
};

/// The registry key of \p d: `name`, or `operation_type` when `name` is empty.
inline const std::string& algorithm_key(const AlgorithmDescriptor& d) {
    return d.name.empty() ? d.operation_type : d.name;
}

/*!
 * \brief Register one algorithm.
 *
 * \return false if the descriptor is incomplete (no operation_type or no
 *         capability) or if its key (`name`, else `operation_type`) is already
 *         registered. A
 *         duplicate is rejected rather than overwritten: two algorithms
 *         answering to one name is not a preference the registry can resolve,
 *         and silently keeping the last one registered makes the result depend
 *         on load order.
 */
bool register_algorithm(const AlgorithmDescriptor& desc);

/// Every registered algorithm of one capability, as a JSON object keyed by
/// `operation_type`. `{}` when nothing of that capability is registered.
/*!
 * \brief Register one algorithm from a complete JSON entry.
 *
 * The entry is what `registry("<capability>")["<key>"]` will show: the
 * generic descriptor fields are read from it (`label`, `summary`,
 * `description`, `operation_type` (defaults to \p key), `params_schema` /
 * `settings_schema`, `inputs`, `outputs`, `row_grain`, `can_replay`,
 * `method`) and the whole object is carried as `extra_json`, so every key
 * the entry had is emitted back exactly. This is how the fit models, the
 * fit-setup blocks, the objectives and the pipeline operations declare
 * themselves next to their code -- one registration path for everything
 * the registry lists, and no hand-authored registry literal anywhere.
 *
 * \return as register_algorithm; also false if \p entry_json is not a JSON
 *         object.
 */
bool register_algorithm_json(const std::string& capability, const std::string& key,
                             const std::string& entry_json);

/*!
 * \brief Remove the entry registered under \p key.
 *
 * Exists for exactly one caller: the plugin host rolling back a plugin whose
 * init failed after it had registered something. Nothing built in is ever
 * unregistered. \return whether an entry was removed.
 */
bool unregister_algorithm(const std::string& key);

std::string algorithms_json(const std::string& capability);

/// The capabilities that have at least one registration, in registration order.
std::vector<std::string> algorithm_capabilities();

/// Look up one registration. Null when it is not registered.
/// The descriptor registered under \p key (`name`, else `operation_type`), or nullptr.
const AlgorithmDescriptor* find_algorithm(const std::string& key);

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
