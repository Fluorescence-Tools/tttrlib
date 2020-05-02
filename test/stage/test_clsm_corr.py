from __future__ import division
import unittest

import tttrlib
import numpy as np


def compute_frc(
        image_1: np.ndarray,
        image_2: np.ndarray,
        bin_width: int = 2.0
):
    """ Computes the Fourier Ring/Shell Correlation of two 2-D images

    :param image_1:
    :param image_2:
    :param bin_width:
    :return:
    """
    image_1 = image_1 / np.sum(image_1)
    image_2 = image_2 / np.sum(image_2)
    f1, f2 = np.fft.fft2(image_1), np.fft.fft2(image_2)
    af1f2 = np.real(f1 * np.conj(f2))
    af1_2, af2_2 = np.abs(f1)**2, np.abs(f2)**2
    nx, ny = af1f2.shape
    x = np.arange(-np.floor(nx / 2.0), np.ceil(nx / 2.0))
    y = np.arange(-np.floor(ny / 2.0), np.ceil(ny / 2.0))
    distances = list()
    wf1f2 = list()
    wf1 = list()
    wf2 = list()
    for xi, yi in np.array(np.meshgrid(x,y)).T.reshape(-1, 2):
        distances.append(np.sqrt(xi**2 + xi**2))
        xi = int(xi)
        yi = int(yi)
        wf1f2.append(af1f2[xi, yi])
        wf1.append(af1_2[xi, yi])
        wf2.append(af2_2[xi, yi])

    bins = np.arange(0, np.sqrt((nx//2)**2 + (ny//2)**2), bin_width)
    f1f2_r, bin_edges = np.histogram(
        distances,
        bins=bins,
        weights=wf1f2
    )
    f12_r, bin_edges = np.histogram(
        distances,
        bins=bins,
        weights=wf1
    )
    f22_r, bin_edges = np.histogram(
        distances,
        bins=bins,
        weights=wf2
    )
    density = f1f2_r / np.sqrt(f12_r * f22_r)
    return density, bin_edges


filename = '../../test/data/imaging/pq/ht3/crn_clv_img.ht3'
data = tttrlib.TTTR(filename, 'HT3')
line_factor = 16
reading_parameter = {
    "tttr_data": data,
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256 * line_factor,
    "reading_routine": 'default',
    "channels": [0, 1],
    "fill": True
}
clsm_image = tttrlib.CLSMImage(**reading_parameter)

fcs_parameter = {
    'tttr': data,
    'n_bins': 10,
    'n_casc': 1,
    'stack_frames': False,
    'normalized_correlation': False,
    'min_photons': 3
}
x_min, x_max = 17 * line_factor, 30 * line_factor
y_min, y_max = 90+50, 200-40

fig, ax = p.subplots(2, 2, sharex=False, sharey=False)
ax[0, 0].set_title('Average pixel ACF')
ax[1, 0].set_title('FRC of region')
ax[0, 1].set_title('Intensity')
ax[1, 1].set_title('Correlation amplitudes')

fcs_image = clsm_image.get_fcs_image(**fcs_parameter)
im1 = fcs_image[::2].sum(axis=0)[y_min:y_max, x_min:x_max, 4]
im2 = fcs_image[1::2].sum(axis=0)[y_min:y_max, x_min:x_max, 4]
frc, frc_bins = compute_frc(im1, im2)
ax[1, 0].plot(frc, label="FCS")

img = clsm_image.intensity
im1 = img[::2].sum(axis=0)[y_min:y_max, x_min:x_max]
im2 = img[1::2].sum(axis=0)[y_min:y_max, x_min:x_max]
frc, frc_bins = compute_frc(im1, im2)
ax[1, 0].plot(frc, label="Int")

ax[0, 0].plot(fcs_image.mean(axis=(0, 1, 2)))
ax[0, 1].imshow(clsm_image.intensity.mean(axis=0)[y_min:y_max, x_min:x_max], aspect=line_factor)
ax[1, 1].imshow(fcs_image.mean(axis=0)[y_min:y_max, x_min:x_max, 4], aspect=line_factor)

ax[1, 0].legend()
p.show()