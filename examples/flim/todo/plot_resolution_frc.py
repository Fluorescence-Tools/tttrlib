"""
==================================
Fourier ring correlation of images
==================================

Estimation of the image resolution
----------------------------------
In electron microscopy the Fourier Ring Correlation (FRC) is widely used as a
measure for the resolution of an image. This very practical approach for a quality
measure begins to get traction in fluorescence microscopy. Briefly, the correlation
between two subsets of the same images are Fourier transformed and their overlap
in the Fourier space is measured. The FRC is the normalised cross-correlation
coefficient between two images over corresponding shells in Fourier space transform.

In CLSM usually multiple images of the sample are recoded. Thus, the
resolution of the image can be estimated by the FRC. Below a few lines of python
code are shown that read a CLSM image, split the image into two sets, and plot
the FRC of the two subsets is shown for intensity images.

#.. plot:: ../examples/imaging/imaging_frc.py

The above approach is used by the software `ChiSurf <https://github.com/fluorescence-tools/chisurf/>`_.
In practice, a set of CLSM images can be split into two subsets. The two subsets
can be used to estimate the resolution of the image.

"""
from __future__ import annotations

import numpy as np
import pylab as plt
import tttrlib


filename = '../../tttr-data/imaging/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
data = tttrlib.TTTR(filename, 'PTU')

line_factor = 1
reading_parameter = {
    "tttr_data": data,
    "marker_frame_start": [4, 6],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 15,
    # if zero the number of pixels is the set to the number of lines
    "n_pixel_per_line": 512 * line_factor,
    "reading_routine": 'SP8',
    "channels": [1],
    "fill": True
}

clsm_image = tttrlib.CLSMImage(**reading_parameter)

img = clsm_image.intensity
im1 = img[::2].sum(axis=0)
im2 = img[1::2].sum(axis=0)
frc, frc_bins = clsm_image.compute_frc(im1, im2)
#%%
# Equivalent to
frc, frc_bins = clsm_image.frc

fig, ax = plt.subplots(nrows=1, ncols=2, sharex=False, sharey=False)
ax[0].set_title('Intensity')
ax[1].set_title('FRC')
ax[1].plot(frc_bins[1:], frc, label="Intensity")
ax[0].imshow(img.mean(axis=0))
plt.show()
