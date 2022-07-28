git submodule update --recursive --init --remote
rmdir build /s /q
%PYTHON% %SRC_DIR%\setup.py install --single-version-externally-managed --record=record.txt
cd build
cmake .. -G "NMake Makefiles" ^
  -DCMAKE_INSTALL_PREFIX=%PREFIX%/Library ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_PYTHON_INTERFACE=OFF ^
  -DCMAKE_PREFIX_PATH=%PREFIX%
nmake
nmake install

