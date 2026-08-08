:: Build script for the tttr CLI Windows conda package (rattler-build)
@echo on
setlocal EnableDelayedExpansion

cd /d %SRC_DIR%
rmdir /s /q b2 2>nul
mkdir b2
cd b2

:: Activate VS if the compiler is not already on PATH (same logic as py/build.bat)
where cl.exe >nul 2>&1
if errorlevel 1 (
    call :activate_vs
    if errorlevel 1 exit /b 1
)

set "PREFIX_W=%PREFIX:\=/%"

cmake -S .. -B . ^
  -G "Ninja" ^
  %CMAKE_ARGS% ^
  -DCMAKE_INSTALL_PREFIX="%PREFIX_W%" ^
  -DHDF5_USE_STATIC_LIBRARIES=OFF ^
  -DBUILD_LIBRARY=ON ^
  -DBUILD_PYTHON_INTERFACE=OFF ^
  -DBUILD_R_INTERFACE=OFF ^
  -DBUILD_PYTHON_DOCS=OFF ^
  -DWITH_AVX=OFF ^
  -DWITH_TIFF=ON ^
  -DWITH_TIFF_SYSTEM=ON
if errorlevel 1 exit 1

ninja tttrlibShared tttrlibStatic tttr
if errorlevel 1 exit 1

ninja install
if errorlevel 1 exit 1

:: Smoke test
"%PREFIX%\bin\tttr.exe" --help >nul
if errorlevel 1 exit 1

exit /b 0

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
if not defined VSWHERE exit /b 1
set "VSINSTALLDIR="
for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALLDIR=%%i"
)
if not defined VSINSTALLDIR exit /b 1
set "VCVARSALL=!VSINSTALLDIR!\VC\Auxiliary\Build\vcvarsall.bat"
call "!VCVARSALL!" x64
exit /b %ERRORLEVEL%
