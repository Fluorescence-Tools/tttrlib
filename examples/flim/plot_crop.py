"""
===============
Image cropping
===============
"""
from __future__ import print_function
import tttrlib
import numpy as np
import pylab as plt

#%%
# Read data of the CLSM image
data = tttrlib.TTTR('../../tttr-data/imaging/pq/ht3/crn_clv_img.ht3')

# Read contents of file into new CLSMImage
settings = {
    "channels": [0, 1],
    "fill": True,
}
image = tttrlib.CLSMImage(data, **settings)
image.shape == (40, 256, 256)
im = image.intensity.sum(axis=0)
plt.imshow(im)
plt.show()

# Cropping
image.crop(5, 20, 0, 128, 0, 96)

image.shape == (15, 128, 96)
im = image.intensity.sum(axis=0)
plt.imshow(im)
plt.show()
