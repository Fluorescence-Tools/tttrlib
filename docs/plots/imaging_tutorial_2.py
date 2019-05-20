from __future__ import print_function
import tttrlib
import pylab as p

data = tttrlib.TTTR('../..//examples/PQ/HT3/PQ_HT3_CLSM.ht3', 1)

frame_marker = 4
line_start_marker = 1
line_stop_marker = 2
event_type_marker = 1
pixel_per_line = 256
image = tttrlib.CLSMImage(data,
                          frame_marker,
                          line_start_marker,
                          line_stop_marker,
                          event_type_marker,
                          pixel_per_line)

channels = (0, 1)
image.fill_pixels(data, channels)

image_intensity = image.get_intensity_image()

fig, ax = p.subplots(2, 2, False, False, True)

im = ax[0, 0].imshow(image_intensity.sum(axis=0))
#p.colorbar(im, ax=ax[0])

im = ax[0, 1].imshow(image_intensity[30])
#p.colorbar(im, ax=ax[1])

image_mean_tac = image.get_mean_tac_image(data, 3)
im = ax[1, 0].imshow(image_mean_tac[30] / 1000)
#p.colorbar(im, ax=ax[2])

image_decay = image.get_decay_image(data, 32)

im = ax[1, 1].semilogy(image_decay[30].sum(axis=(0, 1)))

p.show()
