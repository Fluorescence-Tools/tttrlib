import numpy as np
import pylab as p

import tttrlib
import numba as nb
import scipy.signal
import pylab as p


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
    f1 = np.fft.fft2(image_1)
    f2 = np.fft.fft2(image_2)
    f1f2p = np.real(f1 * np.conjugate(f2))
    af12, af22 = np.abs(f1)**2, np.abs(f2)**2
    nx, ny = image_1.shape

    @nb.jit
    def count(wf1f2, wf1, wf2, nx, ny, f1f2p, af12, af22):
        for xi in range(-np.floor(nx // 2), np.ceil(nx / 2)):
            for yi in range(-np.floor(ny // 2), np.ceil(ny / 2)):
                distance_bin = int(np.sqrt(xi**2 + yi**2) / bin_width)
                wf1f2[distance_bin] += f1f2p[xi, yi]
                wf1[distance_bin] += af12[xi, yi]
                wf2[distance_bin] += af22[xi, yi]
        return wf1f2 / np.sqrt(wf1 * wf2)

    bins = np.arange(0, np.sqrt((nx//2)**2 + (ny//2)**2), bin_width)
    n_bins = int(bins.shape[0])
    wf1f2 = np.zeros(n_bins, np.float)
    wf1 = np.zeros(n_bins, np.float)
    wf2 = np.zeros(n_bins, np.float)
    density = count(wf1f2, wf1, wf2 ,nx, ny, f1f2p, af12, af22)

    return density, bins

def subpixel_images(
        clsm_image,
        channels=[0],
        pixel_fraction: int = 4,
        number_of_pixels: int = 1
):
    # Shift the line starts by +/- one pixel in 32 steps
    pixel_duration = clsm_image[0][0].pixel_duration
    sub_pixel_shifts = np.arange(
        -pixel_duration * number_of_pixels,
        pixel_duration * number_of_pixels,
        pixel_duration / pixel_fraction, dtype=np.int
    )
    # A triangular weighting function to dampen the contribution of
    # the neigboring shifted images
    weights = np.clip(
        (pixel_duration-abs(sub_pixel_shifts)) / pixel_duration,
        0, 1
    )

    shifted_images_join = list()
    shifted_images_set1 = list()
    shifted_images_set2 = list()
    clsm_image.shift_line_start(int(min(sub_pixel_shifts)))
    delta_shift = np.diff(sub_pixel_shifts)
    for shift in delta_shift:
        clsm_image.fill_pixels(tttr_data=data, channels=channels)
        imgs_join = clsm_image.get_intensity_image()
        img_1, img_2 = imgs_join[::2].sum(axis=0), imgs_join[1::2].sum(axis=0)
        img_join = imgs_join.sum(axis=0)
        shifted_images_join.append(img_join)
        shifted_images_set1.append(img_1)
        shifted_images_set2.append(img_2)
        clsm_image.shift_line_start(int(shift))

    return shifted_images_join, \
           shifted_images_join, \
           shifted_images_set2, \
           weights

def savgol_filter(
        join,
        set1,
        set2,
        savgol_filter_window_length: int = 5,
        savgol_filter_polyorder: int = 3
):
    # interpolate the
    join = np.array(join, dtype=np.float)
    set1 = np.array(set1, dtype=np.float)
    set2 = np.array(set2, dtype=np.float)

    set1 = scipy.signal.savgol_filter(
        set1,
        window_length=savgol_filter_window_length,
        polyorder=savgol_filter_polyorder,
        axis=0
    )
    set2 = scipy.signal.savgol_filter(
        set2,
        window_length=5,
        polyorder=3,
        axis=0
    )
    join = scipy.signal.savgol_filter(
        join,
        window_length=5,
        polyorder=3,
        axis=0
    )
    return join, set1, set2

sp5_filename = './data/leica/sp5/convolaria_1.ptu'
data = tttrlib.TTTR(sp5_filename, 'PTU')
reading_parameter = {
    "data": data,
    "frame_marker": [4, 6],
    "line_start_marker": 1,
    "line_stop_marker": 2,
    "event_type_marker": 1,
    "pixel_per_line": 512,
    "reading_routine": 'SP5'
}

clsm_image = tttrlib.CLSMImage(*reading_parameter.values())
clsm_image.fill_pixels(tttr_data=data, channels=[0])

imgs = clsm_image.get_intensity_image()
join, set1, set2 = imgs, imgs[::2], imgs[1::2]
imgs, set1, set2 = savgol_filter(join, set1, set2)
join = join.sum(axis=0)
set1 = set1.sum(axis=0)
set2 = set2.sum(axis=0)
frc_sub_density_12, frc_bins = compute_frc(set1, set1)


img_sub_join, img_sub_1, img_sub_2, weights = subpixel_images(
    clsm_image,
    pixel_fraction=4,
    number_of_pixels=2
)
sub_join, sub_set1, sub_set2 = combine(
    img_sub_join,
    img_sub_1,
    img_sub_2,
    weights=weights
)

frc_binwidth=6
a = sub_set1 / sub_set1.sum()
b = sub_set2 / sub_set2.sum()
p.imshow(a - b)
p.show()
frc_density, _ = compute_frc(
    set1, set2,
    bin_width=frc_binwidth
)
a = sub_set1 / sub_set1.sum()
b = sub_set2 / sub_set2.sum()
p.imshow(a - b)
p.show()
frc_sub_density_12, frc_bins = compute_frc(a, b, bin_width=frc_binwidth)
p.plot(frc_bins, frc_sub_density_12)
p.show()

# Axes images share axis
ax_img = p.subplot(131)
ax_fil = p.subplot(132, sharex=ax_img, sharey=ax_img)
ax_frc = p.subplot(133)
ax_img.imshow(join, cmap='inferno')
ax_fil.imshow(sub_join, cmap='inferno')
ax_frc.plot(frc_bins, frc_sub_density_12, label='frc_sub_density_12')
ax_frc.plot(frc_bins, frc_density, label='frc_density')
ax_frc.legend()
p.show()

