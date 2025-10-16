# .github/ci/build_boost_windows.ps1 (PowerShell 5 compatible)
param([switch]$ForceBuild)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0
$ProgressPreference = 'SilentlyContinue'

# -------------------
# Config
# -------------------
$BoostVersion       = "1.86.0"
$BoostVersionUnderscore = $BoostVersion.Replace(".", "_")
$MinArchiveSizeMB   = 10

# Try package managers first (fastest), then fall back to source build
$UseVcpkg = $true  # Use vcpkg if available (fastest method, no admin required)

# Use source package with b2 pre-built (faster than full source)
# Prefer .zip over .7z for more reliable extraction on Windows runners
$AssetNames = @(
    "boost-$BoostVersion-b2-nodocs.zip",
    "boost-$BoostVersion-b2-nodocs.7z"
)

# Build configuration
$NeededComponents   = @("--with-locale")
$Variant            = "release"
$LinkType           = "shared"
$RuntimeLink        = "shared"

# Detect target architecture from environment (set by cibuildwheel)
$TargetArch = if ($env:CIBW_ARCHS_WINDOWS) { $env:CIBW_ARCHS_WINDOWS } else { $env:PROCESSOR_ARCHITECTURE }
if ($TargetArch -eq "ARM64") {
    $AddressModel = "64"
    $Architecture = "arm"
    Write-Host "Building for Windows ARM64"
} else {
    $AddressModel = "64"
    $Architecture = "x86"
    Write-Host "Building for Windows x64 (AMD64)"
}

# -------------------
# Paths (repo root if possible)
# -------------------
$ProjRoot = $null
try { $gitRoot = (& git rev-parse --show-toplevel) 2>$null } catch { $gitRoot = $null }
if ($gitRoot) { $ProjRoot = $gitRoot }
elseif ($PSScriptRoot) { $ProjRoot = Split-Path -Parent $PSScriptRoot }
else { $ProjRoot = (Get-Location).Path }

$DistRoot      = Join-Path $ProjRoot "dist"
$CacheRoot     = Join-Path $DistRoot "cache"
# Make build directories architecture-specific to avoid conflicts
$ArchSuffix    = if ($TargetArch -eq "ARM64") { "-arm64" } else { "-x64" }
$BoostSrcRoot  = Join-Path $DistRoot "boost-src$ArchSuffix"
$BoostInstall  = Join-Path $DistRoot "boost-install$ArchSuffix"

New-Item -ItemType Directory -Force -Path $DistRoot,$CacheRoot,$BoostSrcRoot,$BoostInstall | Out-Null

# -------------------
# Locals override (unless forced)
# -------------------
if (-not $ForceBuild -and $env:BOOST_ROOT -and (Test-Path (Join-Path $env:BOOST_ROOT "include"))) {
    Write-Host "Using local Boost at $env:BOOST_ROOT"
    $env:BOOST_INCLUDEDIR  = Join-Path $env:BOOST_ROOT "include"
    $env:BOOST_LIBRARYDIR  = Join-Path $env:BOOST_ROOT "lib"
    $env:CMAKE_PREFIX_PATH = $env:BOOST_ROOT
    $env:Path              = "$($env:BOOST_LIBRARYDIR);$($env:Path)"
    Write-Host "BOOST_INCLUDEDIR  = $env:BOOST_INCLUDEDIR"
    Write-Host "BOOST_LIBRARYDIR  = $env:BOOST_LIBRARYDIR"
    Write-Host "CMAKE_PREFIX_PATH = $env:CMAKE_PREFIX_PATH"
    return
}

# -------------------
# vcpkg installation (fastest method, no admin required)
# -------------------
function Install-BoostViaVcpkg {
    param([string]$installPath, [string]$targetArch)
    
    Write-Host "Installing Boost via vcpkg..."
    
    # Check if vcpkg is available
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if (-not $vcpkgCmd) {
        Write-Host "vcpkg not found in PATH, checking VCPKG_ROOT..."
        if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
            $vcpkgCmd = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
            Write-Host "Found vcpkg at: $vcpkgCmd"
        } else {
            Write-Host "vcpkg not available (set VCPKG_ROOT or add to PATH)"
            return $false
        }
    } else {
        $vcpkgCmd = $vcpkgCmd.Source
    }
    
    try {
        # Determine triplet based on architecture
        $triplet = if ($targetArch -eq "ARM64") { "arm64-windows" } else { "x64-windows" }
        
        Write-Host "Installing boost-locale:$triplet via vcpkg..."
        Write-Host "This may take 2-5 minutes on first install..."
        
        # Install only the components we need
        & $vcpkgCmd install "boost-locale:$triplet" --recurse
        
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "vcpkg install failed with exit code $LASTEXITCODE"
            return $false
        }
        
        # Find vcpkg installed directory
        $vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Split-Path (Split-Path $vcpkgCmd -Parent) -Parent }
        $vcpkgInstalled = Join-Path $vcpkgRoot "installed\$triplet"
        
        if (-not (Test-Path $vcpkgInstalled)) {
            Write-Warning "vcpkg installed directory not found: $vcpkgInstalled"
            return $false
        }
        
        Write-Host "Found vcpkg Boost at: $vcpkgInstalled"
        
        # Copy to our expected location
        if (Test-Path $installPath) { 
            Remove-Item -Recurse -Force $installPath -ErrorAction SilentlyContinue 
        }
        New-Item -ItemType Directory -Force -Path $installPath | Out-Null
        
        Write-Host "Copying Boost to: $installPath"
        
        # Copy include directory
        $vcpkgInclude = Join-Path $vcpkgInstalled "include"
        if (Test-Path $vcpkgInclude) {
            Copy-Item -Recurse -Force $vcpkgInclude $installPath
        }
        
        # Copy lib directory
        $vcpkgLib = Join-Path $vcpkgInstalled "lib"
        if (Test-Path $vcpkgLib) {
            Copy-Item -Recurse -Force $vcpkgLib $installPath
        }
        
        # Copy bin directory (DLLs)
        $vcpkgBin = Join-Path $vcpkgInstalled "bin"
        if (Test-Path $vcpkgBin) {
            Copy-Item -Recurse -Force $vcpkgBin $installPath
        }
        
        # Verify installation
        $includeDir = Join-Path $installPath "include\boost"
        $libDir = Join-Path $installPath "lib"
        
        if ((Test-Path $includeDir) -and (Test-Path $libDir)) {
            Write-Host "SUCCESS: Boost installed successfully via vcpkg"
            return $true
        } else {
            Write-Warning "vcpkg Boost installation incomplete"
            Write-Host "Include dir: $includeDir (exists: $(Test-Path $includeDir))"
            Write-Host "Lib dir: $libDir (exists: $(Test-Path $libDir))"
            return $false
        }
        
    } catch {
        Write-Warning "Failed to install Boost via vcpkg: $($_.Exception.Message)"
        return $false
    }
}

# -------------------
# Pre-built binary download (Windows only) - DEPRECATED
# -------------------
function Get-PrebuiltBoost {
    param([string]$version, [string]$arch, [string]$cacheRoot, [string]$installPath)
    
    $versionUnderscore = $version.Replace(".", "_")
    
    # SourceForge pre-built binaries (MSVC 14.3 / VS 2022)
    # These include both debug and release, shared and static libs
    $prebuiltName = if ($arch -eq "ARM64") {
        # ARM64 pre-built binaries are not commonly available
        return $null
    } else {
        "boost_$versionUnderscore-msvc-14.3-64.exe"  # Self-extracting installer
    }
    
    # Multiple download sources
    $prebuiltUrls = @(
        "https://downloads.sourceforge.net/project/boost/boost-binaries/$version/$prebuiltName",
        "https://sourceforge.net/projects/boost/files/boost-binaries/$version/$prebuiltName/download"
    )
    
    $prebuiltFile = Join-Path $cacheRoot $prebuiltName
    
    # Check if already cached
    if (Test-Path $prebuiltFile) {
        $size = (Get-Item $prebuiltFile).Length / 1MB
        if ($size -gt 50) {
            $sizeMB = [math]::Round($size, 2)
            Write-Host "Using cached pre-built binary: $prebuiltFile - $sizeMB MB"
            return $prebuiltFile
        } else {
            Write-Warning "Cached file too small, re-downloading..."
            Remove-Item $prebuiltFile -Force -ErrorAction SilentlyContinue
        }
    }
    
    # Try to download - Start-BitsTransfer works best for SourceForge redirects
    foreach ($url in $prebuiltUrls) {
        try {
            Write-Host "Downloading pre-built Boost from: $url"
            Write-Host "This may take a few minutes - 200+ MB..."
            
            # Try Start-BitsTransfer first (best for SourceForge)
            try {
                Start-BitsTransfer -Source $url -Destination $prebuiltFile -ErrorAction Stop
            } catch {
                # Fallback to Invoke-WebRequest
                Write-Host "Trying alternate download method..."
                Invoke-WebRequest -Uri $url -OutFile $prebuiltFile -MaximumRedirection 20 -UseBasicParsing -UserAgent "Mozilla/5.0"
            }
            
            if (Test-Path $prebuiltFile) {
                $size = (Get-Item $prebuiltFile).Length / 1MB
                if ($size -gt 50) {  # Pre-built should be >50MB
                    $sizeMB = [math]::Round($size, 2)
                    Write-Host "Downloaded pre-built binary: $prebuiltFile - $sizeMB MB"
                    return $prebuiltFile
                } else {
                    $sizeMB = [math]::Round($size, 2)
                    Write-Warning "Downloaded file too small - $sizeMB MB, trying next URL..."
                    Remove-Item $prebuiltFile -Force -ErrorAction SilentlyContinue
                }
            }
        } catch {
            Write-Warning "Failed to download from $url : $($_.Exception.Message)"
            if (Test-Path $prebuiltFile) {
                Remove-Item $prebuiltFile -Force -ErrorAction SilentlyContinue
            }
        }
    }
    
    return $null
}

function Install-PrebuiltBoost {
    param([string]$installerPath, [string]$installPath)
    
    Write-Host "Extracting pre-built Boost installer..."
    
    try {
        # The .exe is an NSIS installer - extract it with 7-Zip
        # 7-Zip can extract NSIS installers directly
        $SevenZip = "$env:ProgramFiles\7-Zip\7z.exe"
        if (-not (Test-Path $SevenZip)) { 
            $SevenZip = "${env:ProgramFiles(x86)}\7-Zip\7z.exe"
        }
        if (-not (Test-Path $SevenZip)) { 
            $SevenZip = "7z.exe"
        }
        
        $Has7z = (Get-Command $SevenZip -ErrorAction SilentlyContinue) -ne $null
        if (-not $Has7z) {
            Write-Warning "7-Zip not found, cannot extract pre-built installer"
            return $false
        }
        
        # Create temp extraction location
        $tempInstall = Join-Path $env:TEMP "boost_install_temp"
        if (Test-Path $tempInstall) { Remove-Item -Recurse -Force $tempInstall -ErrorAction SilentlyContinue }
        New-Item -ItemType Directory -Force -Path $tempInstall | Out-Null
        
        # Extract the NSIS installer with 7-Zip
        Write-Host "Extracting with 7-Zip (this may take 2-3 minutes)..."
        & $SevenZip x -y "-o$tempInstall" $installerPath | Out-Null
        
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "7-Zip extraction failed with exit code $LASTEXITCODE"
            return $false
        }
        
        # Wait a moment for extraction to complete
        Start-Sleep -Seconds 1
        
        # Find the boost root in installed files (usually boost_1_86_0 or similar)
        $boostDirs = Get-ChildItem -Path $tempInstall -Directory -Filter "boost_*" -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $boostDirs) {
            # Maybe it installed directly without subdirectory
            if (Test-Path (Join-Path $tempInstall "boost")) {
                $extractedBoost = $tempInstall
            } else {
                Write-Warning "Could not find boost directory in installed files"
                Write-Host "Contents of $tempInstall :"
                Get-ChildItem $tempInstall | ForEach-Object { Write-Host "  $_" }
                return $false
            }
        } else {
            $extractedBoost = $boostDirs.FullName
        }
        
        # Copy to final install location
        if (Test-Path $installPath) { Remove-Item -Recurse -Force $installPath -ErrorAction SilentlyContinue }
        Copy-Item -Recurse -Force $extractedBoost $installPath
        
        # Clean up temp
        Remove-Item -Recurse -Force $tempInstall -ErrorAction SilentlyContinue
        
        # Verify installation - check for headers and libs
        $includeDir = Join-Path $installPath "boost"
        if (-not (Test-Path $includeDir)) {
            $includeDir = Join-Path $installPath "include\boost"
        }
        
        # Find lib directory (could be lib64-msvc-14.3, lib, or lib64)
        $libDir = $null
        foreach ($libName in @("lib64-msvc-14.3", "lib", "lib64", "stage\lib")) {
            $testPath = Join-Path $installPath $libName
            if (Test-Path $testPath) {
                $libDir = $testPath
                break
            }
        }
        
        if ((Test-Path $includeDir) -and $libDir -and (Test-Path $libDir)) {
            # Normalize lib directory name to just "lib" if needed
            $normalizedLibDir = Join-Path $installPath "lib"
            if ($libDir -ne $normalizedLibDir) {
                if (Test-Path $normalizedLibDir) { Remove-Item -Recurse -Force $normalizedLibDir -ErrorAction SilentlyContinue }
                Copy-Item -Recurse -Force $libDir $normalizedLibDir
            }
            
            # Normalize include directory if needed
            $normalizedIncludeDir = Join-Path $installPath "include"
            if (-not (Test-Path $normalizedIncludeDir)) {
                New-Item -ItemType Directory -Force -Path $normalizedIncludeDir | Out-Null
                if ($includeDir -ne (Join-Path $normalizedIncludeDir "boost")) {
                    Copy-Item -Recurse -Force (Split-Path $includeDir -Parent) $normalizedIncludeDir
                }
            }
            
            Write-Host "Pre-built Boost installed successfully at: $installPath"
            return $true
        } else {
            Write-Warning "Pre-built Boost installation incomplete"
            Write-Host "Include dir: $includeDir (exists: $(Test-Path $includeDir))"
            Write-Host "Lib dir: $libDir (exists: $(if($libDir){Test-Path $libDir}else{'null'}))"
            return $false
        }
    } catch {
        Write-Warning "Failed to install pre-built Boost: $($_.Exception.Message)"
        return $false
    }
}

# -------------------
# Download setup
# -------------------
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Preferred mirror (PS5 doesn't have ??)
$Preferred = ""
if ($env:BOOST_MIRROR) { $Preferred = $env:BOOST_MIRROR.ToLower() }

function New-Candidates([string]$asset) {
    @(
        @{ url="https://github.com/boostorg/boost/releases/download/boost-$BoostVersion/$asset"; file=(Join-Path $CacheRoot $asset) },
        @{ url="https://downloads.sourceforge.net/project/boost/boost/$BoostVersion/$asset";    file=(Join-Path $CacheRoot $asset) },
        @{ url="https://boostorg.jfrog.io/artifactory/main/release/$BoostVersion/source/$asset"; file=(Join-Path $CacheRoot $asset) },
        @{ url="https://archives.boost.io/release/$BoostVersion/source/$asset";                file=(Join-Path $CacheRoot $asset) }
    )
}

function Order-ByPreferred($list, [string]$pref) {
    if ($pref -eq "sourceforge") { return @($list[1],$list[0],$list[2],$list[3]) }
    elseif ($pref -eq "jfrog")  { return @($list[2],$list[0],$list[1],$list[3]) }
    elseif ($pref -eq "archives"){ return @($list[3],$list[0],$list[1],$list[2]) }
    else { return $list } # github default
}

function Test-ArchiveValid([string]$path, [int]$minMB) {
    if (-not (Test-Path $path)) { return $false }
    try {
        $len = (Get-Item $path).Length
        return ($len -ge ($minMB * 1MB))
    } catch { return $false }
}

function Invoke-Download([string]$url, [string]$outfile, [int]$retries=3) {
    for ($i=1; $i -le $retries; $i++) {
        try {
            Write-Host "Downloading: $url (attempt $i/$retries)"
            Invoke-WebRequest -Uri $url -OutFile $outfile -MaximumRedirection 10 -UseBasicParsing
            if (Test-ArchiveValid $outfile $MinArchiveSizeMB) { return $true }
            Write-Warning "File too small; retrying..."
        } catch {
            Write-Warning $_.Exception.Message
            if ($i -lt $retries) { Start-Sleep -Seconds 5 }
        }
    }
    return $false
}

# -------------------
# Check if Boost is already installed (from cache)
# -------------------
$BoostInstallMarker = Join-Path $BoostInstall ".boost_installed.ok"
if (Test-Path $BoostInstallMarker) {
    Write-Host "Boost already installed at: $BoostInstall (from cache)"
    Write-Host "Skipping build step."
    # Skip to environment export
    $SkipBuild = $true
} else {
    $SkipBuild = $false
    
    # Try vcpkg first (fastest method, no admin required)
    $VcpkgSuccess = $false
    if ($UseVcpkg) {
        Write-Host "Attempting to install Boost via vcpkg..."
        $VcpkgSuccess = Install-BoostViaVcpkg -installPath $BoostInstall -targetArch $TargetArch
        
        if ($VcpkgSuccess) {
            Write-Host "Successfully installed Boost via vcpkg (skipping source build)"
            New-Item -ItemType File -Path $BoostInstallMarker -Force | Out-Null
            $SkipBuild = $true
        } else {
            Write-Host "vcpkg installation failed or unavailable, falling back to source build..."
        }
    }
}

# -------------------
# Fetch source (only if needed)
# -------------------
if (-not $SkipBuild) {
    Write-Host "Downloading Boost source for building..."
    $ArchivePath = $null
    foreach ($asset in $AssetNames) {
        $cands = Order-ByPreferred (New-Candidates $asset) $Preferred
        foreach ($c in $cands) {
            if (Test-ArchiveValid $c.file $MinArchiveSizeMB) {
                Write-Host "Using cached archive: $($c.file)"
                $ArchivePath = $c.file; break
            }
            if (Invoke-Download $c.url $c.file 3) {
                $ArchivePath = $c.file; break
            }
        }
        if ($ArchivePath) { break }
    }
    if (-not $ArchivePath) { throw "Failed to download Boost $BoostVersion from all mirrors." }
    Write-Host "Boost archive: $ArchivePath"

    # -------------------
    # Extraction (PS5 safe; prefer 7z > tar > Expand-Archive)
    # -------------------
    # Cache extracted tree by archive base name
    $BaseName = [IO.Path]::GetFileNameWithoutExtension($ArchivePath)
    if ($ArchivePath.ToLower().EndsWith(".tar.xz")) { $BaseName = [IO.Path]::GetFileNameWithoutExtension($BaseName) }
    $ExtractCache = Join-Path $CacheRoot ("extracted_" + $BaseName)
    $Marker = Join-Path $ExtractCache ".extracted.ok"

    if (Test-Path $Marker) {
        Write-Host "Using cached extraction: $ExtractCache"
        if (Test-Path $BoostSrcRoot) { 
            Write-Host "Cleaning old extraction..."
            Remove-Item -Recurse -Force $BoostSrcRoot -ErrorAction Stop
        }
        Copy-Item -Recurse -Force $ExtractCache $BoostSrcRoot
    } else {
        Write-Host "Extracting Boost ..."
        if (Test-Path $BoostSrcRoot) { 
            Write-Host "Cleaning old extraction..."
            # Force unlock and delete
            Get-ChildItem -Path $BoostSrcRoot -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
            Remove-Item -Recurse -Force $BoostSrcRoot -ErrorAction SilentlyContinue
        }
        New-Item -ItemType Directory -Force -Path $BoostSrcRoot | Out-Null
        if (Test-Path $ExtractCache) { Remove-Item -Recurse -Force $ExtractCache -ErrorAction SilentlyContinue }
        New-Item -ItemType Directory -Force -Path $ExtractCache | Out-Null

        $SevenZip = "$env:ProgramFiles\7-Zip\7z.exe"
        if (-not (Test-Path $SevenZip)) { $SevenZip = "7z.exe" }
        $Has7z = (Get-Command $SevenZip -ErrorAction SilentlyContinue) -ne $null
        $HasTar = (Get-Command tar -ErrorAction SilentlyContinue) -ne $null

        try {
            # Prefer Expand-Archive for .zip files (more reliable on Windows)
            if ($ArchivePath -match '\.zip$') {
                Write-Host "Extracting with Expand-Archive (this may take 1-2 minutes)..."
                $ProgressPreference = 'SilentlyContinue'  # Disable progress bar for speed
                Expand-Archive -Force -Path $ArchivePath -DestinationPath $BoostSrcRoot
                Write-Host "Extraction completed."
            } elseif ($Has7z) {
                Write-Host "Extracting with 7-Zip (this may take 1-2 minutes)..."
                # Use fewer threads to avoid I/O contention
                & $SevenZip x -y "-o$BoostSrcRoot" "-mmt=2" $ArchivePath
                if ($LASTEXITCODE -ne 0) { throw "7z extraction failed with exit code $LASTEXITCODE" }
                Write-Host "Extraction completed."
            } elseif ($HasTar -and ($ArchivePath -match '\.tar(\.xz)?$')) {
                Write-Host "Extracting with tar..."
                tar -xf $ArchivePath -C $BoostSrcRoot
                Write-Host "Extraction completed."
            } else {
                throw "No suitable extraction tool found for $ArchivePath"
            }
        } catch {
            throw "Extraction failed: $($_.Exception.Message)"
        }

        if (Test-Path $ExtractCache) { Remove-Item -Recurse -Force $ExtractCache }
        Copy-Item -Recurse -Force $BoostSrcRoot $ExtractCache
        New-Item -ItemType File -Path $Marker | Out-Null
    }

    # Find directory containing b2.exe or bootstrap.bat
    $BoostRoot = $null
    $b2exe = Get-ChildItem -Path $BoostSrcRoot -Recurse -Filter "b2.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($b2exe) {
        $BoostRoot = $b2exe.Directory.FullName
    } else {
        $bootstrap = Get-ChildItem -Path $BoostSrcRoot -Recurse -Filter "bootstrap.bat" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($bootstrap) {
            $BoostRoot = $bootstrap.Directory.FullName
        }
    }

    if (-not $BoostRoot) {
        # Check if boost-src itself is the root
        if (Test-Path (Join-Path $BoostSrcRoot "bootstrap.bat")) {
            $BoostRoot = $BoostSrcRoot
        } else {
            throw "Could not locate Boost source root (bootstrap.bat not found) under $BoostSrcRoot"
        }
    }

    Write-Host "Boost source root: $BoostRoot"

    # Build and install Boost from source
    Push-Location $BoostRoot
    try {
    # Bootstrap b2 if needed
    if (-not (Test-Path (Join-Path $BoostRoot "b2.exe"))) {
        Write-Host "Bootstrapping b2 ..."
        cmd /c "bootstrap.bat" 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Bootstrap failed" }
    } else {
        Write-Host "b2.exe already present."
    }

    # Build and install with CMake config files
    $WithLibs = ($NeededComponents -join " ")
    $B2Args = @(
        "toolset=msvc",
        "address-model=$AddressModel",
        "architecture=$Architecture",
        "variant=$Variant",
        "link=$LinkType",
        "runtime-link=$RuntimeLink",
        "threading=multi",
        "cxxflags=/Zc:__cplusplus",
        "cxxflags=/permissive-",
        "cxxflags=/EHsc",
        "--prefix=`"$BoostInstall`"",
        "--build-type=minimal",
        "--cmake-config",
        "-j$($env:NUMBER_OF_PROCESSORS)"
    )

    $Cmd = ".\b2.exe $WithLibs $($B2Args -join ' ') install"
    Write-Host "Building and installing Boost with CMake config files..."
    Write-Host $Cmd
    Invoke-Expression $Cmd
    
    if ($LASTEXITCODE -ne 0) { throw "Boost build/install failed" }

    # Verify installation
    $LibDir = Join-Path $BoostInstall "lib"
    if (-not (Test-Path $LibDir)) {
        throw "Boost installation failed: lib directory not found at $LibDir"
    }
    
    # Verify CMake config files were created
    $CmakeDir = Join-Path $LibDir "cmake"
    if (Test-Path $CmakeDir) {
        Write-Host "CMake config directory found at: $CmakeDir"
        $ConfigFiles = Get-ChildItem -Path $CmakeDir -Recurse -Filter "*Config.cmake" | Select-Object -First 5
        foreach ($file in $ConfigFiles) {
            Write-Host "  - $($file.FullName)"
        }
    } else {
        Write-Warning "CMake config directory not found at $CmakeDir - CMake may have trouble finding Boost"
    }
    
    Write-Host "Boost installed successfully."
    
    # Create marker file to indicate successful installation
    New-Item -ItemType File -Path $BoostInstallMarker -Force | Out-Null
    Write-Host "Created installation marker: $BoostInstallMarker"
    }
    finally { Pop-Location }
}  # End of if (-not $SkipBuild)

# -------------------
# Export env
# -------------------
$env:BOOST_ROOT        = $BoostInstall
$env:BOOST_INCLUDEDIR  = Join-Path $BoostInstall "include"
$env:BOOST_LIBRARYDIR  = Join-Path $BoostInstall "lib"
$env:CMAKE_PREFIX_PATH = $BoostInstall
$env:Path              = "$($env:BOOST_LIBRARYDIR);$($env:Path)"

# Find and export Boost_DIR for CMake config files
$CmakeDir = Join-Path $BoostInstall "lib/cmake"
if (Test-Path $CmakeDir) {
    $BoostCmakeDir = Get-ChildItem -Path $CmakeDir -Directory -Filter "Boost-*" | Select-Object -First 1
    if (-not $BoostCmakeDir) {
        $BoostCmakeDir = Get-ChildItem -Path $CmakeDir -Directory -Filter "boost" | Select-Object -First 1
    }
    if ($BoostCmakeDir) {
        $env:Boost_DIR = $BoostCmakeDir.FullName
        Write-Host "Boost_DIR         = $($env:Boost_DIR)"
    }
}

Write-Host "BOOST_ROOT        = $($env:BOOST_ROOT)"
Write-Host "BOOST_INCLUDEDIR  = $($env:BOOST_INCLUDEDIR)"
Write-Host "BOOST_LIBRARYDIR  = $($env:BOOST_LIBRARYDIR)"
Write-Host "CMAKE_PREFIX_PATH = $($env:CMAKE_PREFIX_PATH)"

# Write Boost paths to .boost_env file for setup.py to read
# This works with cibuildwheel anywhere (local, CI, GitHub Actions, etc.)
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

# Bonus: If running in GitHub Actions, also persist to GITHUB_ENV for workflow steps
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

Write-Host "Boost setup complete."
