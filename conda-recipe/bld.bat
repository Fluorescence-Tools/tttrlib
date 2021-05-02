
rmdir build /s /q
cd doc
doxygen
%PYTHON% doxy2swig.py ./_build/xml/index.xml ../ext/python/documentation.i
cd ..
%PYTHON% setup.py install --single-version-externally-managed --record=record.txt

cd build
cmake .. -G "NMake Makefiles" ^
  -DCMAKE_INSTALL_PREFIX=%PREFIX%/Library ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_PYTHON_INTERFACE=OFF ^
  -DCMAKE_PREFIX_PATH=%PREFIX%

nmake
if errorlevel 1 exit 1
nmake install
if errorlevel 1 exit 1

