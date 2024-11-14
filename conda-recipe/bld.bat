cd %SRC_DIR%

echo "Build app wrapper"
:: build app wrapper
copy "%RECIPE_DIR%\app_wrapper.c" .
cl app_wrapper.c shell32.lib
if errorlevel 1 exit 1

rmdir b2 /s /q
mkdir b2
cd b2

REM Call Python with the --version flag to get the version information
for /f "tokens=2 delims= " %%v in ('%PYTHON% --version 2^>^&1') do set PYTHON_VERSION=%%v

REM Extract only the numeric part of the version
for /f "tokens=1-3 delims=." %%a in ("%PYTHON_VERSION%") do set PYTHON_VERSION_NUMERIC=%%a.%%b.%%c

echo Python version: %PYTHON_VERSION_NUMERIC%
cmake .. -G "NMake Makefiles" ^
 -DCMAKE_INSTALL_PREFIX="%LIBRARY_PREFIX%" ^
 -DCMAKE_PREFIX_PATH="%PREFIX%" ^
 -DBUILD_PYTHON_INTERFACE=ON ^
 -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%SP_DIR%" ^
 -DCMAKE_SWIG_OUTDIR="%SP_DIR%" ^
 -DPYTHON_VERSION="%PYTHON_VERSION_NUMERIC%" ^
 -DBUILD_LIBRARY=OFF ^
 -DWITH_AVX=OFF ^
 -DBoost_USE_STATIC_LIBS=OFF

nmake install

:: Add wrappers to path for each Python command line tool
:: (all files without an extension)
cd %SRC_DIR%\bin
for /f %%f in ('dir /b *.py') do copy "%SRC_DIR%\bin\%%f" "%PREFIX%\Library\bin\%%f"
for /f %%f in ('dir /b *.') do copy "%SRC_DIR%\app_wrapper.exe" "%PREFIX%\Library\bin\%%f.exe"
if errorlevel 1 exit 1
