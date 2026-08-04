// SPDX-License-Identifier: BSD-3-Clause
/*!
 * \file Registry.h
 * \brief Machine-readable description of what tttrlib can do, as JSON.
 *
 * tttrlib is consumed from Python, R and Java, and by tools that build user
 * interfaces on top of it. Those callers repeatedly need to answer questions the
 * compiled API cannot answer for them: which burst searches exist, which file
 * containers can be read, what parameters an algorithm takes, what their defaults
 * and sensible ranges are. Historically each caller hard-coded its own list and
 * drifted out of step whenever tttrlib gained a feature.
 *
 * The registry is the single place those questions are answered, in JSON so every
 * language binding reads the same bytes rather than re-declaring the lists.
 *
 * There are two complementary layers, and the distinction matters:
 *
 *  - **This registry — curated semantics.** Entries are written by hand because
 *    they carry things a C++ signature cannot express: what an algorithm *is*,
 *    which parameters are meaningful, their units, ranges and physical meaning.
 *    This is what a tool needs to *offer* a feature to a user. It covers the
 *    parts of the library where those semantics exist.
 *  - **The API index — generated coverage.** `tools/generate_api_index.py`
 *    derives a complete list of every class, method and function from the built
 *    module, so scripting languages can discover and call *anything*, not only
 *    what has been curated. It is generated rather than authored precisely
 *    because the API is thousands of methods wide and hand-written coverage
 *    would be wrong within a release.
 *
 * Parameter descriptions use **JSON Schema** (`type`, `title`, `description`,
 * `default`, `minimum`, `maximum`) rather than a bespoke vocabulary, so a
 * consumer that can already render a JSON Schema — chisurf renders these into Qt
 * forms with no tttrlib-specific code — needs nothing new to support a feature
 * added here. Non-standard presentation hints (`unit`, `scale`, `advanced`) ride
 * along as extra keys, which a JSON Schema consumer ignores.
 *
 * **Composite entries.** An entry whose behaviour is delegated to another entry —
 * the coincident burst search runs whichever search you name inside each detector
 * group — has a parameter holding the *inner* entry's parameters. Such a property
 * carries `parameters_of: {category, selector}`, meaning "the schema for this
 * object is the `params_schema` of the entry named by the sibling property
 * `selector`, in `category`". That is a declarative link rather than a special
 * case, so a consumer can render the inner parameters as a nested form for any
 * composite entry, present or future, without knowing which entries are
 * composite. Plain JSON Schema consumers ignore the key and fall back to editing
 * the object directly.
 *
 * Adding a category: write a provider returning a JSON object, and add it in
 * registry_json(). Keep every entry's `method` field equal to the name a caller
 * must invoke, and each schema property name equal to that method's argument.
 */
#ifndef TTTRLIB_REGISTRY_H
#define TTTRLIB_REGISTRY_H

#include <string>
#include <vector>

namespace tttrlib {

/*!
 * \brief The whole registry: `{category: {name: entry}}`, as a JSON string.
 *
 * Each entry carries at least `name`, `label` and `summary`; entries describing
 * something callable also carry `method` and a `params_schema`.
 */
std::string registry_json();

/*!
 * \brief The lifetime fit models (`fit` category), as a JSON string.
 *
 * Describes the Fit2x maximum-likelihood models (Fit23/24/25/26): their
 * optimisable model parameters (`params_schema`, in `initial_values` order) and
 * a `setup` link to the shared construction inputs. See FitRegistry.cpp.
 */
std::string fit_models_json();

/*!
 * \brief The shared Fit2x construction inputs (`fit_setup` category), as JSON.
 *
 * The `dt`, `period`, correction factors and objective flags every Fit2x model
 * is built with. Referenced by each `fit` entry's `setup` link.
 */
std::string fit_setup_json();

/*!
 * \brief The selectable fit objectives (`objective` category), as a JSON string.
 *
 * Which statistic a fit minimises, named rather than encoded as flags, so a new
 * statistic can be offered without touching the models that can use it. A model's
 * setup block selects one by name. See FitRegistry.cpp.
 */
std::string fit_objectives_json();

/*!
 * \brief One category of the registry, as a JSON string.
 * \return `{}` when the category does not exist.
 */
std::string registry_category_json(const std::string& category);

/*!
 * \brief Names of the available registry categories.
 */
std::vector<std::string> registry_categories();

} // namespace tttrlib

#endif // TTTRLIB_REGISTRY_H
