"""
==========================
TTTR Image representations
==========================

This example opens a CLSM TTTR file and creates different representations
of the data contained in the file. The example creates

1. an intensity image
2. an image of the mean micro times
3. an fluorescence intensity decay of the micro times

"""
from __future__ import print_function
import tttrlib
import pylab as p

data = tttrlib.TTTR('../../tttr-data/imaging/pq/ht3/pq_ht3_clsm.ht3')


#%%
# reads header but does not always work, frame marker, line marker, etc.
# more details (in case it does not work without providing markers)
clsm_settings = {
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'default',
    "skip_before_first_frame_marker": True
}
image = tttrlib.CLSMImage(data, **clsm_settings)

#%%
#
channels = (0, 1)
image.fill_pixels(data, channels)


#%%
# Frames, lines, pixel
image_intensity = image.get_intensity_image()
image_intensity_sum = image_intensity.sum(axis=0)


#%%
#
image_mean_micro_time = image.get_mean_micro_time_image(
    data,
    minimum_number_of_photons=3,
    stack_frames=True
)


#%%
# Frames, lines, pixel, micro time channel
# Coarsening micro time resolution / 256
image_decay = image.get_fluorescence_decay_image(data, 256)

#%%
#
image_decay_sum = image_decay.sum(axis=0)


#%%
# Plot images

fig, ax = p.subplots(2, 2, sharex=False, sharey=False)
im = ax[0, 0].imshow(image_intensity[30])
ax[0, 1].set_title('Intensity of a single frame')
fig.colorbar(im, ax=ax[0, 1])
im = ax[0, 1].imshow(image_intensity_sum)
ax[0, 0].set_title('Intensity of integrated frames')
fig.colorbar(im, ax=ax[0, 0])
im = ax[1, 0].imshow(image_mean_micro_time[0])
ax[1, 0].set_title('Mean average time of single frame / ns')
fig.colorbar(im, ax=ax[1, 0])
im = ax[1, 1].semilogy(image_decay[30].sum(axis=(0, 1)))
p.show()
