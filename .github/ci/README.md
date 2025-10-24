# CI Build Configuration

This directory contains configuration for building tttrlib across different platforms.

## Build Configuration

All build configuration is managed via:
- `pyproject.toml` - cibuildwheel configuration
- `CMakeLists.txt` - CMake build configuration

## Platform-Specific Details

### Windows
- Uses Visual Studio 17 2022 generator
- Requires: CMake, Python, SWIG, Ninja

### Linux
- Uses manylinux_2_28 image
- Requires: CMake, Python, SWIG, Ninja
- Package manager: dnf

### macOS
- Requires: CMake, Python, SWIG, Ninja
- Package manager: Homebrew

## System Dependencies

Minimal system dependencies:
- **CMake** - build configuration
- **Python** - development headers
- **SWIG** - Python bindings generation
- **Ninja** - build system

All other dependencies are header-only:
- **HighFive** - HDF5 interface (header-only)
- **tttrlib::bimap** - Boost bimap replacement (header-only)
- **tttrlib::string_encoding** - String encoding utilities (header-only)

## Build Process

1. **Configure**: CMake generates build files
2. **Build**: Ninja compiles C++ code
3. **Bind**: SWIG generates Python bindings
4. **Test**: pytest runs unit tests
5. **Package**: cibuildwheel creates wheels

## Troubleshooting

### Build Failures
1. Check CMakeLists.txt for configuration errors
2. Verify Python and SWIG versions
3. Check platform-specific build logs

### Missing Dependencies
Ensure all required tools are installed:
```bash
cmake --version
python --version
swig -version
```

## Historical Notes

- `MIGRATION_NOTES.md` - Contains historical build migration information
- Boost and HDF5 system dependencies have been removed (using header-only alternatives)
