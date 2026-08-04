// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECAYFITMODELREGISTRATION_H
#define TTTRLIB_DECAYFITMODELREGISTRATION_H

/*!
 * \file DecayFitModelRegistration.h
 * \brief Explicit registration entry points for the built-in decay fit models.
 *
 * These used to be `const bool registered = []{...}();` objects in each model's
 * anonymous namespace, which nothing referenced. That works only as long as
 * every consumer links the loose `.o` files: a static archive member is pulled
 * in only when some symbol in it is needed, so `libtttrlib_static.a` -- which
 * the conda R package links -- is free to drop those translation units entirely,
 * and with them every model. `make_decay_fit("fit_nexp")` would then throw
 * "unknown decay fit" in a build that compiled the model perfectly well.
 *
 * Naming the registration and calling it from `register_builtin_decay_fits()`
 * gives the linker a reason to keep the object file. It is also the shape the
 * plugin host needs later: registration becomes something that happens at a
 * defined moment rather than during static initialisation, in an order no one
 * controls.
 *
 * Internal to the build (`src/`), deliberately not in `include/`: this is not
 * API, and keeping it out of the installed headers keeps it away from SWIG.
 */

/// Register fit23 / fit24 / fit25 / fit26. Idempotent.
void register_decay_fit_models_fit2x();

/// Register fit_nexp. Idempotent.
void register_decay_fit_models_nexp();

#endif  // TTTRLIB_DECAYFITMODELREGISTRATION_H
