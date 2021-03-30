"""
====================
Mean lifetime images
====================

"""
import tttrlib
import numpy as np
import pylab as plt

filename = '../../tttr-data/imaging/pq/ht3/crn_clv_img.ht3'
filename_irf = '../../tttr-data/imaging/pq/ht3/crn_clv_mirror.ht3'

data = tttrlib.TTTR(filename)
irf = tttrlib.TTTR(filename_irf)

# Create a new CLSM Image. This image will be used
# as a template for the green and red image. This avoids
# passing through the TTTR screen multiple times. The frame
# line, and pixel locations will be copied for the green and
# red image from this template
clsm_template = tttrlib.CLSMImage(data)
clsm_green = tttrlib.CLSMImage(
    source=clsm_template,
    channels=[0, 2]
)
clsm_red = tttrlib.CLSMImage(
    source=clsm_template,
    channels=[1, 3]
)

green = clsm_green.intensity.sum(axis=0)
red = clsm_red.intensity.sum(axis=0)


mean_tau_green = clsm_green.get_mean_lifetime_image(
    tttr_irf=irf[irf.get_selection_by_channel([0, 2])],
    tttr_data=data,
    minimum_number_of_photons=20,
    stack_frames=True
)

mask = (green < 30) + (red < 30)
masked_green = np.ma.masked_where(mask, green)
masked_red = np.ma.masked_where(mask, red)
masked_tau = np.ma.masked_where(mask, mean_tau_green.mean(axis=0))
lg_sg_sr = np.log(masked_green / masked_red)

fig, ax = plt.subplots(nrows=2, ncols=2)
ax[0, 0].set_title('Green intensity')
ax[0, 1].set_title('Red intensity')
ax[1, 0].set_title('Mean green fl. lifetime')
ax[1, 1].set_title('Pixel histogram')
ax[1, 1].set_xlabel('tauG / ns')
ax[1, 1].set_ylabel('log(Sg/Sr')
ax[0, 0].imshow(green, cmap='cividis')
ax[0, 1].imshow(red, cmap='inferno')
ax[1, 0].imshow(mean_tau_green.mean(axis=0), cmap='Spectral')
ax[1, 1].hist2d(
    x=masked_tau.flatten(),
    y=lg_sg_sr.flatten(),
    range=((0.001, 4), (-2, 0.9)),
    bins=41
)
plt.show()

