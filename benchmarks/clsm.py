#!/usr/bin/python

import timeit
import tttrlib
import numpy as np

sp8_filename = '.../tttr-data/leica/sp8/da/G-28_C-28_S1_6_1.ptu'
sp8_data = tttrlib.TTTR(sp8_filename, 'PTU')
data = sp8_data

frame_marker = [4, 6]
line_start_marker = 1
line_stop_marker = 2
event_type_marker = 15
pixel_per_line = 0
reading_routine = 'SP8'

clsm_image = tttrlib.CLSMImage(
    data,
    frame_marker,
    line_start_marker,
    line_stop_marker,
    event_type_marker,
    pixel_per_line,
    reading_routine
)
clsm_image.fill_pixels(
    tttr_data=data,
    channels=[1]
)


def test_get_decay_image():
    decay_image = clsm_image.get_decay_image(
        tttr_data=data,
        tac_coarsening=256,
        stack_frames=False
    )


sel = np.zeros((512, 512), dtype=np.uint8)
selection = np.broadcast_to(
    sel,
    (
        clsm_image.n_frames,
        clsm_image.n_lines,
        clsm_image.n_pixel
    )
)
selection = np.ascontiguousarray(selection)


def test_make_clsm_image():
    clsm_images = tttrlib.CLSMImage(
            data,
            frame_marker,
            line_start_marker,
            line_stop_marker,
            event_type_marker,
            pixel_per_line,
            reading_routine
        )


def test_get_decays():
    decays = clsm_image.get_decays(
        tttr_data=data,
        tac_coarsening=256,
        stack_frames=True,
        selection=selection
    )


def test_fill_pixels():
    clsm_image.clear_pixels()
    clsm_image.fill_pixels(
        tttr_data=data,
        channels=[1]
    )


n_ph_min = 1
def test_get_mean_micro_time_image():
    mean_micro_time = clsm_image.get_mean_micro_time_image(
        data,
        n_ph_min,
        True
    )


if __name__ == '__main__':
    n_test_runs = 10

    # test_test_make_clsm_image = timeit.timeit(
    #     "test_make_clsm_image()",
    #     setup="from __main__ import test_make_clsm_image",
    #     number=n_test_runs
    # )
    #
    # test_get_decay_image_time = timeit.timeit(
    #     "test_get_decay_image()",
    #     setup="from __main__ import test_get_decay_image",
    #     number=n_test_runs
    # )
    #
    # test_fill_pixels_time = timeit.timeit(
    #     "test_fill_pixels()",
    #     setup="from __main__ import test_fill_pixels",
    #     number=n_test_runs
    # )
    #
    # test_get_decays_time = timeit.timeit(
    #     "test_get_decays()",
    #     setup="from __main__ import test_get_decays",
    #     number=n_test_runs
    # )
    #
    test_get_mean_micro_time_image_time = timeit.timeit(
        "test_get_mean_micro_time_image()",
        setup="from __main__ import test_get_mean_micro_time_image",
        number=n_test_runs
    )
    # print("test_test_make_clsm_image: %s" % (test_test_make_clsm_image / \
    #                                          n_test_runs))
    # print("test_get_decay_image_time: %s" % test_get_decay_image_time  / n_test_runs)
    # print("test_fill_pixels_time: %s" % (test_fill_pixels_time / n_test_runs))
    # print("test_get_decays_time: %s" % (test_get_decays_time / n_test_runs))
    print("test_get_mean_micro_time_image: %s" % (test_get_mean_micro_time_image_time /
                                           n_test_runs))
