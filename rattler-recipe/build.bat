:: Build script for Windows conda package (rattler-build)
@echo on
setlocal EnableDelayedExpansion

:: Ensure we are in the source directory
cd /d %SRC_DIR%

rmdir /s /q b2 2>nul
mkdir b2
cd b2

:: Query site-packages from the host Python
if "%PYTHON%" == "" set "PYTHON=%PREFIX%\python.exe"
%PYTHON% -c "import sysconfig; print(sysconfig.get_path('purelib'))" > sp_dir.txt
set /p SP_DIR=<sp_dir.txt
set "SP_DIR=%SP_DIR:/=\%"
echo SP_DIR detected as: %SP_DIR%

:: Convert paths to forward slashes for CMake
set "PREFIX_W=%PREFIX:\=/%"
set "SP_DIR_W=%SP_DIR:\=/%"

:: Check if the compiler is already available from rattler-build activation.
:: The compiler('c') / compiler('cxx') jinja functions should set up the
:: environment via build_env.bat.  Only attempt manual activation if cl.exe
:: is not already on PATH.
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo cl.exe not on PATH, attempting manual VS activation...
    call :activate_vs
    if errorlevel 1 (
        echo ERROR: Failed to activate Visual Studio environment
        exit /b 1
    )
) else (
    echo cl.exe found on PATH, using existing environment
)

:: Final verification
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found on PATH
    exit /b 1
)
echo Using compiler:
where cl.exe

cmake -S .. -B . ^
  -G "Ninja" ^
  %CMAKE_ARGS% ^
  -DHDF5_USE_STATIC_LIBRARIES=OFF ^
  -DBUILD_PYTHON_INTERFACE=ON ^
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%SP_DIR_W%" ^
  -DCMAKE_SWIG_OUTDIR="%SP_DIR_W%" ^
  -DPython_ROOT_DIR="%PREFIX_W%" ^
  -DBUILD_LIBRARY=OFF ^
  -DBUILD_PYTHON_DOCS=OFF ^
  -DWITH_AVX=OFF
if errorlevel 1 exit 1

ninja install
if errorlevel 1 exit 1

:: Assemble tttrlib Python package directory in site-packages
if not exist "%SP_DIR%\tttrlib" mkdir "%SP_DIR%\tttrlib"

:: Move compiled extension (_tttrlib*.pyd) into package dir
for %%f in ("%SP_DIR%\_tttrlib*.pyd") do (
    if exist "%%f" move "%%f" "%SP_DIR%\tttrlib\"
)

:: Move SWIG-generated wrapper (tttrlib.py) to __init__.py
if exist "%SP_DIR%\tttrlib.py" (
    move "%SP_DIR%\tttrlib.py" "%SP_DIR%\tttrlib\__init__.py"
)

exit /b 0

:: ============================================================
:: Subroutine: activate_vs
:: Finds the latest Visual Studio and calls vcvarsall.bat
:: ============================================================
:activate_vs

:: Try vswhere from the build prefix first
set "VSWHERE="
if exist "%BUILD_PREFIX%\Library\bin\vswhere.exe" (
    set "VSWHERE=%BUILD_PREFIX%\Library\bin\vswhere.exe"
)

:: Fallback: system vswhere (use quotes around the full path to handle parentheses)
if not defined VSWHERE (
    set "VSWHERE_SYS=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
)
if not defined VSWHERE if defined VSWHERE_SYS (
    if exist "!VSWHERE_SYS!" set "VSWHERE=!VSWHERE_SYS!"
)

if not defined VSWHERE (
    echo ERROR: vswhere.exe not found
    exit /b 1
)

echo Using vswhere at: !VSWHERE!

:: Find latest VS installation
set "VSINSTALLDIR="
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALLDIR=%%i"
)

if not defined VSINSTALLDIR (
    echo ERROR: No Visual Studio found via vswhere
    exit /b 1
)

echo Found Visual Studio at: !VSINSTALLDIR!

if not exist "!VSINSTALLDIR!\VC\Auxiliary\Build\vcvarsall.bat" (
    echo ERROR: vcvarsall.bat not found
    exit /b 1
)

call "!VSINSTALLDIR!\VC\Auxiliary\Build\vcvarsall.bat" x64
exit /b %ERRORLEVEL%
