cd %SRC_DIR%

echo "Update submodules"
git submodule update --recursive --init
if errorlevel 1 exit 1

echo "Build app wrapper"
:: build app wrapper
copy "%RECIPE_DIR%\app_wrapper.c" .
cl app_wrapper.c shell32.lib
if errorlevel 1 exit 1

rmdir b2 /s /q
mkdir b2
cd b2

echo Python version: %PYTHON_VERSION_NUMERIC%
cmake .. -G "NMake Makefiles" ^
 -DCMAKE_INSTALL_PREFIX="%LIBRARY_PREFIX%" ^
 -DCMAKE_PREFIX_PATH="%LIBRARY_PREFIX%;%PREFIX%" ^
 -DHDF5_ROOT="%LIBRARY_PREFIX%" ^
 -DBUILD_PYTHON_INTERFACE=ON ^
 -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%SP_DIR%" ^
 -DCMAKE_SWIG_OUTDIR="%SP_DIR%" ^
 -DPython_ROOT_DIR="%PREFIX%\bin" ^
 -DBUILD_PHOTON_HDF=ON ^
 -DBUILD_LIBRARY=OFF ^
 -DBUILD_PYTHON_DOCS=OFF ^
 -DWITH_AVX=OFF ^
 

nmake install
