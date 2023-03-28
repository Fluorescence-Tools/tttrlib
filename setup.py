#! /usr/bin/env python
import os
import sys
import inspect
from pathlib import Path
import cmake_build_extension
import setuptools

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

def read_version(header_file):
    version = "0.0.0"
    with open(header_file, "r") as fp:
        for line in fp.readlines():
            if "#define" in line and "TTTRLIB_VERSION" in line:
                version = line.split()[-1]
    return version.replace('"', '')


NAME = "tttrlib"
VERSION = read_version(os.path.dirname(os.path.abspath(__file__)) + '/include/info.h')
LICENSE = 'BSD 3-Clause License'

setuptools.setup(
    name=NAME,
    version=VERSION,
    author='Thomas-Otavio Peulen',
    author_email='thomas@peulen.xyz',
    ext_modules=[
        cmake_build_extension.CMakeExtension(
            name="tttrlib",
            install_prefix="tttrlib",
            cmake_configure_options=[
                # Select the bindings implementation
                "-DCALL_FROM_SETUP_PY:BOOL=ON",
                "-DEXAMPLE_WITH_SWIG:BOOL=ON",
                "-DBUILD_PYTHON_DOCS:BOOL=OFF",
                "-DBUILD_PYTHON_INTERFACE:BOOL=ON",
                "-DWITH_AVX:BOOL=ON",
                "-DBUILD_R_INTERFACE:BOOL=OFF",
                "-DCMAKE_BUILD_TYPE=Release"
            ]
        )
    ],
       cmdclass=dict(
        # Enable the CMakeExtension entries defined above
        build_ext=cmake_build_extension.BuildExtension,
        # Pack the whole git folder:
        sdist=cmake_build_extension.GitSdistTree,
    ),
)

