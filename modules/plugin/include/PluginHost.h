// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PLUGINHOST_H
#define TTTRLIB_PLUGINHOST_H

/*!
 * \file PluginHost.h
 * \brief Finds, loads and contains plugin libraries.
 *
 * The user-facing promise is one sentence: drop ``tttrlib_<name>.so`` into a
 * directory and the capability it provides is there next time tttrlib is used.
 * Everything here exists to make that true without making ``import tttrlib``
 * something a stranger's binary can break.
 *
 * \section host_when When loading happens
 *
 * Lazily, once, behind a ``std::call_once``, triggered by the first thing that
 * could need a plugin: constructing a ``TTTR``, asking for the registry, or
 * inferring a file type. Not at import, and not from a static initialiser.
 *
 * Two reasons. Import cannot then be broken by whatever is in the plugin
 * directory -- a failed plugin is data in ``registry("plugin")``, not a
 * traceback on ``import``. And registration always happens after every static
 * initialiser in the process has run, which is the difference between
 * registering into a table and registering into one that has not been
 * constructed yet.
 *
 * \section host_where Where it looks
 *
 * In order, first match by plugin *name* winning:
 *
 *   1. every directory in ``$TTTRLIB_PLUGIN_PATH``
 *   2. ``<the tttrlib package>/plugins/``
 *   3. the per-user directory for the platform
 *
 * Never the current working directory, and never the directory of the file
 * being opened. Those two are the whole of the DLL-hijacking attack: a shared
 * instrument drive with a data file and a helpful-looking library beside it.
 *
 * Only files matching ``tttrlib_<name>.<ext>`` are probed, so a plugin's own
 * dependency libraries can sit next to it without being mistaken for plugins.
 * Load order within a directory is case-folded filename order, so a bug is
 * reproducible rather than filesystem-dependent.
 *
 * \section host_contain Containment
 *
 * A plugin that fails to load, fails to init, or registers something invalid is
 * caught, has every registration it made rolled back from a per-plugin journal,
 * and is marked failed. It is never ``dlclose``d -- unloading a library whose
 * static constructors have run is a well-known way to crash at exit, and the
 * memory is bounded by the number of plugins.
 *
 * A plugin that hard-crashes takes the process with it, exactly as a bad numpy
 * extension does. The proportionate mitigations are here rather than an attempt
 * at a sandbox: ``RTLD_NOW`` so a missing symbol is a diagnosis at load instead
 * of a crash mid-file, ``TTTRLIB_PLUGINS=0`` to disable everything, and a
 * quarantine marker written before the library is opened and deleted after it
 * succeeds -- so a plugin that killed the last process is skipped on the next
 * run rather than making Python unstartable.
 */

#include <string>
#include <vector>

#include "tttrlib_plugin.h"

namespace tttrlib {

/// What happened to one candidate plugin. Reported through
/// ``registry("plugin")`` rather than printed, so a failure is inspectable
/// after the fact instead of being a line that scrolled past.
enum class PluginStatus {
    Loaded,        ///< Initialised and its registrations took effect.
    Failed,        ///< Could not be opened or initialised; rolled back.
    Quarantined,   ///< A previous run died while loading this; skipped.
    Shadowed,      ///< A plugin of the same name was found earlier in the path.
    Disabled       ///< Excluded by TTTRLIB_PLUGINS.
};

/// One candidate plugin and what became of it.
struct PluginRecord {
    std::string name;          ///< From the filename; the plugin's own name if it differs is in \ref declared_name.
    std::string declared_name; ///< What the plugin called itself. Usually equal to \ref name.
    std::string version;
    std::string description;
    std::string path;          ///< Absolute path, for provenance.
    std::string sha256;        ///< Of the file as loaded. A published result should be able to say what ran.
    PluginStatus status = PluginStatus::Failed;
    std::string message;       ///< Why, when the status is not Loaded.
    std::vector<std::string> containers;   ///< Format names this plugin registered.
};

/*!
 * \brief The loaded plugins, and the capabilities they contributed.
 *
 * A namespace of statics rather than an injectable object: the tables it feeds
 * -- the format registry, the container name map -- are process-wide statics
 * themselves, and a second host would mean two answers to "what formats exist".
 */
class PluginHost {
public:
    /*!
     * \brief Load every plugin, once. Safe and cheap to call repeatedly.
     *
     * Every entry point that could need a plugin calls this first. The work
     * happens on the first call; later calls are one atomic load.
     */
    static void ensure_loaded();

    /// Every candidate found, in discovery order, whatever became of it.
    static const std::vector<PluginRecord>& plugins();

    /*!
     * \brief The container table for \p container_type, or nullptr.
     *
     * How the reader path reaches a plugin format: ``TTTR::read_file`` asks for
     * the container id it was given, and if a plugin owns it, reads through
     * this instead of the built-in dispatch. Keeps the format description
     * (``FileFormat``) free of function pointers into somebody else's binary.
     */
    static const tttrlib_container_v1* container_for(int container_type);

    /*!
     * \brief Report a plugin's failure the way the plugin itself would.
     *
     * Exposed so the read path can attribute an error to the plugin that caused
     * it, rather than to tttrlib.
     */
    static std::string last_error();

    /*!
     * \brief Accept decay fit models from plugins, using \p registrar.
     *
     * This module sits below the fitting code -- deliberately, because that is
     * what lets a plugin register a *format* without dragging the fitting stack
     * beneath the format table. So it cannot construct a fit model itself, and
     * the layer that can hands down the means to, exactly as the readers hand
     * down their content sniffers.
     *
     * Until a registrar is installed, a plugin calling ``register_decay_fit``
     * gets \ref TTTRLIB_UNSUPPORTED and a message saying the fitting module is
     * not loaded -- which is an honest answer rather than a silent failure.
     *
     * \param registrar Returns false if it rejects the model (a taken name).
     */
    static void set_decay_fit_registrar(
            bool (*registrar)(const tttrlib_decay_fit_v1* fit));

    /// Every decay fit model registered by a plugin, in registration order.
    static const std::vector<const tttrlib_decay_fit_v1*>& decay_fits();

    /*!
     * \brief Those models as registry entries, ready to splice into `fit`.
     *
     * Built here, from the C tables, because everything needed is already in
     * them -- the plugin wrote the schema. That is what lets the registry
     * publish a plugin's fit model without the registry depending on the
     * fitting stack, which would be a cycle.
     *
     * Returns "" when no plugin provides one, so the caller can skip the splice
     * entirely in the overwhelmingly common case.
     */
    static std::string decay_fit_models_json();

    /// The burst search registered under \p name, or nullptr.
    static const tttrlib_burst_search_v1* burst_search(const std::string& name);

    /// Every burst search a plugin registered, in registration order.
    static const std::vector<const tttrlib_burst_search_v1*>& burst_searches();

    /*!
     * \brief Those searches as registry entries, ready to splice into
     *        `burst_search`.
     *
     * As \ref decay_fit_models_json, and for the same reason: everything needed
     * is already in the C table, so the category can publish a plugin's search
     * without depending on anything above this module. Entries carry
     * ``"provider": "plugin"`` and no ``method``, which is how a caller knows to
     * dispatch by name rather than by attribute.
     */
    static std::string burst_searches_json();

    // ── generic operations ─────────────────────────────────────────

    /// The operation registered under \p name, or nullptr.
    static const tttrlib_operation_v1* operation(const std::string& name);

    /// Every operation a plugin registered, in registration order.
    static const std::vector<const tttrlib_operation_v1*>& operations();

    /*!
     * \brief Plugin operations as registry entries, ready to splice into the
     *        ``operation`` category.
     *
     * Same shape as \ref burst_searches_json: everything needed is in the C
     * table. Entries carry ``"provider": "plugin"``.
     */
    static std::string operations_json();

    // ── correlation methods / decay priors ─────────────────────────

    /// The correlation kernel registered under \p name, or nullptr.
    static const tttrlib_correlation_method_v1* correlation_method(const std::string& name);
    /// Every correlation kernel a plugin registered, in registration order.
    static const std::vector<const tttrlib_correlation_method_v1*>& correlation_methods();

    /// The prior kind registered under \p kind, or nullptr.
    static const tttrlib_decay_prior_v1* decay_prior(const std::string& kind);
    /// Every prior kind a plugin registered, in registration order.
    static const std::vector<const tttrlib_decay_prior_v1*>& decay_priors();
};

}  // namespace tttrlib

#endif  // TTTRLIB_PLUGINHOST_H
