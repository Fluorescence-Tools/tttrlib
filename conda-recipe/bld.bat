cd %SRC_DIR%
git submodule update --recursive --init --remote
%PYTHON% %SRC_DIR%\setup.py install --single-version-externally-managed --record=record.txt

cd %SRC_DIR%
mkdir b2 && cd b2
cmake .. -G "NMake Makefiles" ^
  -DCMAKE_INSTALL_PREFIX=%PREFIX%/Library ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_PYTHON_INTERFACE=OFF ^
  -DCMAKE_PREFIX_PATH=%PREFIX%
nmake
nmake install
