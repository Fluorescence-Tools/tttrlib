cmake_policy(SET CMP0078 NEW)
cmake_policy(SET CMP0086 NEW)
cmake_policy(SET CMP0094 NEW)
# Use FindPython3 instead of FindPython for better Python 3.13 support
if(POLICY CMP0148)
    cmake_policy(SET CMP0148 NEW)
endif()


# Module headers must be on the include path of every binding target -- and, 
# critically, of swig itself. See cmake/TTTRLibModule.cmake.
tttrlib_module_include_directories()

SET(MODULE_NAME ${PROJECT_NAME})

FIND_PACKAGE(SWIG REQUIRED)
MESSAGE(STATUS "SWIG_EXECUTABLE: ${SWIG_EXECUTABLE}")
MESSAGE(STATUS "SWIG_VERSION: ${SWIG_VERSION}")
INCLUDE(${SWIG_USE_FILE})
INCLUDE_DIRECTORIES(.)

# glibc on LP64 Linux defines int64_t as 'long', but SWIG's stdint.i (pulled
# in by Sim.i) typedefs it to 'long long' unless SWIGWORDSIZE64 is defined,
# and depending on the SWIG build the generated Python wrappers then mix
# std::vector<long long> with the library's std::vector<long> and do not
# compile (seen with conda-forge swig 4.2.1 and 4.4.1). With the flag the
# typedef says 'long', which is correct on Linux whether SWIG resolves it or
# keeps int64_t literal, so pass it unconditionally for the Python and Java
# modules. The R module is the exception: it never includes stdint.i, and
# SWIG 4.2's R backend mishandles 'long long' returns under SWIGWORDSIZE64
# (the C side returns a native vector while the R proxy expects an external
# pointer), so R only gets the flag for SWIG >= 4.4 where it is verified fine.
IF(UNIX AND NOT APPLE AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    SET(TTTRLIB_SWIG_INT64_FLAG "-DSWIGWORDSIZE64")
    LIST(APPEND CMAKE_SWIG_FLAGS ${TTTRLIB_SWIG_INT64_FLAG})
    IF(NOT SWIG_VERSION VERSION_LESS "4.4")
        SET(TTTRLIB_SWIG_INT64_FLAG_R "-DSWIGWORDSIZE64")
    ENDIF()
ENDIF()

IF(BUILD_PYTHON_INTERFACE)

    IF(BUILD_PYTHON_DOCS)
        # Doxygen: To create the python interface documentation
        FIND_PACKAGE(Doxygen REQUIRED)
        MESSAGE(STATUS "Building documentation for Python interface")
        MESSAGE(STATUS "Doxygen executable " ${DOXYGEN_EXECUTABLE})
        #This will be the main output of our command
        execute_process(COMMAND ${DOXYGEN_EXECUTABLE} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/doc OUTPUT_QUIET)
        execute_process(
                COMMAND python ${CMAKE_SOURCE_DIR}/tools/doxy2swig.py ${CMAKE_SOURCE_DIR}/doc/_doxygen_build/xml/index.xml ${CMAKE_SOURCE_DIR}/ext/documentation.i
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    ENDIF()

    MESSAGE(STATUS "Building Python interface")
    
    # Use Python3 explicitly for better compatibility with Python 3.13+
    # For conda builds, use the Python from PREFIX (host environment)
    if(DEFINED ENV{PREFIX})
        set(Python3_ROOT_DIR "$ENV{PREFIX}")
        set(Python3_FIND_STRATEGY LOCATION)
        MESSAGE(STATUS "Using Python3_ROOT_DIR from conda PREFIX: $ENV{PREFIX}")
    elseif(DEFINED Python_ROOT_DIR)
        # For cibuildwheel and setup.py, use Python_ROOT_DIR if provided
        set(Python3_ROOT_DIR "${Python_ROOT_DIR}")
        set(Python3_FIND_STRATEGY LOCATION)
        MESSAGE(STATUS "Using Python3_ROOT_DIR from Python_ROOT_DIR: ${Python_ROOT_DIR}")
    endif()
    FIND_PACKAGE(Python3 REQUIRED COMPONENTS Interpreter Development.Module NumPy)
    
    # Mark Python_EXECUTABLE as used if it was provided (for setup.py compatibility)
    # This suppresses CMake warning about unused manually-specified variables
    if(DEFINED Python_EXECUTABLE)
        MESSAGE(STATUS "Python_EXECUTABLE provided but not used (Python3_EXECUTABLE is used instead): ${Python_EXECUTABLE}")
    endif()
    
    # Debug output
    MESSAGE(STATUS "Python3_FOUND: ${Python3_FOUND}")
    MESSAGE(STATUS "Python3_VERSION: ${Python3_VERSION}")
    MESSAGE(STATUS "Python3_EXECUTABLE: ${Python3_EXECUTABLE}")
    MESSAGE(STATUS "Python3_INCLUDE_DIRS: ${Python3_INCLUDE_DIRS}")
    MESSAGE(STATUS "Python3_NumPy_INCLUDE_DIRS: ${Python3_NumPy_INCLUDE_DIRS}")
    
    INCLUDE_DIRECTORIES(BEFORE ${Python3_NumPy_INCLUDE_DIRS} ${Python3_INCLUDE_DIRS})
    LINK_LIBRARIES(Python3::Module Python3::NumPy)

    # Handle where to install the resulting Python package
    #
    # Both spellings are needed. scikit-build-core sets SKBUILD as a CACHE
    # variable (SKBUILD=2); cibuildwheel sets it in the environment. Testing
    # only the environment meant a plain `pip install .` took the site-packages
    # branch and tried to install to an absolute path outside the wheel root.
    if(SKBUILD OR DEFINED ENV{SKBUILD})
        # The build backend sets CMAKE_INSTALL_PREFIX to the root of the
        # resulting wheel archive
        set(MY_INSTALL_PREFIX ".")
    else()
        # The Python package is installed directly in the folder of the
        # detected interpreter (system, user, or virtualenv)
        set(MY_INSTALL_PREFIX ${Python3_SITELIB})
    endif()

    # Everything Python-side lands in a real package DIRECTORY, not loose next
    # to it in site-packages. `tttrlib.py` + `_tttrlib*.so` at the top level is
    # what shipped before, which leaves nowhere to put a sibling shared library,
    # nowhere for delvewheel to inject its DLL-path patch, nothing for
    # os.add_dll_directory to point at, and no plugins/ directory. It is also
    # what recipes/py/build.sh already reassembles by hand after installing.
    set(TTTRLIB_PY_PACKAGE_DIR "${MY_INSTALL_PREFIX}/tttrlib")
    SET_SOURCE_FILES_PROPERTIES(python/tttrlib.i PROPERTIES CPLUSPLUS ON SWIG_MODULE_NAME ${PROJECT_NAME})

    # Ensure SWIG reruns when any of the included interface or helper Python files change.
    file(GLOB SWIG_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/python/*.i")
    list(REMOVE_ITEM SWIG_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/python/tttrlib.i")
    file(GLOB SWIG_PYTHON_HELPERS
            "${CMAKE_CURRENT_SOURCE_DIR}/python/*.py"
    )
    # Exclude the generated wrapper (lives in the build directory, not the source tree)
    list(FILTER SWIG_PYTHON_HELPERS EXCLUDE REGEX ".*/tttrlib.py$")

    set(SWIG_DEPENDENCIES "${SWIG_INTERFACE_FILES};${SWIG_PYTHON_HELPERS}")
    if(SWIG_DEPENDENCIES)
        list(REMOVE_DUPLICATES SWIG_DEPENDENCIES)
        # NB: SWIG_MODULE_DEPENDS is NOT a property UseSWIG reads -- it was silently
        # ignored, so editing a .i (or a header) never regenerated the wrapper and a
        # cached build directory could ship a stale one. UseSWIG.cmake consumes the
        # SWIG_MODULE_<name>_EXTRA_DEPS variable (and the target's SWIG_DEPENDS).
        set(SWIG_MODULE_${MODULE_NAME}_EXTRA_DEPS ${SWIG_DEPENDENCIES})
    endif()
    # Better still: let swig compute implicit dependencies itself, which also covers
    # the C++ headers pulled in by %include. Requires CMake >= 3.20 (this project
    # declares 3.13), hence the explicit list above as a fallback.
    set_property(SOURCE python/tttrlib.i PROPERTY USE_SWIG_DEPENDENCIES TRUE)

    # Fix file path on Windows systems
    FILE(TO_CMAKE_PATH "${CMAKE_SWIG_OUTDIR}" CMAKE_SWIG_OUTDIR)
    MESSAGE(STATUS "CMAKE_SWIG_OUTDIR: '${CMAKE_SWIG_OUTDIR}'")
    MESSAGE(STATUS "SWIG_SUPPORT_FILES_DIRECTORY: '${SWIG_SUPPORT_FILES_DIRECTORY}'")
    MESSAGE(STATUS "MY_INSTALL_PREFIX: '${MY_INSTALL_PREFIX}'")
    SWIG_ADD_LIBRARY(
            ${MODULE_NAME}
            TYPE MODULE
            LANGUAGE python
            SOURCES python/tttrlib.i
    )
    # Link the modules rather than recompiling every source into the wrapper.
    # The same glob used to be handed to this target, to the R target, to the
    # Java target and to both library targets, so src/ was compiled up to five
    # times per configure.
    tttrlib_link_all_modules(${MODULE_NAME})

    # The wrapper itself still needs the OpenMP include dirs and flags: it
    # includes the project headers, and info.h reaches for <omp.h>.
    if(WITH_OPENMP AND OpenMP_CXX_FOUND)
        target_include_directories(${MODULE_NAME} PRIVATE ${OpenMP_CXX_INCLUDE_DIRS})
        # OpenMP_CXX_FLAGS is a *string*, and on AppleClang it is two words
        # ("-Xclang -fopenmp"). target_compile_options treats each list element
        # as one argument, so passing it unsplit hands clang the single argument
        # "-Xclang -fopenmp" and every translation unit dies with
        # "unknown argument". Split it the way a shell would.
        separate_arguments(_tttrlib_omp_cxx_flags NATIVE_COMMAND "${OpenMP_CXX_FLAGS}")
        target_compile_options(${MODULE_NAME} PRIVATE ${_tttrlib_omp_cxx_flags})
    endif()

    # Force correct Python extension suffix for auditwheel compatibility
    if(Python3_EXECUTABLE)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX') or sysconfig.get_config_var('SO') or '.so')"
            OUTPUT_VARIABLE PYTHON_EXT_SUFFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(PYTHON_EXT_SUFFIX)
            MESSAGE(STATUS "Setting Python extension suffix to: ${PYTHON_EXT_SUFFIX}")
            set_target_properties(${MODULE_NAME} PROPERTIES SUFFIX "${PYTHON_EXT_SUFFIX}")
        endif()
    endif()

    # The extension now loads its module libraries from its own directory, so it
    # needs the rpath in EVERY build, not only wheel builds: the conda package
    # installs the same layout into site-packages/tttrlib/.
    tttrlib_set_sibling_rpath(${MODULE_NAME})

    # Keep the SWIG runtime type table private to this module. SWIG modules
    # otherwise share one process-global table, and whichever registers a common
    # type such as std::vector<double> first owns the entry -- so a proxy created
    # here stops matching a by-value argument of that type as soon as another SWIG
    # extension (IMP, for one) is imported first. The failure is import-order
    # dependent and reads as an unrelated TypeError. Nothing here is meant to be
    # exchanged with another SWIG module, so a private table costs nothing.
    TARGET_COMPILE_DEFINITIONS(${MODULE_NAME} PRIVATE SWIG_TYPE_TABLE=tttrlib)

    TARGET_LINK_LIBRARIES(
            ${MODULE_NAME}
            Python3::Module
            ${CMAKE_THREAD_LIBS_INIT}
    )
    # The OpenMP link flags reach the top-level CMAKE_EXE_LINKER_FLAGS only, and
    # a Python extension is a MODULE library rather than an executable, so it
    # never saw them: the module compiled with -fopenmp but linked without the
    # runtime. The R and Java modules below always linked OpenMP explicitly; do
    # the same here. Without this the module fails at *import* rather than at
    # link time on macOS, where the dynamic-lookup flag below lets the
    # unresolved OpenMP symbols through ("symbol not found in flat namespace
    # '___kmpc_barrier'"), which forced every downstream build to inject
    # -lomp by hand.
    IF(OpenMP_CXX_FOUND)
        TARGET_LINK_LIBRARIES(${MODULE_NAME} OpenMP::OpenMP_CXX)
    ENDIF()
    # On macOS, use dynamic lookup for Python symbols
    IF (APPLE)
        # Plain dynamic_lookup, NOT -flat_namespace. Both let the Python symbols
        # stay unresolved until the interpreter loads the extension, which is the
        # only thing needed here -- but flat namespace is process-wide: it merges
        # every loaded image into one symbol namespace, so a plugin (or any other
        # extension) that statically links its own zlib or libtiff can interpose
        # on the copies tttrlib bundles, and which definition wins depends on load
        # order. That is a bad property for a library that is about to start
        # loading third-party binaries on purpose.
        SET_TARGET_PROPERTIES(${MODULE_NAME} PROPERTIES LINK_FLAGS "-Wl,-undefined,dynamic_lookup")
    ENDIF()

    # =======
    # Install
    # =======
    # Get the autogenerated Python file
    get_property(WRAPPER_PY_FILE
            TARGET ${MODULE_NAME}
            PROPERTY SWIG_SUPPORT_FILES)

    # Install the autogenerated Python file as the package's __init__.py
    INSTALL(FILES ${WRAPPER_PY_FILE}
            DESTINATION ${TTTRLIB_PY_PACKAGE_DIR}
            RENAME __init__.py
            COMPONENT bindings)

    # Where the module libraries go depends on who is packaging.
    #
    # A wheel must be self-contained, so they sit beside the extension inside
    # tttrlib/ and @loader_path / $ORIGIN finds them. (pyproject.toml already
    # passes -DCMAKE_INSTALL_LIBDIR=tttrlib for exactly this.)
    #
    # A conda package must NOT do that. conda-build rewrites install names and
    # rpaths in post-processing on the assumption that shared libraries live in
    # $PREFIX/lib: it turns the extension's `@loader_path` into
    # `@loader_path/../../..`, which from site-packages/tttrlib/ is $PREFIX/lib.
    # Installing the modules next to the extension there produces a package that
    # builds and packages cleanly and then fails at `import tttrlib` with the
    # library not found -- which is exactly what happened. Put them where
    # conda-build's rewriting already points.
    #
    # COMPONENT bindings is load-bearing either way: pyproject.toml installs only
    # that component, so a module installed without it yields a wheel that
    # builds, repairs and uploads cleanly and then ImportErrors.
    include(GNUInstallDirs)
    if(SKBUILD OR DEFINED ENV{SKBUILD})
        set(TTTRLIB_MODULE_INSTALL_DIR "${TTTRLIB_PY_PACKAGE_DIR}")
    elseif(WIN32)
        # No rpath on Windows: the loader finds a pyd's dependencies only
        # next to the pyd itself, so the module DLLs go into the package.
        set(TTTRLIB_MODULE_INSTALL_DIR "${TTTRLIB_PY_PACKAGE_DIR}")
    else()
        set(TTTRLIB_MODULE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")
    endif()
    MESSAGE(STATUS "Module libraries install dir: ${TTTRLIB_MODULE_INSTALL_DIR}")
    tttrlib_install_modules(DESTINATION ${TTTRLIB_MODULE_INSTALL_DIR} COMPONENT bindings)

    # The drop-in plugin directory. Shipped with a README so it exists, is
    # discoverable, and explains itself without anyone having to find the docs.
    INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/plugins-README.txt
            DESTINATION ${TTTRLIB_PY_PACKAGE_DIR}/plugins
            RENAME README.txt
            COMPONENT bindings)

    # Provide a lightweight target to regenerate only the SWIG-generated Python wrapper.
    add_custom_target(${MODULE_NAME}_python_wrapper
            DEPENDS ${WRAPPER_PY_FILE}
            COMMENT "Regenerate SWIG-generated Python wrapper ${WRAPPER_PY_FILE}")

    # Backward compatible alias so users can run: cmake --build <build-dir> --target python_wrapper
    if(NOT TARGET python_wrapper)
        add_custom_target(python_wrapper
                DEPENDS ${MODULE_NAME}_python_wrapper)
    endif()

    # Install the SWIG library
    INSTALL(TARGETS ${MODULE_NAME}
            COMPONENT bindings
            LIBRARY DESTINATION ${TTTRLIB_PY_PACKAGE_DIR}
            ARCHIVE DESTINATION ${TTTRLIB_PY_PACKAGE_DIR}
            RUNTIME DESTINATION ${TTTRLIB_PY_PACKAGE_DIR}
    )

    # Configure HDF5 DLL bundling for Windows wheels (when using h5py)
    if(BUILD_PHOTON_HDF AND HDF5_IS_FROM_H5PY AND WIN32)
        message(STATUS "Configuring HDF5 DLL bundling for Windows wheel")

        # Find the Python extension target
        if(TARGET ${MODULE_NAME})
            # Get the Python extension's install directory
            # For scikit-build-core, the extension is installed to ${MY_INSTALL_PREFIX}
            # which should be the same directory as the Python module

            message(STATUS "Python extension install dir: ${TTTRLIB_PY_PACKAGE_DIR}")

            # Install HDF5 DLLs to same directory as Python extension (for Windows)
            foreach(dll_path ${HDF5_DLL_PATHS})
                file(TO_CMAKE_PATH "${dll_path}" dll_path_cmake)
                install(FILES "${dll_path_cmake}"
                        DESTINATION "${TTTRLIB_PY_PACKAGE_DIR}"
                        COMPONENT bindings
                )
                message(STATUS "Bundling HDF5 DLL: ${dll_path_cmake}")
            endforeach()
        else()
            message(WARNING "Python extension target '${MODULE_NAME}' not found - DLL bundling skipped")
        endif()
    else()
        message(STATUS "HDF5 DLL bundling not needed (Linux/macOS or traditional build)")
    endif()

ENDIF(BUILD_PYTHON_INTERFACE)

IF(BUILD_R_INTERFACE)
    MESSAGE(STATUS "Building R interface")
    FIND_PACKAGE(R REQUIRED)
    INCLUDE_DIRECTORIES(${R_INCLUDE_DIR})

    # Distinct target name so it never collides with the Python "tttrlib" target,
    # but keep the SWIG module (and generated wrapper) named "tttrlib".
    SET(R_MODULE_NAME tttrlibR)
    SET_SOURCE_FILES_PROPERTIES(r/tttrlib.i PROPERTIES
            CPLUSPLUS ON
            SWIG_MODULE_NAME tttrlib)

    # r/tttrlib.i re-includes the shared fragments that live in ext/python.
    SET(CMAKE_SWIG_FLAGS -I${CMAKE_CURRENT_SOURCE_DIR}/python ${TTTRLIB_SWIG_INT64_FLAG_R})

    # Rerun SWIG when any shared fragment, the R module, or the R array library changes.
    file(GLOB R_SWIG_DEPS
            "${CMAKE_CURRENT_SOURCE_DIR}/python/*.i"
            "${CMAKE_CURRENT_SOURCE_DIR}/r/*.i")
    # See the Python block: SWIG_MODULE_DEPENDS is not read by UseSWIG.
    set(SWIG_MODULE_${R_MODULE_NAME}_EXTRA_DEPS ${R_SWIG_DEPS})
    set_property(SOURCE r/tttrlib.i PROPERTY USE_SWIG_DEPENDENCIES TRUE)

    SWIG_ADD_LIBRARY(
            ${R_MODULE_NAME}
            TYPE MODULE
            LANGUAGE r
            SOURCES r/tttrlib.i
    )
    tttrlib_link_all_modules(${R_MODULE_NAME})
    # R expects the loadable object to be named "tttrlib.so" (no "lib" prefix).
    SET_TARGET_PROPERTIES(${R_MODULE_NAME} PROPERTIES
            OUTPUT_NAME tttrlib
            PREFIX "")
    TARGET_LINK_LIBRARIES(
            ${R_MODULE_NAME}
            ${R_LIBRARIES}
            ${HDF5_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
    )
    IF(OpenMP_CXX_FOUND)
        TARGET_LINK_LIBRARIES(${R_MODULE_NAME} OpenMP::OpenMP_CXX)
    ENDIF()

    # Stage the SWIG-generated R wrapper (tttrlib.R) into the package source tree.
    get_property(WRAPPER_R_FILE TARGET ${R_MODULE_NAME} PROPERTY SWIG_SUPPORT_FILES)
    add_custom_target(${R_MODULE_NAME}_wrapper
            DEPENDS ${WRAPPER_R_FILE}
            COMMENT "Regenerate SWIG-generated R wrapper ${WRAPPER_R_FILE}")

    # Reset for any subsequent language.
    UNSET(CMAKE_SWIG_FLAGS)
ENDIF()

IF(BUILD_JAVASCRIPT_INTERFACE)
    MESSAGE(STATUS "Building JavaScript (Node-API) interface")

    # Node supplies the headers through two npm packages rather than a system
    # include directory: node-api-headers has the C API (node_api.h), and
    # node-addon-api has the C++ wrapper (napi.h) that the SWIG -napi backend
    # generates against. Ask node for their locations rather than guessing.
    FIND_PROGRAM(NODE_EXECUTABLE NAMES node nodejs REQUIRED)
    MESSAGE(STATUS "NODE_EXECUTABLE: ${NODE_EXECUTABLE}")

    # Resolve from ext/js/pkg, whose package.json declares both as dependencies,
    # so `npm install` there is the single documented setup step.
    SET(JS_PKG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/js/pkg)
    # path.resolve() because node-addon-api reports include_dir RELATIVE to the
    # current directory ("node_modules/node-addon-api"); handing that to
    # include_directories() silently resolves against the build tree and the
    # compiler then cannot find napi.h.
    execute_process(
            COMMAND ${NODE_EXECUTABLE} -p "require('path').resolve(require('node-api-headers').include_dir)"
            WORKING_DIRECTORY ${JS_PKG_DIR}
            OUTPUT_VARIABLE NODE_API_HEADERS_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _node_api_headers_res
            ERROR_QUIET)
    execute_process(
            COMMAND ${NODE_EXECUTABLE} -p "require('path').resolve(require('node-addon-api').include_dir)"
            WORKING_DIRECTORY ${JS_PKG_DIR}
            OUTPUT_VARIABLE NODE_ADDON_API_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _node_addon_api_res
            ERROR_QUIET)
    IF(NOT _node_api_headers_res EQUAL 0 OR NOT _node_addon_api_res EQUAL 0)
        MESSAGE(FATAL_ERROR
                "node-api-headers / node-addon-api not found. Run `npm install` in "
                "${JS_PKG_DIR} before configuring with -DBUILD_JAVASCRIPT_INTERFACE=ON.")
    ENDIF()
    MESSAGE(STATUS "node-api-headers: ${NODE_API_HEADERS_DIR}")
    MESSAGE(STATUS "node-addon-api:   ${NODE_ADDON_API_DIR}")
    INCLUDE_DIRECTORIES(${NODE_API_HEADERS_DIR} ${NODE_ADDON_API_DIR})

    SET(JS_MODULE_NAME tttrlibJs)
    SET_SOURCE_FILES_PROPERTIES(js/tttrlib.i PROPERTIES
            CPLUSPLUS ON
            SWIG_MODULE_NAME tttrlib)

    # js/tttrlib.i re-includes the shared fragments that live in ext/python, and
    # ext/js supplies jsarrays.i and js_shared_ptr.i.
    SET(CMAKE_SWIG_FLAGS -napi
            -I${CMAKE_CURRENT_SOURCE_DIR}/python
            -I${CMAKE_CURRENT_SOURCE_DIR}/js
            ${TTTRLIB_SWIG_INT64_FLAG})

    file(GLOB JS_SWIG_DEPS
            "${CMAKE_CURRENT_SOURCE_DIR}/python/*.i"
            "${CMAKE_CURRENT_SOURCE_DIR}/js/*.i")
    # See the Python block: SWIG_MODULE_DEPENDS is not read by UseSWIG.
    set(SWIG_MODULE_${JS_MODULE_NAME}_EXTRA_DEPS ${JS_SWIG_DEPS})
    set_property(SOURCE js/tttrlib.i PROPERTY USE_SWIG_DEPENDENCIES TRUE)

    SWIG_ADD_LIBRARY(
            ${JS_MODULE_NAME}
            TYPE MODULE
            LANGUAGE javascript
            SOURCES js/tttrlib.i
    )
    tttrlib_link_all_modules(${JS_MODULE_NAME})

    # require() loads "tttrlib.node" -- no "lib" prefix, and the .node suffix is
    # what Node's loader looks for. Node-API's ABI stability means this one
    # binary serves every Node >= 12.17, which is the whole reason for -napi
    # over the V8 backend.
    SET_TARGET_PROPERTIES(${JS_MODULE_NAME} PROPERTIES
            OUTPUT_NAME tttrlib
            PREFIX ""
            SUFFIX ".node"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/js-pkg)

    # NAPI_VERSION 8 is Node 12.22/14.17+ and covers BigInt64Array (needed for
    # macro times) and external ArrayBuffers. NAPI_CPP_EXCEPTIONS is not
    # optional: the generated wrapper's error paths throw Napi::Error, and
    # without it node-addon-api compiles those into abort().
    TARGET_COMPILE_DEFINITIONS(${JS_MODULE_NAME} PRIVATE
            NAPI_VERSION=8
            NAPI_CPP_EXCEPTIONS
            BUILDING_NODE_EXTENSION)

    TARGET_LINK_LIBRARIES(
            ${JS_MODULE_NAME}
            ${HDF5_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
    )
    IF(OpenMP_CXX_FOUND)
        TARGET_LINK_LIBRARIES(${JS_MODULE_NAME} OpenMP::OpenMP_CXX)
    ENDIF()

    # Node resolves the addon's own undefined symbols (napi_*) against the host
    # process at load time; there is no import library to link against, and on
    # macOS the linker must be told so explicitly.
    IF(APPLE)
        SET_TARGET_PROPERTIES(${JS_MODULE_NAME} PROPERTIES
                LINK_FLAGS "-Wl,-undefined,dynamic_lookup")
    ENDIF()

    # The module libraries sit next to the addon in js-pkg/, the same layout the
    # npm package ships, so @loader_path / $ORIGIN finds them.
    tttrlib_set_sibling_rpath(${JS_MODULE_NAME})

    # Keep the SWIG runtime type table private (see the Python block for why).
    TARGET_COMPILE_DEFINITIONS(${JS_MODULE_NAME} PRIVATE SWIG_TYPE_TABLE=tttrlib)

    # Stage the JavaScript package next to the built addon so `node test/js/...`
    # works straight out of the build directory.
    FILE(COPY ${JS_PKG_DIR}/index.js ${JS_PKG_DIR}/index.d.ts ${JS_PKG_DIR}/package.json
         DESTINATION ${CMAKE_BINARY_DIR}/js-pkg)

    UNSET(CMAKE_SWIG_FLAGS)
ENDIF()

IF(BUILD_JAVA_INTERFACE)
    MESSAGE(STATUS "Building Java interface")
    FIND_PACKAGE(JNI REQUIRED)
    INCLUDE_DIRECTORIES(${JNI_INCLUDE_DIRS})

    SET(J_MODULE_NAME tttrlibJava)
    SET(J_PACKAGE io.github.fluorescencetools.tttrlib)
    SET(J_OUTDIR ${CMAKE_BINARY_DIR}/java-pkg/src/main/java/io/github/fluorescencetools/tttrlib)
    FILE(MAKE_DIRECTORY ${J_OUTDIR})

    SET_SOURCE_FILES_PROPERTIES(java/tttrlib.i PROPERTIES
            CPLUSPLUS ON
            SWIG_MODULE_NAME tttrlib)
    SET(CMAKE_SWIG_FLAGS -package ${J_PACKAGE} -I${CMAKE_CURRENT_SOURCE_DIR}/python ${TTTRLIB_SWIG_INT64_FLAG})
    SET(CMAKE_SWIG_OUTDIR ${J_OUTDIR})

    file(GLOB J_SWIG_DEPS
            "${CMAKE_CURRENT_SOURCE_DIR}/python/*.i"
            "${CMAKE_CURRENT_SOURCE_DIR}/java/*.i")
    # See the Python block: SWIG_MODULE_DEPENDS is not read by UseSWIG.
    set(SWIG_MODULE_${J_MODULE_NAME}_EXTRA_DEPS ${J_SWIG_DEPS})
    set_property(SOURCE java/tttrlib.i PROPERTY USE_SWIG_DEPENDENCIES TRUE)

    SWIG_ADD_LIBRARY(
            ${J_MODULE_NAME}
            TYPE MODULE
            LANGUAGE java
            SOURCES java/tttrlib.i
    )
    tttrlib_link_all_modules(${J_MODULE_NAME})
    # System.loadLibrary("tttrlibjni") -> libtttrlibjni.so / tttrlibjni.dll / libtttrlibjni.dylib
    SET_TARGET_PROPERTIES(${J_MODULE_NAME} PROPERTIES
            OUTPUT_NAME tttrlibjni
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/java-pkg/native)
    TARGET_LINK_LIBRARIES(
            ${J_MODULE_NAME}
            ${JNI_LIBRARIES}
            ${HDF5_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
    )
    IF(OpenMP_CXX_FOUND)
        TARGET_LINK_LIBRARIES(${J_MODULE_NAME} OpenMP::OpenMP_CXX)
    ENDIF()

    UNSET(CMAKE_SWIG_FLAGS)
    UNSET(CMAKE_SWIG_OUTDIR)
ENDIF()
