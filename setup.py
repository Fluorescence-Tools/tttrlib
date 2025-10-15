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
    f"-DPython_ROOT_DIR:PATH={Path(sys.executable).parent}",
    "-DBoost_USE_STATIC_LIBS:BOOL=OFF",        # dynamic Boost
    "-DCMAKE_C_VISIBILITY_PRESET=hidden",
    "-DCMAKE_CXX_VISIBILITY_PRESET=hidden",
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
]

if sys.platform == "win32":
    # Let CMake choose the generator (will use available VS or Ninja if MSVC env is set)
    CMAKE_GENERATOR = os.environ.get("CMAKE_GENERATOR", None)
    # The Boost-related environment variables are set by cibuildwheel and will be
    # automatically picked up by CMake's FindBoost module.
    # Avoid picking system Boost
    common_cmake_opts += [
        "-DBoost_NO_SYSTEM_PATHS:BOOL=ON",
        "-DBoost_DEBUG:BOOL=ON",
        # Ensure /MD (dynamic CRT) to match Python wheels
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"
    ]
    # Pass Boost paths from environment if set (for BoostConfig.cmake)
    boost_dir = os.environ.get("Boost_DIR")
    boost_root = os.environ.get("BOOST_ROOT")
    cmake_prefix = os.environ.get("CMAKE_PREFIX_PATH")
    
    # Check if cibuildwheel placeholders weren't expanded (they contain {project})
    if boost_dir and "{project}" in boost_dir:
        boost_dir = None
    if boost_root and "{project}" in boost_root:
        boost_root = None
    if cmake_prefix and "{project}" in cmake_prefix:
        cmake_prefix = None
    
    # Try reading from .boost_env file written by build script
    if not boost_root or not boost_dir:
        boost_env_file = ROOT_DIR / ".boost_env"
        if boost_env_file.exists():
            print(f"[setup.py] Reading Boost paths from {boost_env_file}")
            with open(boost_env_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("BOOST_ROOT=") and not boost_root:
                        boost_root = line.split("=", 1)[1]
                    elif line.startswith("Boost_DIR=") and not boost_dir:
                        boost_dir = line.split("=", 1)[1]
                    elif line.startswith("CMAKE_PREFIX_PATH=") and not cmake_prefix:
                        cmake_prefix = line.split("=", 1)[1]
    
    # Fallback: construct paths from project root if env vars not set or not expanded
    if not boost_root:
        # Detect architecture for architecture-specific Boost install directory
        arch = platform.machine().upper()
        if arch in ("AMD64", "X86_64"):
            arch_suffix = "-x64"
        elif arch == "ARM64":
            arch_suffix = "-arm64"
        else:
            arch_suffix = ""
        
        boost_install = ROOT_DIR / "dist" / f"boost-install{arch_suffix}"
        if boost_install.exists():
            boost_root = str(boost_install)
            print(f"[setup.py] Using fallback BOOST_ROOT: {boost_root}")
        elif arch_suffix:
            # Try without suffix as fallback
            boost_install_generic = ROOT_DIR / "dist" / "boost-install"
            if boost_install_generic.exists():
                boost_root = str(boost_install_generic)
                print(f"[setup.py] Using fallback BOOST_ROOT (generic): {boost_root}")
    
    if not boost_dir and boost_root:
        # Try to find the Boost CMake config directory
        boost_cmake = Path(boost_root) / "lib" / "cmake" / "Boost-1.86.0"
        if boost_cmake.exists():
            boost_dir = str(boost_cmake)
            print(f"[setup.py] Using fallback Boost_DIR: {boost_dir}")
    
    print(f"[setup.py] Boost_DIR: {boost_dir}")
    print(f"[setup.py] BOOST_ROOT: {boost_root}")
    print(f"[setup.py] CMAKE_PREFIX_PATH: {cmake_prefix}")
    
    if boost_dir:
        # Convert to forward slashes for CMake
        boost_dir = str(Path(boost_dir)).replace("\\", "/")
        common_cmake_opts.append(f"-DBoost_DIR:PATH={boost_dir}")
    if boost_root:
        boost_root = str(Path(boost_root)).replace("\\", "/")
        common_cmake_opts.append(f"-DBoost_ROOT:PATH={boost_root}")
    if cmake_prefix:
        # Handle multiple paths separated by semicolon
        cmake_prefix = ";".join(str(Path(p.strip())).replace("\\", "/") for p in cmake_prefix.split(";"))
        common_cmake_opts.append(f"-DCMAKE_PREFIX_PATH:PATH={cmake_prefix}")
    elif boost_root:
        # Fallback: set CMAKE_PREFIX_PATH from boost_root
        boost_root_fwd = str(Path(boost_root)).replace("\\", "/")
        common_cmake_opts.append(f"-DCMAKE_PREFIX_PATH:PATH={boost_root_fwd}")
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