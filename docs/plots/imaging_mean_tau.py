import tttrlib
import numpy as np
import pylab as p

filename = '../../test/data/imaging/pq/ht3/crn_clv_img.ht3'
filename_irf = '../../test/data/imaging/pq/ht3/crn_clv_mirror.ht3'

ht3_reading_parameter = {
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'default',
    "skip_before_first_frame_marker": True
}

data = tttrlib.TTTR(filename, 'HT3')
irf = tttrlib.TTTR(filename_irf, 'HT3')

clsm_image = tttrlib.CLSMImage(
    tttr_data=data,
    **ht3_reading_parameter
)
clsm_image_green = tttrlib.CLSMImage(
    source=clsm_image,
    tttr_data=data,
    channels=[0, 2],
    fill=True
)
clsm_image_red = tttrlib.CLSMImage(
    source=clsm_image,
    tttr_data=data,
    channels=[1, 3],
    fill=True
)

green = clsm_image_green.intensity.sum(axis=0)
red = clsm_image_red.intensity.sum(axis=0)


mean_tau_green = clsm_image_green.get_mean_lifetime_image(
    tttr_irf=irf[irf.get_selection_by_channel([0, 2])],
    tttr_data=data,
    minimum_number_of_photons=60,
    stack_frames=True
)

mask = (green < 30) + (red < 30)
masked_green = np.ma.masked_where(mask, green)
masked_red = np.ma.masked_where(mask, red)
masked_tau = np.ma.masked_where(mask, mean_tau_green.mean(axis=0))
lg_sg_sr = np.log(masked_green / masked_red)

fig, ax = p.subplots(nrows=2, ncols=2)
ax[0, 0].set_title('Green intensity')
ax[0, 1].set_title('Red intensity')
ax[1, 0].set_title('Mean green fl. lifetime')
ax[1, 1].set_title('Pixel histogram')
ax[1, 1].set_xlabel('tauG / ns')
ax[1, 1].set_ylabel('log(Sg/Sr')
ax[0, 0].imshow(green, cmap='cividis')
ax[0, 1].imshow(red, cmap='inferno')
ax[1, 0].imshow(mean_tau_green[0], cmap='Spectral')
ax[1, 1].hist2d(
    x=masked_tau.flatten(),
    y=lg_sg_sr.flatten(),
    range=((0.001, 4), (-2, 0.9)),
    bins=41
)
p.show()

