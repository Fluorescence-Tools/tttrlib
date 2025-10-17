:: Submodules should already be checked out by GitHub Actions checkout step
cd %SRC_DIR%

:: Activate the compiler
call "%BUILD_PREFIX%\etc\conda\activate.d\vs2017_compiler_activation.bat"
if errorlevel 1 (
    echo "Failed to activate VS2017 compiler"
    exit 1
)

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
 -DWITH_AVX=OFF ^
 -DBoost_USE_STATIC_LIBS=OFF

nmake install
