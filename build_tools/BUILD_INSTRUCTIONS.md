# Documentation

The documentation of tttrlib is build on Linux (Ubuntu 18.04) and requires a set of 
packages and programs. To facilitate building the documentation all required packages 
are listed in a YAML file that defines a conda environment (`docs/environemnt.yml`).

To create the conda environment to buidl the documentation use the 
following commands 

Requirements:
  * 16 GB RAM (Otherwise some data/plots does not fit in memory)

```commandline
cd docs
conda env create -f environment.yml
```

First, build the "documentation.i", i.e., the SWIG documentation for the
Python bindings.
```commandline
conda activate doc
python ../setup.py docs
```

Next, build the Python documentation via Sphinx.

```commandline
cd doc
make html
```

