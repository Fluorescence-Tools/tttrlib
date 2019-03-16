#!/usr/bin/python

import timeit


#############
# Test the TTTR reading function
#############

import phconvert
import tttrlib
import numpy as np
import pylab as p

plot_output = True

# Load different file types
# 0 = PQ-PTU
# 1 = PQ-HT3
# 2 = BH-Spc132
# 3 = BH-Spc600_256
# 4 = BH-Spc600_4096 (I don't have a sample file)
# 5 = Photon-HDF5


ptu = tttrlib.TTTR('../../examples/PQ/PTU/PQ_PTU_HH_T3.ptu', 0)
ht3 = tttrlib.TTTR('../../examples/PQ/HT3/PQ_HT3_CLSM.ht3', 1)
spc132 = tttrlib.TTTR('../../examples/BH/BH_SPC132.spc', 2)
spc600_256 = tttrlib.TTTR('../../examples/BH/BH_SPC630_256.spc', 3)
photon_hdf5 = tttrlib.TTTR('../../examples/HDF/1a_1b_Mix.hdf5', 5)

# Compare speed to phconvert
n_test_runs = 10
time_phconvert = timeit.timeit(
    'phconvert.pqreader.load_ht3("../../examples/PQ/HT3/1a_1b_Mix.ht3", ovcfunc=None)',
    number=n_test_runs,
    setup="from __main__ import phconvert"
)
time_tttrlib = timeit.timeit(
    'tttrlib.TTTR("../../../examples/PQ/HT3/1a_1b_Mix.ht3", 1)',
    number=n_test_runs,
    setup="from __main__ import tttrlib"
)

print("time(phconvert) = %s" % (time_phconvert / n_test_runs))
print("time(tttrlib) = %s" % (time_tttrlib / n_test_runs))
print("tttrlib speedup: %.2f" % (time_phconvert / time_tttrlib))

# time(phconvert) = 5054 ms (without numba)
# time(phconvert) = 1840 ms (with numba) # 270 MB file -> 141 MB/sec
# time(tttrlib)   = 690 ms               # 270 Mb file -> 391 MB/sec

r = spc132
ch0 = r.get_indeces_by_channels(np.array([0]))
p.hist(r.micro_times[ch0], bins=np.linspace(1, 2000))

ch1 = r.get_indeces_by_channels(np.array([2]))
p.hist(r.micro_times[ch1], bins=np.linspace(1, 2000))

p.show()
