"""
=============================
Computing ICS data by tttrlib
=============================

Demonstrate the use of the tttrlib ICS features and compare to
numpy ICS implementation use CLSMImage as an input
"""
import tttrlib
import numpy as np
import matplotlib.pylab as p
import matplotlib.patches


data = tttrlib.TTTR('../../tttr-data/imaging/leica/sp5/LSM_1.ptu', 'PTU')
reading_parameter = {
    "tttr_data": data,
    "reading_routine": 'SP5',
    "channels": [0],
    "fill": True
}
clsm = tttrlib.CLSMImage(**reading_parameter)

# # specifies a selection on the image
x_range = [120, 152]
y_range = [100, 132]

# if nothing or the following ranges are specified
# an ICS of the entire image is computed
# x_range = [0, -1]
# y_range = [0, -1]

# the ICS routines computes the correlation between pairs or
# images. This way the CCF between different images can be computed, e.g.,
# to compute the correlation between different frames.
# If no image pairs are specified the ACF of the images is computed.
frames = np.arange(0, clsm.n_frames)
frame_shift = 1

ics_parameter = {
    'tttr_data': data,
    'x_range': x_range,
    'y_range': y_range,
    'clsm': clsm,
    'frames_index_pairs': list(
        zip(
            frames.tolist(),
            np.roll(frames, frame_shift).tolist()
        )
    ),
    'subtract_average': "stack"
}
ics = tttrlib.CLSMImage.compute_ics(**ics_parameter)

img = clsm.intensity
ics_mean = ics.mean(axis=0)
ics_std = ics.std(axis=0)

fig, ax = p.subplots(ncols=2)
ax[0].imshow(img.sum(axis=0), cmap='inferno')
rect = matplotlib.patches.Rectangle(
    xy=(x_range[0], y_range[0]),
    width=x_range[1]-x_range[0],
    height=y_range[1]-y_range[0],
    edgecolor='r',
    facecolor="none"
)
ax[0].add_patch(rect)
ax[1].imshow(
    np.fft.fftshift(ics_mean),
    cmap='inferno',
    vmax=0.05
)
p.show()
