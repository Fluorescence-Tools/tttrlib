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


p.show()
