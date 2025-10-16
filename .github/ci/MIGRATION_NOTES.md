# Boost Build Script Simplification

## Summary

The Windows Boost build script has been simplified from **685 lines** to **~170 lines** (75% reduction) by focusing exclusively on vcpkg installation.

## Changes Made

### 1. New Simplified Script
- **File**: `.github/ci/build_boost_windows_simple.ps1`
- **Purpose**: Install Boost via vcpkg only (no fallback to source builds)
- **Benefits**:
  - Much easier to read and maintain
  - Faster execution (no complex fallback logic)
  - Still supports caching
  - Still creates `.boost_env` file for setup.py

### 2. Updated Configuration
- **pyproject.toml**: Updated `before-all` to use new script
- **build-wheels.yml**: Updated cache key to `v2` (removed `dist/cache`)

### 3. What Was Removed
- ❌ Source build fallback (downloading and building Boost from source)
- ❌ Pre-built binary support (SourceForge installers)
- ❌ Complex mirror selection logic
- ❌ Archive extraction caching
- ❌ 7-Zip/tar extraction logic

### 4. What Was Kept
- ✅ vcpkg installation (primary method)
- ✅ Architecture detection (x64/ARM64)
- ✅ Caching support (via `.boost_installed.ok` marker)
- ✅ `.boost_env` file generation
- ✅ Environment variable setup
- ✅ GitHub Actions integration

## Migration Path

### If vcpkg Fails
Since there's no fallback, you have two options:

1. **Fix vcpkg** (recommended)
   - Ensure vcpkg is available on the runner
   - Check network connectivity
   - Verify triplet is correct

2. **Revert to old script** (temporary)
   ```toml
   before-all = "powershell -ExecutionPolicy Bypass -File .github/ci/build_boost_windows.ps1"
   ```

## Testing

To test locally:
```powershell
# From project root
.\.github\ci\build_boost_windows_simple.ps1

# Force rebuild
.\.github\ci\build_boost_windows_simple.ps1 -ForceBuild
```

## Rollback

If you need to rollback:

1. Update `pyproject.toml`:
   ```toml
   before-all = "powershell -ExecutionPolicy Bypass -File .github/ci/build_boost_windows.ps1"
   ```

2. Update cache key in `build-wheels.yml`:
   ```yaml
   key: boost-vcpkg-${{ runner.os }}-${{ runner.arch }}-v1
   ```

The old script (`build_boost_windows.ps1`) is still in the repository and can be used if needed.
