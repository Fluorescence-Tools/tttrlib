from __future__ import division


import unittest
import tttrlib
import numpy as np


import tttrlib
filename = './data/leica/sp5/LSM_1.ptu'
data = tttrlib.TTTR(filename, 'PTU')
reading_parameter = {
    "tttr_data": data,
    "marker_frame_start": [4, 6],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'SP5',
    "channels": [0],
    "fill": True
}
clsm_image_1 = tttrlib.CLSMImage(**reading_parameter)
# clsm_image_2 = tttrlib.CLSMImage(source=clsm_image_1, fill=True)
#
# for i_pixel in range(len(clsm_image_2[0][100])):
#     pixel = clsm_image_2[0][100][i_pixel]
#     print(i_pixel, ":", len(pixel.tttr_indices))
#
# pixel = clsm_image_2[0][100][105]
#
img = clsm_image_1.intensity
# frame = 0
# line = 150
# for i_pixel in range(len(clsm_image_2[frame][line])):
#     pixel = clsm_image_2[frame][line][i_pixel]
#     pixel_copy = clsm_image_2[frame][line][i_pixel]
#     print(np.array(pixel.tttr_indices) - np.array(pixel_copy.tttr_indices))
#     if(img[frame][line][i_pixel] != len(pixel.tttr_indices)):
#         print("Error", i_pixel)


fcs_image = clsm_image_1.get_fcs_image(
    tttr_data=data,
    tttr_data_other=data,
    clsm_other=clsm_image_1
)
p.imshow(fcs_image[0,:,:,1])
p.show()

p.imshow(fcs_image[0,:,:,7:].sum(axis=2) * img)
p.show()

# decay_image = clsm_image_1.get_fluorescence_decay_image(
#     tttr_data=data,
#     tac_coarsening=256,
# )