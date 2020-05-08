import pylab as p
from matplotlib.pyplot import imread
import tttrlib
import numpy as np

tttr_data = tttrlib.TTTR('../../test/data/imaging/pq/ht3/pq_ht3_clsm.ht3', 'HT3')
channels = (0, 1)
reading_parameter = {
    "tttr_data": tttr_data,
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256, # if zero n_pixel_per_line = n_lines
    "reading_routine": 'default',
    "fill": True,
    "channels": channels,
    "skip_before_first_frame_marker": True
}
clsm_image = tttrlib.CLSMImage(**reading_parameter)
mask = imread("../../test/data/imaging/misc/clsm_mask.png").astype(np.uint8)
selection = np.ascontiguousarray(
    np.broadcast_to(
        mask,
        (clsm_image.n_frames, clsm_image.n_lines, clsm_image.n_pixel)
    )
)
kw = {
    "tttr_data": tttr_data,
    "selection": selection,
    "tac_coarsening": 16,
    "stack_frames": False
}
decay = clsm_image.get_average_decay_of_pixels(**kw)
intensity = clsm_image.intensity
intensity_stacked = clsm_image.intensity.sum(axis=0)


# mask small intensities to have transparency
masked_intensity_stacked = np.ma.masked_where(
    intensity_stacked < 0.9,
    intensity_stacked
)
masked_intensity = np.ma.masked_where(
    intensity < 0.9,
    intensity
)
mask = np.ma.masked_where(mask.T < 0.9, mask.T)

fig, ax = p.subplots(nrows=1, ncols=3, sharex=False, sharey=False)
im = ax[0].imshow(intensity_stacked, vmin=0.1)
ax[0].set_title('Stacked frames')
im = ax[0].imshow(mask, vmin=0.1)
ax[1].set_title('Pixel selection mask')
im = ax[2].semilogy(decay.sum(axis=0), label='Mask: Stacked frames')


im = ax[1].imshow(masked_intensity[0], vmin=0.1)
ax[1].set_title('First frame')
im = ax[1].imshow(mask, vmin=0.1)
im = ax[2].semilogy(decay[0], label='Mask: First frame')
ax[2].legend()
p.show()
