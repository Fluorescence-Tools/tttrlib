#!/usr/bin/env bash
rm -r -f build
cd doc
doxygen
python doxy2swig.py _build/xml/index.xml ../ext/python/documentation.i
cd ..
python setup.py install --single-version-externally-managed --record=record.txt

cd build
cmake \
  -DCMAKE_INSTALL_PREFIX=$PREFIX \
  -DCMAKE_PREFIX_PATH=$PREFIX \
  -DBUILD_PYTHON_INTERFACE=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  ..

make
make install
