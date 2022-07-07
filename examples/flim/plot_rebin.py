"""
===============
Image rebinning
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
img_original = image.intensity.sum(axis=0)

# do a 4x4 binning (uses internally CLSMImage.transform)
bin_line = 4
bin_pixel = 4
n_frames, n_lines, n_pixel = image.shape

image.rebin(bin_line, bin_pixel)
img_transformed = image.intensity.sum(axis=0)

# Plot the results
fig, axs = plt.subplots(1, 2)
axs[0].imshow(img_original)
axs[1].imshow(img_transformed)
plt.show()

# After transforming the image can be cropped
image.crop(0, n_frames, 0, n_lines // bin_line, 0, n_pixel // bin_pixel)
img_transformed_cropped = image.intensity.sum(axis=0)
# Plot the results
fig, axs = plt.subplots(1, 2)
axs[0].imshow(img_original)
axs[1].imshow(img_transformed_cropped)
plt.show()
