# FindR.cmake - locate an R installation for building the SWIG R interface.
#
# Sets:
#   R_FOUND         - TRUE if R and its development headers were found
#   R_COMMAND       - path to the R executable
#   R_HOME          - R home directory (R RHOME)
#   R_INCLUDE_DIR   - directory containing R.h / Rinternals.h
#   R_LIBRARIES     - the R shared library to link against (libR)
#
# Honours R_HOME / R_COMMAND from the environment or cache.

include(FindPackageHandleStandardArgs)

# --- R executable ----------------------------------------------------------
find_program(R_COMMAND
    NAMES R
    HINTS ENV R_HOME "$ENV{R_HOME}/bin"
    DOC "Path to the R executable")

if(R_COMMAND)
    # R home
    execute_process(COMMAND ${R_COMMAND} RHOME
        OUTPUT_VARIABLE R_HOME
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    # Include dir: prefer `R CMD config --cppflags` (yields -I<path>), fall back
    # to ${R_HOME}/include.
    execute_process(COMMAND ${R_COMMAND} CMD config --cppflags
        OUTPUT_VARIABLE _r_cppflags
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(REGEX MATCH "-I[^ ]+" _r_inc "${_r_cppflags}")
    string(REGEX REPLACE "^-I" "" _r_inc "${_r_inc}")

    find_path(R_INCLUDE_DIR
        NAMES Rinternals.h R.h
        HINTS "${_r_inc}" "${R_HOME}/include"
        DOC "Directory containing R.h and Rinternals.h")

    # Library: parse `R CMD config --ldflags` (yields -L<dir> -lR); also probe
    # the usual lib/bin locations (Windows uses ${R_HOME}/bin/x64/R.dll).
    execute_process(COMMAND ${R_COMMAND} CMD config --ldflags
        OUTPUT_VARIABLE _r_ldflags
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(REGEX MATCH "-L[^ ]+" _r_libdir "${_r_ldflags}")
    string(REGEX REPLACE "^-L" "" _r_libdir "${_r_libdir}")

    find_library(R_LIBRARIES
        NAMES R libR
        HINTS "${_r_libdir}" "${R_HOME}/lib" "${R_HOME}/bin" "${R_HOME}/bin/x64"
        DOC "R shared library (libR)")
endif()

find_package_handle_standard_args(R
    REQUIRED_VARS R_COMMAND R_INCLUDE_DIR R_LIBRARIES
    FAIL_MESSAGE "Could not find R. Set R_HOME or add R to PATH.")

mark_as_advanced(R_COMMAND R_INCLUDE_DIR R_LIBRARIES R_HOME)
