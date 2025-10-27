:: Submodules should already be checked out by GitHub Actions checkout step
cd %SRC_DIR%

:: Manually activate the VS2017 compiler from conda's vs2017_win-64 package
:: The activation scripts should be sourced automatically by conda-build, but we'll do it explicitly
for /f "usebackq tokens=*" %%i in (`"%BUILD_PREFIX%\Library\bin\vswhere.exe" -products * -version "[15.0,16.0)" -property installationPath`) do (
    set "VSINSTALLDIR=%%i"
)

if not defined VSINSTALLDIR (
    echo "Visual Studio 2017 not found via vswhere"
    exit 1
)

echo "Found VS2017 at: %VSINSTALLDIR%"
call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64

rmdir b2 /s /q
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
 -DWITH_AVX=ON

nmake install
