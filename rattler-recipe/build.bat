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
:: SWIG generates files directly to %SP_DIR% (site-packages)
:: Create tttrlib package directory if it doesn't exist
if not exist "%SP_DIR%\tttrlib" mkdir "%SP_DIR%\tttrlib"

:: Move the compiled extension (_tttrlib*.pyd) from site-packages root to tttrlib package dir
for %%f in ("%SP_DIR%"\_tttrlib*.pyd) do (
    if exist "%%f" move "%%f" "%SP_DIR%\tttrlib\"
)

:: Move the Python wrapper (tttrlib.py) to __init__.py in package dir
if exist "%SP_DIR%\tttrlib.py" (
    move "%SP_DIR%\tttrlib.py" "%SP_DIR%\tttrlib\__init__.py"
)
