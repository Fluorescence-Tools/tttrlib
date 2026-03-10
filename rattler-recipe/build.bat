:: Build script for Windows conda package (rattler-build)
@echo on

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
echo "SP_DIR detected as: %SP_DIR%"

:: Convert paths to forward slashes for CMake
set "PREFIX_W=%PREFIX:\=/%"
set "SP_DIR_W=%SP_DIR:\=/%"

:: Activate the MSVC compiler environment.
:: rattler-build's ${{ compiler('c') }} sets CC/CXX but Ninja needs the full
:: Visual Studio environment (PATH, INCLUDE, LIB) to locate cl.exe.
:: Use vswhere to find the latest VS installation and call vcvarsall.bat.
set "VSWHERE=%BUILD_PREFIX%\Library\bin\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo WARNING: vswhere.exe not found, relying on environment
    goto :skip_vcvars
)

:: Find latest VS (any version: 2017, 2019, 2022)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALLDIR=%%i"
)

if not defined VSINSTALLDIR (
    echo WARNING: No Visual Studio installation found via vswhere, relying on environment
    goto :skip_vcvars
)

echo "Found Visual Studio at: %VSINSTALLDIR%"
if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
    call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64
    if errorlevel 1 (
        echo WARNING: vcvarsall.bat failed, relying on environment
    )
) else (
    echo WARNING: vcvarsall.bat not found at expected location
)

:skip_vcvars

:: Verify compiler is available
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found on PATH after environment setup
    echo PATH=%PATH%
    exit /b 1
)

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
