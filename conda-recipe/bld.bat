:: Build script for Windows conda package (conda-build)
@echo on
setlocal EnableDelayedExpansion

cd %SRC_DIR%

:: Check if the compiler is already available from conda-build activation.
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

exit /b %ERRORLEVEL%

:: ============================================================
:: Subroutine: activate_vs
:: ============================================================
:activate_vs
set "VSWHERE="
if exist "%BUILD_PREFIX%\Library\bin\vswhere.exe" (
    set "VSWHERE=%BUILD_PREFIX%\Library\bin\vswhere.exe"
)
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

set "VSINSTALLDIR="
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALLDIR=%%i"
)
if not defined VSINSTALLDIR (
    echo ERROR: No Visual Studio found via vswhere
    exit /b 1
)
echo Found Visual Studio at: !VSINSTALLDIR!
call "!VSINSTALLDIR!\VC\Auxiliary\Build\vcvarsall.bat" x64
exit /b %ERRORLEVEL%
