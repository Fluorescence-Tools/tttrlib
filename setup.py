import os
import sys
import inspect
import setuptools
from pathlib import Path
import cmake_build_extension

# Importing the bindings inside the build_extension_env context manager is necessary only
# in Windows with Python>=3.8.
# See https://github.com/diegoferigo/cmake-build-extension/issues/8.
# Note that if this manager is used in the init file, cmake-build-extension becomes an
# install_requires that must be added to the setup.cfg. Otherwise, cmake-build-extension
# could only be listed as build-system requires in pyproject.toml since it would only
# be necessary for packaging and not during runtime.
init_py = inspect.cleandoc(
    """
    import cmake_build_extension
    with cmake_build_extension.build_extension_env():
        from . import bindings
    """
)

setuptools.setup(
    ext_modules=[
        cmake_build_extension.CMakeExtension(
            name="tttrlib",
            install_prefix="tttrlib",
            write_top_level_init="from . tttrlib import *",
            cmake_configure_options=[
                # Select the bindings implementation
                "-DCALL_FROM_SETUP_PY:BOOL=ON",
                "-DBUILD_PYTHON_DOCS:BOOL=OFF",
                "-DBUILD_PYTHON_INTERFACE:BOOL=ON",
                "-DWITH_AVX:BOOL=ON",
                "-DBUILD_R_INTERFACE:BOOL=OFF",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_PHOTON_HDF:BOOL=OFF",
                "-DBUILD_ICS:BOOL=OFF",
                "-DBUILD_LIBRARY:BOOL=OFF",
                "-DCMAKE_CXX_FLAGS='-w'"
            ]
        )
    ],
       cmdclass=dict(
        # Enable the CMakeExtension entries defined above
        build_ext=cmake_build_extension.BuildExtension,
    ),
)
