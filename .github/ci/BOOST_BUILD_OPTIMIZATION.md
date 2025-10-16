# Windows Boost Build Optimization

## Overview

The Windows wheel build process has been optimized to significantly reduce Boost build times from **15-20 minutes** down to **2-5 minutes** with vcpkg, or **< 1 minute** with cache hits.

## Optimization Strategies

### 1. vcpkg Package Manager (Primary Method - Fastest!)

**What it does:** Installs pre-built Boost binaries via vcpkg package manager.

**Benefits:**
- **Saves 10-15 minutes** compared to source build
- **2-5 minutes** total install time (first run)
- **< 1 minute** with GitHub Actions cache
- **No admin privileges required**
- Pre-built with MSVC (Visual Studio 2022)
- Automatic dependency handling
- Works on GitHub-hosted Windows runners (vcpkg pre-installed)

**How it works:**
1. Checks if vcpkg is available (via PATH or VCPKG_ROOT)
2. Installs `boost-locale:x64-windows` via `vcpkg install`
3. Copies from vcpkg installed directory to project directory
4. Normalizes directory structure for CMake
5. Falls back to source build if vcpkg unavailable or fails

**Requirements:**
- vcpkg installed (pre-installed on GitHub Actions Windows runners)
- No administrator privileges needed

**Configuration:**
```powershell
# In build_boost_windows.ps1
$UseVcpkg = $true  # Set to $false to force source build
```

### 2. Enhanced Caching Strategy

**What it does:** Caches Chocolatey installations and source builds across CI runs.

**Benefits:**
- **Instant reuse** on cache hits (< 1 minute)
- Caches Chocolatey-installed Boost in `C:\local\boost_*`
- Separate caches for x64 and ARM64 architectures
- Falls back to older cache keys if needed

**Cache locations:**
- `C:\local\boost_*` - Chocolatey-installed Boost
- `dist/cache/` - Downloaded source archives
- `dist/boost-install-x64/` - Built/installed Boost for x64
- `dist/boost-install-arm64/` - Built/installed Boost for ARM64

**Cache key format:**
```yaml
key: boost-choco-${{ runner.os }}-${{ runner.arch }}-v3
```

### 3. Source Build Fallback (When Needed)

The script automatically falls back to building from source if:
- Chocolatey is not available
- Not running with administrator privileges
- Chocolatey installation fails
- ARM64 architecture (no Chocolatey package)
- `$UseChocolatey` is set to `$false`

**Source build optimizations:**
- Uses `b2-nodocs` package (smaller, faster to extract)
- Builds only required components (`--with-locale`)
- Uses `--build-type=minimal` for faster builds
- Parallel compilation with `-j$NUMBER_OF_PROCESSORS`

## Performance Comparison

| Scenario | Time | Description |
|----------|------|-------------|
| **Cache hit** | ~30-60s | Restores from GitHub Actions cache |
| **Chocolatey (no cache)** | ~2-3 min | Installs via Chocolatey package manager |
| **Source build (no cache)** | ~15-20 min | Full download + extract + compile |

## Architecture Support

| Architecture | Chocolatey Support | Fallback |
|--------------|-------------------|----------|
| **x64 (AMD64)** | ✅ Yes (boost-msvc-14.3) | Source build |
| **ARM64** | ❌ No | Source build only |

## Troubleshooting

### Force source build
Set `$UseChocolatey = $false` in the script or run:
```powershell
.github/ci/build_boost_windows.ps1 -ForceBuild
```

### Check if Chocolatey is available
```powershell
choco --version
```

### Install Chocolatey manually
```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
```

### Clear cache
In GitHub Actions, manually delete the cache or change the cache key version number.

### Check what was used
Look for these log messages:
- `"✓ Boost installed successfully via Chocolatey"` - Chocolatey used
- `"Chocolatey requires administrator privileges"` - Need to run as admin
- `"Chocolatey installation failed or unavailable, falling back to source build..."` - Source build used
- `"Boost already installed at: ... (from cache)"` - Cache hit

## Future Improvements

1. **Consider NuGet packages** - Alternative source for pre-built binaries
2. **Parallel architecture builds** - Build x64 and ARM64 in parallel jobs
3. **Minimal component builds** - Further reduce build scope if possible
4. **Docker-based builds** - Pre-configured build environments

## Version Updates

When updating Boost version:
1. Update `$BoostVersion` in `build_boost_windows.ps1`
2. Update cache key in `build-wheels.yml`
3. Verify pre-built binary availability on SourceForge
4. Test both pre-built and source build paths
