@echo off
setlocal

rem ------------------------------------------------------------
rem Robust Sphinx HTML build (Windows)
rem  - First arg is BUILD_TIER iff 0..4 (then SHIFTs)
rem  - Remaining args are passed to Sphinx BEFORE sourcedir/outdir
rem  - Creates minimal RSTs via PowerShell (avoids CMD () pitfalls)
rem  - Keeps Doxygen + API move
rem ------------------------------------------------------------

rem Full clean for gallery builds
if exist _build rmdir /s /q _build
if exist auto_examples rmdir /s /q auto_examples
if exist .jupyter_cache rmdir /s /q .jupyter_cache

set "EXITCODE=0"

rem Go to this script's directory (docs/)
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%"

rem Defaults (caller can override via environment)
if not defined BUILDDIR set "BUILDDIR=_build"
if not defined SPHINXOPTS set "SPHINXOPTS=-j auto -vv"

rem ---------- Decide sphinx command ----------
if defined SPHINXBUILD (
  rem Using user-provided SPHINXBUILD: %SPHINXBUILD%
) else (
  where sphinx-build >nul 2>&1
  if %ERRORLEVEL%==0 (
    set "SPHINXBUILD=sphinx-build"
  ) else (
    set "SPHINXBUILD=python -m sphinx"
  )
)

rem ---------- Parse first arg as tier ONLY if 0..4 ----------
set "BUILD_TIER_ARG="
if "%~1"=="0" set "BUILD_TIER_ARG=0"
if "%~1"=="1" set "BUILD_TIER_ARG=1"
if "%~1"=="2" set "BUILD_TIER_ARG=2"
if "%~1"=="3" set "BUILD_TIER_ARG=3"
if "%~1"=="4" set "BUILD_TIER_ARG=4"

if defined BUILD_TIER_ARG (
  set "BUILD_TIER=%BUILD_TIER_ARG%"
  shift
) else (
  if not defined BUILD_TIER set "BUILD_TIER=0"
)

rem Optional: pattern for sphinx-gallery
if defined EXAMPLES_PATTERN (
  set "EXAMPLES_PATTERN_OPTS=-D sphinx_gallery_conf.filename_pattern=%EXAMPLES_PATTERN%"
) else (
  set "EXAMPLES_PATTERN_OPTS="
)

echo [make_html] SCRIPT_DIR   = %SCRIPT_DIR%
echo [make_html] BUILDDIR     = %BUILDDIR%
echo [make_html] SPHINXBUILD  = %SPHINXBUILD%
echo [make_html] SPHINXOPTS   = %SPHINXOPTS%
echo [make_html] BUILD_TIER   = %BUILD_TIER%
if defined EXAMPLES_PATTERN echo [make_html] EXAMPLES_PATTERN = %EXAMPLES_PATTERN%
if not "%~1"=="" echo [make_html] EXTRA options   = %*


rem ---------- Clean stale caches ----------
if exist "%BUILDDIR%\html\stable\_images" (
  echo [make_html] Removing "%BUILDDIR%\html\stable\_images" ...
  rmdir /s /q "%BUILDDIR%\html\stable\_images"
)
if exist "%BUILDDIR%\doctrees" (
  echo [make_html] Removing "%BUILDDIR%\doctrees" ...
  rmdir /s /q "%BUILDDIR%\doctrees"
)

rem Ensure target dirs
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
if not exist "%BUILDDIR%\html" mkdir "%BUILDDIR%\html"
if not exist "%BUILDDIR%\html\stable" mkdir "%BUILDDIR%\html\stable"

rem ---------- Doxygen (optional) ----------
if exist Doxyfile (
  echo [make_html] Running Doxygen...
  doxygen
  if errorlevel 1 goto :error
) else (
  echo [make_html] Warning: No Doxyfile found. Skipping Doxygen.
)

rem ---------- Build HTML ----------
echo [make_html] Building Sphinx HTML...
set "BUILD_TIER=%BUILD_TIER%"
rem Pass options BEFORE '.' (sourcedir) and the outdir; no tokens after outdir.
%SPHINXBUILD% -b html . "%BUILDDIR%/html/stable"
if errorlevel 1 goto :error

rem Move API docs if present
if exist "%BUILDDIR%\api" (
  echo [make_html] Moving API docs into HTML tree...
  if exist "%BUILDDIR%\html\stable\api" rmdir /s /q "%BUILDDIR%\html\stable\api"
  move /y "%BUILDDIR%\api" "%BUILDDIR%\html\stable" >nul
)

echo.
echo [make_html] Build finished. HTML is in %BUILDDIR%\html\stable
goto :success

:error
echo.
echo *** Build failed. ***
set "EXITCODE=1"

:success
popd
endlocal & exit /b %EXITCODE%
