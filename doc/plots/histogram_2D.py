import timeit
import tttrlib
import numpy as np
import pylab as p


def histogram2d(data, bins):
    h = tttrlib.doubleHistogram()
    h.setAxis(0, "x", -3, 3, bins, 'lin')
    h.setAxis(1, "y", -3, 3, bins, 'lin')
    h.setData(data.T)
    h.update()
    return h.getHistogram().reshape((bins, bins))


print("\n\nTesting 2D Histogram linear spacing")
print("---------------------------------------")
x = np.random.randn(10000)
y = 0.2 * np.random.randn(10000)
data = np.vstack([x, y])
bins = 100

hist2d = histogram2d(data, 100)
p.imshow(hist2d)
p.show()
