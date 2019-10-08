import tttrlib
import numpy as np
import pylab as p
import numba as nb


@nb.jit
def count_marker(channels, event_types, marker, event_type):
    n = 0
    for i, ci in enumerate(channels):
        if (ci == marker) and (event_types[i] == event_type):
            n += 1
    return n


@nb.jit
def find_marker(channels, event_types, maker, event_type=1):
    r = list()
    for i, ci in enumerate(channels):
        if (ci == maker) and (event_types[i] == event_type):
            r.append(i)
    return np.array(r)


@nb.jit
def make_image(
        c, m, t, e,
        n_frames, n_lines, pixel_duration,
        channels,
        frame_marker=4,
        line_start=1,
        line_stop=2,
        n_pixel=None,
        tac_coarsening=32,
        n_tac_max=2**15):
    if n_pixel is None:
        n_pixel = n_lines  # assume squared image

    n_tac = n_tac_max / tac_coarsening
    image = np.zeros((n_frames, n_lines, n_pixel, n_tac))
    # iterate through all photons in a line and add to image

    frame = -1
    current_line = 0
    time_start_line = 0
    invalid_range = True
    mask_invalid = True
    for ci, mi, ti, ei in zip(c, m, t, e):
        if ei == 1:  # marker
            if ci == frame_marker:
                frame += 1
                current_line = 0
                if frame < n_frames:
                    continue
                else:
                    break
            elif ci == line_start:
                time_start_line = ti
                invalid_range = False
                continue
            elif ci == line_stop:
                invalid_range = True
                current_line += 1
                continue
        elif ei == 0:  # photon
            if ci in channels and (not invalid_range or not mask_invalid):
                pixel = int((ti - time_start_line) // pixel_duration[current_line])
                if pixel < n_pixel:
                    tac = mi // tac_coarsening
                    image[frame, current_line, pixel, tac] += 1
    return image


events = tttrlib.TTTR('../../data/PQ/HT3/PQ_HT3_CLSM.ht3', 1)
e = events.get_event_type()
c = events.get_routing_channel()
t = events.get_macro_time()
m = events.get_micro_time()

frame_marker_list = find_marker(c, e, 4)
line_start_marker_list = find_marker(c, e, 1)
line_stop_marker_list = find_marker(c, e, 2)
n_frames = len(frame_marker_list) - 1 # 41
n_line_start_marker = len(line_start_marker_list) # 10246
n_lines = n_line_start_marker / n_frames # 256
line_duration_valid = t[line_stop_marker_list] - t[line_start_marker_list]
line_duration_total = t[line_start_marker_list[1:]] - t[line_start_marker_list[0:-1]]
n_pixel = 256
pixel_duration = line_duration_valid // n_pixel
line_duration_valid = t[line_stop_marker_list] - t[line_start_marker_list]

image = make_image(
    c, m, t, e,
    n_frames,
    n_lines,
    pixel_duration,
    channels=np.array([0, 1]),
    tac_coarsening=128
)

fig, ax = p.subplots(1, 2)
ax[0].imshow(image.sum(axis=(0, 3)), cmap='inferno')
ax[1].semilogy(image.sum(axis=(0, 1))[128])
p.show()
