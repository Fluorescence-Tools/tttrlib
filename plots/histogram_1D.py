import tttrlib
import numpy as np
import pylab as p

fig, ax = p.subplots(3, 1)

# Linear histograms
data = np.random.normal(10, 2, int(2e6))
bins = np.linspace(0, 20, 32000, dtype=np.float)
hist = np.zeros(len(bins), dtype=np.float)
weights = np.ones_like(data)
tttrlib.histogram1D_double(data, weights, bins, hist, 'lin', True)
ax[0].plot(bins, hist, 'g')

# Logarithmic histogram
bins = np.logspace(0, 3.5, 32000, dtype=np.float)
data = np.random.lognormal(3.0, 1, int(2e6))
hist = np.zeros(len(bins), dtype=np.float)
weights = np.ones_like(data)
tttrlib.histogram1D_double(data, weights, bins, hist, '', True)
ax[1].semilogx(bins, hist, 'r')

# Histogram with arbitrary spacing
bins1 = np.linspace(1, 600, 16000, dtype=np.float)
bins2 = np.logspace(np.log10(bins1[-1]+0.1), 3.0, 16000, dtype=np.float)
bins = np.hstack([bins1, bins2])
hist = np.zeros(len(bins), dtype=np.float)
weights = np.ones_like(data)
tttrlib.histogram1D_double(data, weights, bins, hist, '', True)
ax[2].semilogx(bins, hist, 'y')

p.show()
