:: Build script for Windows conda package (rattler-build)
:: Relies on rattler-build's automatic environment activation

cd %SRC_DIR%
rmdir /s /q b2 2>nul
mkdir b2
cd b2

:: Query site-packages from the host Python
if "%PYTHON%" == "" set PYTHON=%PREFIX%\python.exe
for /f "delims=" %%i in ('"%PYTHON%" -c "import site; print(site.getsitepackages()[0])"') do set SP_DIR=%%i
echo SP_DIR=%SP_DIR%

:: Convert paths to forward slashes for CMake
set "PREFIX_W=%PREFIX:\=/%"
set "SP_DIR_W=%SP_DIR:\=/%"

cmake -S .. -B . ^
  -G Ninja ^
  %CMAKE_ARGS% ^
  -DCMAKE_PREFIX_PATH="%PREFIX_W%/Library;%PREFIX_W%" ^
  -DHDF5_ROOT="%PREFIX_W%/Library" ^
  -DHDF5_USE_STATIC_LIBRARIES=OFF ^
  -DBUILD_PYTHON_INTERFACE=ON ^
  -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%SP_DIR_W%" ^
  -DCMAKE_SWIG_OUTDIR="%SP_DIR_W%" ^
  -DPython_ROOT_DIR="%PREFIX_W%" ^
  -DBUILD_LIBRARY=OFF ^
  -DBUILD_PYTHON_DOCS=OFF ^
  -DWITH_AVX=OFF
if errorlevel 1 exit 1

ninja install -j %CPU_COUNT%
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
