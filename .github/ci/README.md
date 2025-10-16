# CI Build Scripts

This directory contains scripts and configuration for building tttrlib across different platforms.

## Boost Configuration

Boost dependencies are managed centrally via `boost-config.txt`.

### Configuration File: `boost-config.txt`

This file lists all required Boost components, one per line:
- **Compiled libraries** (require linking): `locale`, `filesystem`, `thread`, etc.
- **Header-only libraries** (for documentation): `bimap`, `any`, `multi_array`, etc.

Comments start with `#` and empty lines are ignored.

### Platform-Specific Handling

#### Windows (`build_boost_windows_simple.ps1`)
- Reads `boost-config.txt`
- Installs `boost` package (all headers) via vcpkg
- Installs compiled components (e.g., `boost-locale`) individually
- Header-only components are automatically included in the `boost` package

#### Linux (via `pyproject.toml`)
- Installs `boost-devel` package via dnf
- This includes all Boost libraries (headers + compiled)
- Components in `boost-config.txt` serve as documentation

#### macOS (via `pyproject.toml`)
- Installs `boost` package via Homebrew
- This includes all Boost libraries (headers + compiled)
- Components in `boost-config.txt` serve as documentation

## Adding New Boost Dependencies

1. **Update `boost-config.txt`**: Add the component name
2. **Test locally**: Run the appropriate build script
3. **Verify**: Check that CMakeLists.txt includes the component if it's a compiled library

### Example: Adding `boost-filesystem`

```txt
# boost-config.txt
locale
filesystem  # Add this line
bimap
```

If it's a compiled library, also update `CMakeLists.txt`:
```cmake
FIND_PACKAGE(Boost 1.36 REQUIRED COMPONENTS locale filesystem)
```

## Scripts

### `build_boost_windows_simple.ps1`
Simplified Windows Boost installation script using vcpkg.

**Usage:**
```powershell
# Normal build
.\.github\ci\build_boost_windows_simple.ps1

# Force rebuild (ignore cache)
.\.github\ci\build_boost_windows_simple.ps1 -ForceBuild
```

**Features:**
- Reads components from `boost-config.txt`
- Installs via vcpkg (fast, no admin required)
- Copies to `dist/boost-install-{arch}` for caching
- Creates `.boost_env` file for setup.py
- Supports x64 and ARM64 architectures

### `build_boost_windows.ps1` (Legacy)
Original script with source build fallback. Kept for reference but no longer used.

## Troubleshooting

### Windows: vcpkg not found
Ensure vcpkg is in PATH or set `VCPKG_ROOT` environment variable.

### Linux: boost-devel not available
The manylinux_2_28 image includes dnf. For other distros, adjust the package manager command.

### macOS: Homebrew installation fails
Ensure Homebrew is installed and up to date: `brew update`

### Missing Boost component
1. Check if it's listed in `boost-config.txt`
2. For compiled libraries, verify it's in `CMakeLists.txt` FIND_PACKAGE
3. Check build logs for specific error messages
