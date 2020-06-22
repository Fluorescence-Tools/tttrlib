def test_constructor(self):
    print("test_constructor")
    import tttrlib

    pixel = tttrlib.CLSMPixel()
    pixel.append(1)
    self.assertTupleEqual(tuple(pixel.tttr_indices), (1,))
    start, stop = 33, 55
    pixel = tttrlib.CLSMPixel(start, stop)
    self.assertTupleEqual(
        tuple(pixel.start_stop),
        (start, stop)
    )
    start, stop = 33, 55
    pixel = tttrlib.CLSMPixel(start, stop)
    self.assertTupleEqual(
        tuple(pixel.start_stop),
        (start, stop)
    )
#
# def test_copy_constructor(self):
#     print("test_copy_constructor")
#     data = self.sp5_data
#     reading_parameter = self.sp5_reading_parameter
#     data = tttrlib.TTTR('./data/leica/sp5/LSM_1.ptu', 'PTU')
#     reading_parameter = {
#         "tttr_data": data,
#         "frame_marker": [4, 6],
#         "line_start_marker": 1,
#         "line_stop_marker": 2,
#         "event_type_marker": 1,
#         "pixel_per_line": 256,
#         "reading_routine": 'SP5'
#     }
#     clsm_image = tttrlib.CLSMImage(*reading_parameter.values())
