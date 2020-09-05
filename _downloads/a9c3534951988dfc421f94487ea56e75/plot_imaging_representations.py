"""
=========================
TTTR Image representations
=========================

This example opens a CLSM TTTR file and creates different image representations
of the data contained in the file. The example creates

1. an intensity image
2. an image of the mean micro times
3. an fluorescence intensity decay of the micro times

"""
from __future__ import print_function
import tttrlib
import pylab as p

data = tttrlib.TTTR('../../test/data/imaging/pq/ht3/pq_ht3_clsm.ht3', 1)

frame_marker = [4]
line_start_marker = 1
line_stop_marker = 2
event_type_marker = 1
pixel_per_line = 256
reading_routine = 'default'
image = tttrlib.CLSMImage(
    data,
    frame_marker,
    line_start_marker,
    line_stop_marker,
    event_type_marker,
    pixel_per_line,
    reading_routine,
    skip_before_first_frame_marker=True
)

channels = (0, 1)

image.fill_pixels(data, channels)
image_intensity = image.get_intensity_image()
image_intensity_sum = image_intensity.sum(axis=0)

fig, ax = p.subplots(2, 2, sharex=False, sharey=False)

im = ax[0, 0].imshow(image_intensity[30])
ax[0, 1].set_title('Intensity of a single frame')
fig.colorbar(im, ax=ax[0, 1])

im = ax[0, 1].imshow(image_intensity_sum)
ax[0, 0].set_title('Intensity of integrated frames')
fig.colorbar(im, ax=ax[0, 0])

image_mean_micro_time = image.get_mean_micro_time_image(
    data,
    minimum_number_of_photons=3,
    stack_frames=True
)
im = ax[1, 0].imshow(image_mean_micro_time[0])
ax[1, 0].set_title('Mean average time of single frame / ns')
fig.colorbar(im, ax=ax[1, 0])

image_decay = image.get_fluorescence_decay_image(data, 256)
image_decay_sum = image_decay.sum(axis=0)
im = ax[1, 1].semilogy(image_decay[30].sum(axis=(0, 1)))

p.show()
