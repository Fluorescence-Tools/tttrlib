#! /usr/bin/env python

import os
import sys
import platform
import subprocess

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


def read_version(header_file):
    version = "0.0.0"
    with open(header_file, "r") as fp:
        for line in fp.readlines():
            if "#define" in line and "TTTRLIB_VERSION" in line:
                version = line.split()[-1]
    return version.replace('"', '')


class CMakeExtension(Extension):

    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


def build_swig_documentation():
    # build the documentation.i file using doxygen and doxy2swig
    if not os.path.isfile("./ext/python/documentation.i"):
        print("-- building documentation.i using doxygen and doxy2swig")
        path = os.path.dirname(os.path.abspath(__file__)) 
        env = os.environ.copy()
        subprocess.check_call(["doxygen"], cwd=path + "/doc", env=env)
        subprocess.check_call(
            ["python", "doxy2swig.py", "../doc/_build/xml/index.xml", "../ext/python/documentation.i"],
            cwd= path + "/build_tools",
            env=env
        )


class CMakeBuild(build_ext):

    def run(self):
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(
            os.path.dirname(
                self.get_ext_fullpath(ext.name)
            )
        ).replace('\\', '/')

        cmake_args = [
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + extdir,
            '-DCMAKE_SWIG_OUTDIR=' + extdir
        ]

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]
        cmake_args += ['-DCMAKE_BUILD_TYPE=' + cfg]
        if platform.system() == "Windows":
            cmake_args += [
                '-DBUILD_PYTHON_INTERFACE=ON',
                '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}'.format(cfg.upper(), extdir),
                '-GVisual Studio 14 2015 Win64'
            ]
        else:
            build_args += ['--', '-j8']

        env = os.environ.copy()
        env['CXXFLAGS'] = '{} -DVERSION_INFO=\\"{}\\"'.format(
            env.get(
                'CXXFLAGS', ''
            ),
            self.distribution.get_version()
        )
        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        print("BUILDING: " + " ".join(cmake_args))
        subprocess.check_call(
            ['cmake', ext.sourcedir] + cmake_args,
            cwd=self.build_temp,
            env=env
        )
        subprocess.check_call(
            ['cmake', '--build', '.'] + build_args,
            cwd=self.build_temp
        )


NAME = "tttrlib"
DESCRIPTION = "tttrlib read/process/write TTTR data"
LONG_DESCRIPTION = """tttrlib is a C++ library with Python wrappers to \
read, write and process time-tagged time resolved data."""
VERSION = read_version(
    os.path.dirname(os.path.abspath(__file__)) + '/include/info.h'
)
print("TTTRLIB VERSION:", VERSION)
build_swig_documentation()
LICENSE = 'BSD 3-Clause License'
build_swig_documentation()
setup(
    name=NAME,
    version=VERSION,
    license=LICENSE,
    author='Thomas-Otavio Peulen',
    author_email='thomas@peulen.xyz',
    ext_modules=[
        CMakeExtension('tttrlib')
    ],
    cmdclass={
        'build_ext': CMakeBuild
    },
    description=DESCRIPTION,
    long_description=LONG_DESCRIPTION,
    install_requires=[
        'numpy'
    ],
    setup_requires=[
        'setuptools'
    ],
    zip_safe=False,
    classifiers=[
        'Development Status :: 2 - Pre-Alpha',
        'Intended Audience :: Science/Research',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Operating System :: Microsoft :: Windows',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: Python',
        'Topic :: Scientific/Engineering',
    ]
)
