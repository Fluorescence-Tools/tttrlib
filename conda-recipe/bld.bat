:: Build script for Windows conda package (conda-build)
@echo on

cd %SRC_DIR%

:: Activate the MSVC compiler environment.
:: Use vswhere to find the latest VS installation.
set "VSWHERE=%BUILD_PREFIX%\Library\bin\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo WARNING: vswhere.exe not found, relying on environment
    goto :skip_vcvars
)

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
    exit /b 1
)

rmdir b2 /s /q 2>nul
mkdir b2
cd b2

echo Python version: %PYTHON_VERSION_NUMERIC%
cmake .. -G "NMake Makefiles" ^
 -DCMAKE_INSTALL_PREFIX="%LIBRARY_PREFIX%" ^
 -DCMAKE_PREFIX_PATH="%PREFIX%" ^
 -DBUILD_PYTHON_INTERFACE=ON ^
 -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%SP_DIR%" ^
 -DCMAKE_SWIG_OUTDIR="%SP_DIR%" ^
 -DPython_ROOT_DIR="%PREFIX%\bin" ^
 -DBUILD_LIBRARY=OFF ^
 -DBUILD_PYTHON_DOCS=ON ^
 -DWITH_AVX=OFF

nmake install
