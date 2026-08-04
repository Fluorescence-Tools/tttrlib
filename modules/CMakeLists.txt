# SPDX-License-Identifier: BSD-3-Clause
#
# Module declarations.
#
# Exactly ONE module today: `legacy`, holding the whole of src/. That is
# deliberate. Splitting the sources and switching the build to shared module
# libraries are two independent risks, and doing them at once makes every
# failure ambiguous. With a single module the sources are untouched, so anything
# that breaks is the linkage change and nothing else -- and it is the linkage
# change that has to be proven on every platform at once: the per-target
# visibility override, WINDOWS_EXPORT_ALL_SYMBOLS, $ORIGIN / @loader_path,
# auditwheel / delocate / delvewheel repair, the ImageJ JAR and the R package.
#
# Subsequent phases move subsystems out of `legacy` one at a time. Each of those
# is a small, reviewable change whose blast radius is one module, because
# tttrlib_finalize_modules() refuses to configure if a source ends up claimed
# twice or not at all.
#
# Extraction order (leaves first, so nothing has to be moved back):
#   opt, hist, imageio, sim, pda, superres, localization,
#   then burst, core, decay, imaging, nn, hmm.

# Extracted modules first: each owns its own sources under modules/<name>/src,
# so what is left in src/ is by definition what nobody has claimed yet.
add_subdirectory(opt)
add_subdirectory(hist)
add_subdirectory(imageio)
add_subdirectory(sim)
add_subdirectory(localization)

file(GLOB_RECURSE TTTRLIB_LEGACY_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")

tttrlib_add_module(
    NAME    legacy
    SOURCES ${TTTRLIB_LEGACY_SOURCES}
    # The first declared edges between modules. TTTR.h and TTTRHeader.h include
    # Histogram.h; DecayFit.h, DecayFit23.h and ImageLocalization.h include
    # i_lbfgs.h. Declaring them is what puts those headers on this module's
    # compile line -- and, equally, keeps everything else off it.
    DEPENDS opt hist sim
    # Everything, for now. The point of the split is that these become per-module
    # declarations -- Eigen is REQUIRED project-wide today for the sake of two
    # files, and that only stops being true once `localization` and `nn` are the
    # only modules that ask for it.
    EXTERNAL_DEPS
        tttrlib::json
        tttrlib::pocketfft
        tttrlib::autodiff
        tttrlib::eigen
        tttrlib::highfive
    TEST_DIR test/python
)

# These two depend on `legacy`, so they are declared after it. Both are leaves of
# the include graph -- nothing else includes Pda.h or CLSMSuperRes.h -- but their
# implementations read the photon-stream data model, so the edge is real and
# becomes `core` / `core imaging` once those modules exist.
add_subdirectory(pda)
add_subdirectory(superres)

tttrlib_finalize_modules()
