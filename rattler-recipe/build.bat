:: Submodules should already be checked out by GitHub Actions checkout step
cd %SRC_DIR%

rmdir b2 /s /q
mkdir b2
cd b2

cmake .. -G "NMake Makefiles" ^
 -DCMAKE_INSTALL_PREFIX="%LIBRARY_PREFIX%" ^
 -DCMAKE_PREFIX_PATH="%PREFIX%" ^
 -DBUILD_PYTHON_INTERFACE=ON ^
 -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%SP_DIR%" ^
 -DCMAKE_SWIG_OUTDIR="%SP_DIR%" ^
 -DPython_ROOT_DIR="%PREFIX%\bin" ^
 -DBUILD_LIBRARY=OFF ^
 -DBUILD_PYTHON_DOCS=OFF ^
 -DBUILD_LOGIC_ANALYZER=OFF ^
 -DWITH_AVX=OFF

nmake install

:: Convert SWIG output to a package
:: SWIG generates: tttrlib.py (module) + _tttrlib*.pyd (extension)
:: We need both in tttrlib\ package directory
for /f "delims=" %%i in ('python -c "import site; print(site.getsitepackages()[0])"') do set PYTHON_SITE_PACKAGES=%%i

:: Create tttrlib package directory
if not exist "%PYTHON_SITE_PACKAGES%\tttrlib" mkdir "%PYTHON_SITE_PACKAGES%\tttrlib"

:: Move the compiled extension (_tttrlib*.pyd) to package dir
for %%f in ("%PYTHON_SITE_PACKAGES%"\_tttrlib*.pyd) do (
    if exist "%%f" move "%%f" "%PYTHON_SITE_PACKAGES%\tttrlib\"
)

:: Move the Python wrapper to __init__.py
if exist "%PYTHON_SITE_PACKAGES%\tttrlib.py" (
    move "%PYTHON_SITE_PACKAGES%\tttrlib.py" "%PYTHON_SITE_PACKAGES%\tttrlib\__init__.py"
)
