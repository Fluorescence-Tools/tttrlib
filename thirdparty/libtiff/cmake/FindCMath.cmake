# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindCMath
--------

Find the native CMath includes and library.

IMPORTED Targets
^^^^^^^^^^^^^^^^

This module defines :prop_tgt:`IMPORTED` target ``CMath::CMath``, if
CMath has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

::

  CMath_INCLUDE_DIRS   - Where to find math.h
  CMath_LIBRARIES      - List of libraries when using CMath.
  CMath_FOUND          - True if CMath found.

#]=======================================================================]


include(CheckSymbolExists)
include(CheckLibraryExists)

check_symbol_exists(pow "math.h" CMath_HAVE_LIBC_POW)

if(NOT CMath_HAVE_LIBC_POW)
    # Plain -lm, not find_library: the latter resolves to the build host's
    # glibc linker SCRIPT (/usr/lib/.../libm.so with absolute /lib64 member
    # paths), which a conda cross-sysroot ld cannot follow.
    set(CMath_LIBRARY m)
    set(CMAKE_REQUIRED_LIBRARIES_SAVE ${CMAKE_REQUIRED_LIBRARIES})
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES} ${CMath_LIBRARY})
    check_symbol_exists(pow "math.h" CMath_HAVE_LIBM_POW)
    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES_SAVE})
endif()

set(CMath_pow FALSE)
if(CMath_HAVE_LIBC_POW OR CMath_HAVE_LIBM_POW)
    set(CMath_pow TRUE)
endif()

set(CMath_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(CMath REQUIRED_VARS CMath_pow)

if(CMath_FOUND)
    if(NOT CMath_INCLUDE_DIRS)
        set(CMath_INCLUDE_DIRS)
    endif()
    if(NOT CMath_LIBRARIES)
        if (CMath_LIBRARY)
            set(CMath_LIBRARIES ${CMath_LIBRARY})
        endif()
    endif()

    if(NOT TARGET CMath::CMath)
        # INTERFACE, not UNKNOWN IMPORTED: "m" is a link flag (-lm), not a
        # file path, so it must never become an IMPORTED_LOCATION (Ninja
        # would treat it as a missing file-level dependency).
        add_library(CMath::CMath INTERFACE IMPORTED)
        if(CMath_LIBRARIES)
            set_target_properties(CMath::CMath PROPERTIES
                  INTERFACE_LINK_LIBRARIES "${CMath_LIBRARIES}")
        endif()
    endif()
endif()
