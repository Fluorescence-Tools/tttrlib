#!/usr/bin/python

import timeit

#############
# Test the TTTR reading function
#############

import phconvert
import os
import tttrlib
import numpy as np
import pylab as p


test_files = [
    ('../data/bh/bh_spc132.spc', 'SPC-130'),
    ('../data/bh/bh_spc132.spc', 'SPC-130'),
    ('../data/bh/bh_spc630_256.spc', 'SPC-600_256'),
    ('../data/HDF/1a_1b_Mix.hdf5', 'PHOTON-HDF5'),
    ('../data/PQ/HT3/PQ_HT3_CLSM.ht3', 'HT3'),
    ('../data/PQ/PTU/PQ_PTU_HH_T2.ptu', 'PTU'),
    ('../data/PQ/PTU/PQ_PTU_HH_T3.ptu', 'PTU')
]

# Load different file types
# 0 = PQ-PTU
# 1 = PQ-HT3
# 2 = BH-Spc132
# 3 = BH-Spc600_256
# 4 = BH-Spc600_4096 (I don't have a sample file)
# 5 = Photon-HDF5


ptu = tttrlib.TTTR('../data/PQ/PTU/PQ_PTU_HH_T3.ptu', 0)
ht3 = tttrlib.TTTR('../data/PQ/HT3/PQ_HT3_CLSM.ht3', 1)
spc132 = tttrlib.TTTR('../data/bh/bh_spc132.spc', 2)
spc600_256 = tttrlib.TTTR('../data/bh/bh_spc630_256.spc', 3)
photon_hdf5 = tttrlib.TTTR('../data/HDF/1a_1b_Mix.hdf5', 5)

# Compare speed to phconvert
benchmark_file = '../data/PQ/PTU/PQ_PTU_HH_T3.ptu'
statinfo = os.stat(benchmark_file)
n_test_runs = 10
time_phconvert = timeit.timeit(
    'phconvert.pqreader.load_ptu("%s", ovcfunc=None)' % benchmark_file,
    number=n_test_runs,
    setup="from __main__ import phconvert"
)
time_tttrlib = timeit.timeit(
    'tttrlib.TTTR("%s", "PTU")' % benchmark_file,
    number=n_test_runs,
    setup="from __main__ import tttrlib"
)

file_size = statinfo.st_size / (1024 * 1024)
print("time(phconvert) = %s s" % (time_phconvert / n_test_runs))
print("time(tttrlib) = %s s" % (time_tttrlib / n_test_runs))
print("MB/sec(phconvert) = %s s" % (file_size / (time_phconvert / n_test_runs)))
print("MB/sec(tttrlib) = %s s" % (file_size / (time_tttrlib / n_test_runs)))
print("tttrlib speedup: %.2f" % (time_phconvert / time_tttrlib))

# time(phconvert) = 5054 ms (without numba)
# time(phconvert) = 1840 ms (with numba) # 270 MB file -> 141 MB/sec
# time(tttrlib)   = 690 ms               # 270 Mb file -> 391 MB/sec
