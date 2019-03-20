Imaging
=======

Confocal laser scanning
-----------------------

Theory
++++++

In confocal microscopy the laser beam scans over the sample either by moving the sample over a parked (fixed) laser
beam (stage scanning) or by deflecting the laser and moving the position of the exciting on a fixed sample (laser
scanning). In both cases, the position of the detection volume within the sample needs to be saved in the recorded
TTTR event stream.

The position of the laser in the TTTR event stream is encoded by injecting markers into the event stream the report
on the laser position. Some manufactures present these markers as special ""events"" that are distinguished from
normal photon events. Some manufacturers use the same routing channel numbers for markers and for photon detection
channels. To distinguish photons from markers,``TTTR`` objects present for every event an additional event type
specifier (see :ref:`TTTR Objects:Anatomy`).

The position of the laser on the sample is mostly defined by the following markers

.. highlights::

    1. A frame marker
    2. A line start marker
    3. A line stop marker

The line start and the line stop marker define a *valid* region of the image. In confocal laser scanning microscopy
(CLSM) the laser beam is usually repositioned between the line stop and the line start marker. The detectors are
usually switched on in this period and register how the laser is fast repositioned on the sample.

The assignment of the markers to channels depends on the configuration of the microscope. Below it is briefly outlined
how these markers can be assigned to channels.

Below a typical traces of channel numbers for special events are shown.

.. plot:: plots/imaging_special_markers.py
   :include-source:

To the top a longer trace of special events channel numbers is shown. As can be seen by inspecting the traces of
special events, channel number 4 is followed by an interleaved sequence of channel number 1 and channel number 2.
This identifies channel 4 as frame number and channel 1 and 2 as, line start and line stop, respectively. As is shown
for the end of the special event channel trace, the last frame marker is followed by an incomplete frame. This is
not uncommon. Hence, the photons registered following the last frame number should be rejected, as they would result
in an incomplete image.


Image construction
^^^^^^^^^^^^^^^^^^

``tttrlib`` provides a set of functions to create images based on CLSM TTTR data. ``tttrlib`` provides efficient
functionality to generate FLIM images implemented in C/C++. Below, it is outlined, how an image can be only using the
basic functionality of ``tttrlib``. The presented outline presented below may serve to understand how the image
construction in CLSM work and is not intended for productive use.

Before creating an image the channel numbers of the frame and the line markers need to be determined. For the example
that is shown above the frame, line start, and line stop markers are 4, 1, and 2, respectively. Next, the TTTR data
needs to be loaded into memory, the number of frames, and the number of lines need to be determined in order to allocate
memory for the image. In CLSM the number of pixels per line can be arbitrarily defined, as the laser beam is
continuously displaced and the fluorescence of the sample is continuously recorded. Hence, the main experimental
determinants of the image size in memory are the number of frames and the number of line scans per frame. Usually,
the number of line scans per frame is constant within a TTTR file.

The number of frames can be determined by counting extracting and counting the number of frame markers as shown below.

.. code-block:: python

    import tttrlib
    import numpy as np
    import pylab as p

    def count_marker(channels, event_types, marker, event_type):
        n = 0
        for i, ci in enumerate(channels):
            if (ci == marker) and (event_types[i] == event_type):
                n += 1
        return n

    def find_marker(channels, event_types, maker, event_type=1):
        r = list()
        for i, ci in enumerate(channels):
            if (ci == maker) and (event_types[i] == event_type):
                r.append(i)
        return np.array(r)

    events = tttrlib.TTTR('./examples/PQ/HT3/PQ_HT3_CLSM.ht3', 1)
    e = events.get_event_type()
    c = events.get_routing_channel()
    t = events.get_macro_time()
    m = events.get_micro_time()

    frame_marker_list = find_marker(c, e, 4)
    line_start_marker_list = find_marker(c, e, 1)
    line_stop_marker_list = initialize(c, e, 2)
    n_frames = len(frame_marker_list) - 1 # 41
    n_line_start_marker = len(line_start_marker_list) # 10246
    n_lines_per_frame = n_line_start_marker / n_frames # 256
    line_duration_valid = t[line_stop_marker_list] - t[line_start_marker_list]
    line_duration_total = t[line_start_marker_list[1:]] - t[line_start_marker_list[0:-1]]
    n_pixel = 256
    pixel_duration = line_duration_valid // n_pixel
    line_duration_valid = t[line_stop_marker_list] - t[line_start_marker_list]

.. note::
    The channel number of the frame makers (here 4) depends on the experimental setup. Moreover, some
    setup configurations use "photons" event types to record special events.

In the example above, first the number of frames are counted. Next, the number of start line events are counted. In
the example, there are overall 41 frames are present in the file each having 256 lines. As the last frame is often
incomplete (see Figure above) the last frame is neglected (41 - 1 = 40). With the script above, the number of frames
``n_frames`` and the number of lines per frame ``n_lines_per_frame`` is determined. Next, the number of pixel per line
``n_pixel`` can be freely defined. Based on the time the laser spends in each line, the duration per pixel (the laser
is constantly scanning) needs to be calculated. Here, there are two options: 1) either the total time from the
beginning of each new line (line start) to the beginning of the next line is considered as a line or 2) the time
between the line start and the line stop is considered as the time base to calculate the pixel duration. In the first
case, the back movement of the laser to the line start can be visualized in the image. In the later case, only the
*valid* region where the laser scans over the sample is visualized. For most applications the later approach is
useful. To understand the microscope laser scanner the former approach is more useful. Above, ``line_duration_valid`` is
the time the laser spends in every of the lines in a valid region and ``line_duration_total`` is the total time the laser
spends in a line including the rewind to the line beginning. Above, ``n_pixel`` is the freely defined number of pixels
per line and ``pixel_duration`` is the duration of every pixel. With the number of frames ``n_frames``, the number
of pixels ``n_pixel``, and the number of lines ``n_lines_per_frame`` it is clear how much the memory for an image needs to be
can be allocated and with the defined number of pixels per line the duration for the pixel can be calculated for all
the lines of the frames.

With these numbers an image for a certain set of detector channels ``detector_channels`` can be calculated. Below this
is by the function ``make_image``.

.. code-block:: python

    def make_image(
            c, m, t, e,
            n_frames, n_lines_per_frame, pixel_duration,
            channels,
            frame_marker=4,
            start=1,
            stop=2,
            n_pixel=None,
            tac_coarsening=32,
            n_tac_max=2**15):
        if n_pixel is None:
            n_pixel = n_lines_per_frame  # assume squared image

        n_tac = n_tac_max / tac_coarsening
        image = np.zeros((n_frames, n_lines_per_frame, n_pixel, n_tac))
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
                elif ci == start:
                    time_start_line = ti
                    invalid_range = False
                    continue
                elif ci == stop:
                    invalid_range = True
                    current_line += 1
                    continue
            elif ei == 0:  # photon
                if ci in channels and (not invalid_range or not mask_invalid):
                    pixel = int((ti - time_start_line) // pixel_duration[current_line])
                    if pixel < n_pixel:
                        tac = mi / tac_coarsening
                        image[frame, current_line, pixel, tac] += 1
        return image

    image = make_image(
        c,
        m,
        t,
        e,
        n_frames,
        n_lines_per_frame,
        pixel_duration,
        channels=np.array([0, 1])
    )

In the example function ``make_image`` the an 3D array is created that contains in every pixel a histogram of the
micro times. An histogram of the micro time can be displayed by the code shown below:


.. code-block:: python

    fig, ax = p.subplots(1, 2)
    ax[0].imshow(image.sum(axis=(0, 3)), cmap='inferno')
    ax[1].plot(image.sum(axis=0)[175,128])
    p.show()

The outcome of such analysis for a complete working example is shown below including all necessary source code.

.. plot:: plots/imaging_tutorial.py


For any practical applications it is recommended the determine the images using the built-in functions of ``tttrlib``.
Using this functions is illustrated below.


C/C++ interface
^^^^^^^^^^^^^^^


As was pointed out above based on some lines of Python source code (see :ref:`Imaging:Confocal laser scanning:Image construction:Theory`)
to construct an image

    1. the frame marker
    2. the line start marker
    3. the line stop marker
    4. the detector channel numbers
    5. the number of pixels per scanning line

need to be specified. Based on these parameters, the indices of the photons in the TTTR data stream are assigned to
frames, lines, and pixels. When creating a ``CLSMImage`` object with a ``TTTR`` object that contains the photon stream
a set of ``CLSMFrame``, ``CLSMLine``, and ``CLSMPixel`` objects are create.

.. code-block:: python

    from __future__ import print_function
    import tttrlib
    import numpy as np
    import pylab as p

    data = tttrlib.TTTR('./examples/PQ/HT3/PQ_HT3_CLSM.ht3', 1)

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


As illustrated by the code shown below, every ``CLSMImage`` object may contain multiple ``CLSMFrame`` objects , every
``CLSMFrame`` contain a set of ``CLSMLine`` objects, and every ``CLSMLine`` object contains multiple ``CLSMPixel``
objects. The number of ``CLSMPixel`` objects per line is specified upon instantiation if the ``CLSMImage`` object (see
code example above). The ``CLSMFrame``, ``CLSMLine``, and the ``CLSMPixel`` classes derive from the ``TTTRRange`` class
and provide access to the associated TTTR indices that mark the beginning and the end of the respective object via the
function ``get_start_stop`` (see example below).

.. code-block:: python

    frames = image.get_frames()
    frame = frames[20]

    print("Frame")
    print("-----")
    print("start, stop: ", frame.get_start_stop())
    print("start time, stop time: ", frame.get_start_stop_time())
    print("duration: ", frame.get_start_stop_time())

    lines = frame.get_lines()
    line = lines[50]
    print("Line")
    print("-----")
    print("start, stop: ", line.get_start_stop())
    print("start time, stop time: ", line.get_start_stop_time())
    print("duration: ", line.get_start_stop_time())

    pixels = line.get_pixels()
    pixel = pixels[100]
    print("Pixel")
    print("-----")
    print("start, stop: ", pixel.get_start_stop())
    print("start time, stop time: ", pixel.get_start_stop_time())
    print("duration: ", pixel.get_start_stop_time())



Object of the ``CLSMImage`` class store the frame, line, and pixel location of the TTTR data stream that was used to
create the ``CLSMImage`` object. Next, to determine images, the detection channels of interest need to be specified
using the method ``fill_pixels``. The method ``fill_pixels`` populates the

.. note::
    The pixels are not filled with start and stop indices and associated start and stop times, as the channels of
    the image have not been defined.

To fill the pixels, it has to be defined, which detection channels are used. Next, the pixels can be filled. When
filling the pixels, to every pixel a start and stop time in the TTTR data stream is associated.

.. code-block:: python

    channels = (0, 1)
    image.fill_pixels(data, (0, 1))

    print("Pixel")
    print("-----")
    print("start, stop: ", pixel.get_start_stop())
    print("start time, stop time: ", pixel.get_start_stop_time())
    print("duration: ", pixel.get_start_stop_time())

    image_intensity = image.get_intensity_image()
    image_decay = image.get_decay_image(data, 32)

    p.imshow(image_intensity.sum(axis=0))
    p.show()

    p.semilogy(image_decay.sum(axis=(0,1,2)))
    p.show()


To yield the mean time between excitation and detection of fluorescence the method ``get_mean_tac_image`` can be used.
The example shown below shows the counts per pixel for all frames (top, left), the counts per pixel for frame number 30
(top, right), and the mean time between excitation and detection of fluorescence (bottom, left). The function
``get_mean_tac_image`` takes in addition to the TTTR data an argument that discriminates pixels with less than a
certain amount of photons (below 3 photons). As can be seen by this analysis, the mean time between excitation and
detection of fluorescence is fairly constant over the cell, while the intensity varies in this particular sample.

For more detailed analysis the fluorescence decays contained in the 4D image (frame, x, y, fluorescence decay) returned
by ``get_decay_image`` can be used, e.g., by analyzing fluorescence decay histograms. The fluorescence decay containing
all photons of frame 30 is shown to the bottom right.


.. plot:: plots/imaging_tutorial_2.py

