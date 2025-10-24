import os
import sys
import platform
import inspect
from pathlib import Path
from distutils.errors import DistutilsArgError

import setuptools
import cmake_build_extension

ROOT_DIR = Path(__file__).resolve().parent

init_py = inspect.cleandoc(
    """
    from __future__ import annotations

    import importlib
    from types import ModuleType

    __all__ = []  # will be populated after importing compiled bindings


    def _load_bindings() -> ModuleType:
        module = importlib.import_module(
            f"{__name__}.tttrlib"
        )
        return module


    _bindings = _load_bindings()

    for _symbol in dir(_bindings):
        globals()[_symbol] = getattr(_bindings, _symbol)
        if not _symbol.startswith("_"):
            __all__.append(_symbol)


    del _symbol, _bindings, _load_bindings, importlib, ModuleType
    """
)

# Determine the correct Python root directory
# On Windows venvs, sys.executable is in Scripts/, so we need the parent
# On Unix venvs, sys.executable is in bin/, so we need the parent
# For system Python on Windows, sys.executable is in the root, so parent is correct
python_exe_parent = Path(sys.executable).parent
if python_exe_parent.name in ("Scripts", "bin"):
    # Virtual environment - go up one more level
    python_root = python_exe_parent.parent
else:
    # System Python or other layout
    python_root = python_exe_parent

common_cmake_opts = [
    "-DCALL_FROM_SETUP_PY:BOOL=ON",
    "-DBUILD_PYTHON_DOCS:BOOL=OFF",
    "-DBUILD_PYTHON_INTERFACE:BOOL=ON",
    "-DWITH_AVX:BOOL=ON",
    "-DBUILD_R_INTERFACE:BOOL=OFF",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_PHOTON_HDF:BOOL=ON",
    "-DBUILD_LIBRARY:BOOL=OFF",
    f"-DPYTHON_VERSION:STRING={platform.python_version()}",
    f"-DPython_ROOT_DIR:PATH={python_root}",
    "-DCMAKE_C_VISIBILITY_PRESET=hidden",
    "-DCMAKE_CXX_VISIBILITY_PRESET=hidden",
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
]

if sys.platform == "win32":
    # Let CMake choose the generator (will use available VS or Ninja if MSVC env is set)
    CMAKE_GENERATOR = os.environ.get("CMAKE_GENERATOR", None)
    # Ensure /MD (dynamic CRT) to match Python wheels
    common_cmake_opts += [
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
    ]
else:
    CMAKE_GENERATOR = None

# RPATH (unchanged)
if sys.platform == "darwin":
    common_cmake_opts += ["-DCMAKE_INSTALL_RPATH=@loader_path"]
    # Force architecture from environment if CMAKE_OSX_ARCHITECTURES is set
    osx_arch = os.environ.get("CMAKE_OSX_ARCHITECTURES")
    if osx_arch:
        common_cmake_opts.append(f"-DCMAKE_OSX_ARCHITECTURES={osx_arch}")
elif sys.platform.startswith("linux"):
    common_cmake_opts += ["-DCMAKE_INSTALL_RPATH=$ORIGIN"]

setuptools.setup(
    include_package_data=True,
    package_data={"tttrlib": ["*.dll","*.so","*.dylib"]},
    zip_safe=False,
    ext_modules=[
        cmake_build_extension.CMakeExtension(
            name="tttrlib",
            install_prefix="tttrlib",
            write_top_level_init=init_py,
            cmake_configure_options=common_cmake_opts,
            cmake_generator=CMAKE_GENERATOR,
        )
    ],
    cmdclass={"build_ext": cmake_build_extension.BuildExtension},
)