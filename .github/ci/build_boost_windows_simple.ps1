# Simplified Boost installation script using vcpkg
# This script installs Boost via vcpkg and sets up the environment for cibuildwheel

param([switch]$ForceBuild)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0
$ProgressPreference = 'SilentlyContinue'

# -------------------
# Configuration
# -------------------
# Detect architecture
$TargetArch = if ($env:CIBW_ARCHS_WINDOWS) { $env:CIBW_ARCHS_WINDOWS } else { $env:PROCESSOR_ARCHITECTURE }
$Triplet = if ($TargetArch -eq "ARM64") { "arm64-windows" } else { "x64-windows" }
$ArchSuffix = if ($TargetArch -eq "ARM64") { "-arm64" } else { "-x64" }

Write-Host "Building for Windows $TargetArch (triplet: $Triplet)"

# -------------------
# Paths
# -------------------
try { $ProjRoot = (& git rev-parse --show-toplevel 2>$null) } catch { $ProjRoot = $null }
if (-not $ProjRoot) {
    $ProjRoot = if ($PSScriptRoot) { Split-Path -Parent (Split-Path -Parent $PSScriptRoot) } else { (Get-Location).Path }
}

$DistRoot = Join-Path $ProjRoot "dist"
$BoostInstall = Join-Path $DistRoot "boost-install$ArchSuffix"
$BoostMarker = Join-Path $BoostInstall ".boost_installed.ok"

New-Item -ItemType Directory -Force -Path $DistRoot, $BoostInstall | Out-Null

# -------------------
# Check if already installed (from cache)
# -------------------
if ((Test-Path $BoostMarker) -and -not $ForceBuild) {
    Write-Host "Boost already installed at: $BoostInstall (from cache)"
} else {
    # -------------------
    # Find vcpkg
    # -------------------
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if (-not $vcpkgCmd) {
        if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
            $vcpkgCmd = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
        } else {
            throw "vcpkg not found. Install vcpkg or set VCPKG_ROOT environment variable."
        }
    } else {
        $vcpkgCmd = $vcpkgCmd.Source
    }

    Write-Host "Using vcpkg: $vcpkgCmd"

    # -------------------
    # Install Boost via vcpkg
    # -------------------
    Write-Host "Installing boost-locale:$Triplet via vcpkg..."
    Write-Host "This may take 2-5 minutes on first install..."
    
    & $vcpkgCmd install "boost-locale:$Triplet" --recurse
    
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed with exit code $LASTEXITCODE"
    }

    # -------------------
    # Find vcpkg installed directory
    # -------------------
    $vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Split-Path $vcpkgCmd -Parent }
    $vcpkgInstalled = Join-Path $vcpkgRoot "installed\$Triplet"
    
    if (-not (Test-Path $vcpkgInstalled)) {
        throw "vcpkg installed directory not found: $vcpkgInstalled"
    }
    
    Write-Host "Found vcpkg Boost at: $vcpkgInstalled"

    # -------------------
    # Copy to project location
    # -------------------
    Write-Host "Copying Boost to: $BoostInstall"
    
    # Clean destination
    if (Test-Path $BoostInstall) {
        Remove-Item -Recurse -Force $BoostInstall -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Force -Path $BoostInstall | Out-Null
    
    # Copy directories
    foreach ($dir in @("include", "lib", "bin")) {
        $srcDir = Join-Path $vcpkgInstalled $dir
        if (Test-Path $srcDir) {
            Copy-Item -Recurse -Force $srcDir $BoostInstall
            Write-Host "  Copied $dir"
        }
    }
    
    # Verify installation
    $includeDir = Join-Path $BoostInstall "include\boost"
    $libDir = Join-Path $BoostInstall "lib"
    
    if (-not (Test-Path $includeDir)) {
        throw "Boost headers not found at: $includeDir"
    }
    if (-not (Test-Path $libDir)) {
        throw "Boost libraries not found at: $libDir"
    }
    
    # Create marker file
    New-Item -ItemType File -Path $BoostMarker -Force | Out-Null
    Write-Host "Boost installed successfully"
}

# -------------------
# Set up environment variables
# -------------------
$env:BOOST_ROOT = $BoostInstall
$env:BOOST_INCLUDEDIR = Join-Path $BoostInstall "include"
$env:BOOST_LIBRARYDIR = Join-Path $BoostInstall "lib"
$env:CMAKE_PREFIX_PATH = $BoostInstall
$env:Path = "$($env:BOOST_LIBRARYDIR);$($env:Path)"

# Find Boost CMake config directory
$CmakeDir = Join-Path $BoostInstall "lib\cmake"
if (Test-Path $CmakeDir) {
    $BoostCmakeDir = Get-ChildItem -Path $CmakeDir -Directory -Filter "Boost-*" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $BoostCmakeDir) {
        $BoostCmakeDir = Get-ChildItem -Path $CmakeDir -Directory -Filter "boost*" -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($BoostCmakeDir) {
        $env:Boost_DIR = $BoostCmakeDir.FullName
    }
}

# -------------------
# Write .boost_env file for setup.py
# -------------------
$EnvFile = Join-Path $ProjRoot ".boost_env"
$EnvContent = @"
BOOST_ROOT=$($env:BOOST_ROOT)
BOOST_INCLUDEDIR=$($env:BOOST_INCLUDEDIR)
BOOST_LIBRARYDIR=$($env:BOOST_LIBRARYDIR)
CMAKE_PREFIX_PATH=$($env:CMAKE_PREFIX_PATH)
BOOST_LIB_PATH=$($env:BOOST_LIBRARYDIR)
"@

if ($env:Boost_DIR) {
    $EnvContent += "`nBoost_DIR=$($env:Boost_DIR)"
}

Set-Content -Path $EnvFile -Value $EnvContent -Encoding UTF8
Write-Host "Wrote Boost environment to: $EnvFile"

# -------------------
# Export to GitHub Actions environment
# -------------------
if ($env:GITHUB_ENV) {
    Write-Host "Writing environment variables to GITHUB_ENV..."
    Add-Content -Path $env:GITHUB_ENV -Value "BOOST_ROOT=$($env:BOOST_ROOT)"
    Add-Content -Path $env:GITHUB_ENV -Value "BOOST_INCLUDEDIR=$($env:BOOST_INCLUDEDIR)"
    Add-Content -Path $env:GITHUB_ENV -Value "BOOST_LIBRARYDIR=$($env:BOOST_LIBRARYDIR)"
    Add-Content -Path $env:GITHUB_ENV -Value "CMAKE_PREFIX_PATH=$($env:CMAKE_PREFIX_PATH)"
    if ($env:Boost_DIR) {
        Add-Content -Path $env:GITHUB_ENV -Value "Boost_DIR=$($env:Boost_DIR)"
    }
}

# -------------------
# Summary
# -------------------
Write-Host ""
Write-Host "Boost setup complete:"
Write-Host "  BOOST_ROOT        = $($env:BOOST_ROOT)"
Write-Host "  BOOST_INCLUDEDIR  = $($env:BOOST_INCLUDEDIR)"
Write-Host "  BOOST_LIBRARYDIR  = $($env:BOOST_LIBRARYDIR)"
Write-Host "  CMAKE_PREFIX_PATH = $($env:CMAKE_PREFIX_PATH)"
if ($env:Boost_DIR) {
    Write-Host "  Boost_DIR         = $($env:Boost_DIR)"
}
