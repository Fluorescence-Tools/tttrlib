// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DECAYFITDESCRIPTORS_H
#define TTTRLIB_DECAYFITDESCRIPTORS_H

/*!
 * \file DecayFitDescriptors.h
 * \brief The decay module's entries in the one registry.
 *
 * There is a single registry (`register_algorithm` / `algorithms_json`,
 * module `algorithm`) and no hand-authored registry literal. The decay fit
 * models declare their `fit` and `fit_setup` entries next to their code
 * (`DecayFitModelFit2x.cpp`, `DecayFitModelNExp.cpp`), the objectives their
 * `objective` entries next to theirs (`DecayStatistics.cpp`), and the burst
 * pipeline operations this module performs (`tcspc_calibration`, `mle_*`) in
 * `DecayFitDescriptors.cpp`. This header only exposes the one call that makes
 * all of them register.
 *
 * \par The flattening rule (normative)
 * Parameters, setup values and results each cross the C++ boundary as one flat
 * `double` array, and a fit entry's `params_schema` is the only description of
 * what each slot means (`DecayFitSetup.cpp` derives the layout from it):
 *   1. properties are laid out in **declaration order** (ordered_json);
 *   2. a scalar property occupies one slot; `boolean` is 0.0/1.0, `integer`
 *      an exactly representable double;
 *   3. a `string` property with an `enum` occupies one slot holding the index
 *      of its value within that enum;
 *   4. an `array` property occupies `count` contiguous slots, `count` coming
 *      from the `count_from` link -- a property of the model's **setup** block;
 *   5. nothing else occupies a slot: construction inputs and outputs are
 *      separate arrays with separate schemas (`setup`, `results_schema`).
 */

namespace tttrlib {

/// Register every entry the decay module contributes to the registry: the
/// `fit`, `fit_setup`, `objective` and `prior` categories and its `operation` entries.
/// Idempotent; explicit rather than a static initialiser (an archive member
/// nothing references is dropped by the linker, DecayFitModelRegistration.h).
void register_decay_descriptors();

}  // namespace tttrlib

#endif  // TTTRLIB_DECAYFITDESCRIPTORS_H
