Compilation
===========

Windows
-------

tttrlib can be compiled with the `Visual Studio 2017 <https://visualstudio.microsoft.com/vs/community/>`_ Community
edition. In addition to Visual Studio a `HDF5 <https://www.hdfgroup.org/>`_ library with include files need to be
installed. The tttrlib distribution provides files for the use with `cmake <https://cmake.org/>`_. In addition to
A recent 64bit `Python <https://www.python.org/>`_ installation is required. All precompiled Windows libraries
were compiled on Windows 10 with a minimal 64bit Python 2.7 `miniconda <https://docs.conda.io/en/latest/miniconda.html>`_
installation.

MacOS
-----


Linux
-----



Conda
-----

A conda recipe is provided in the folder 'conda-recipe' to build the tttrlib library with the 
[conda build](https://docs.conda.io/projects/conda-build/en/latest/) environment.

To build the library download the recipe, install the conda build package and use the provided
recipe to build the library. ::

..
    conda install conda-build
    conda build conda-recipe

