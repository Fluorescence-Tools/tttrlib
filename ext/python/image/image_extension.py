
@property
def intensity(self):
    return self.get_intensity_image()


@property
def line_duration(self):
    """ line duration in milliseconds
    """
    # this is in milliseconds
    header = self.header
    line_duration = (self[0][1][0].get_start_stop_time()[0] -
                     self[0][0][0].get_start_stop_time()[0]) * \
                    header.macro_time_resolution / 1e6
    return line_duration


@property
def pixel_duration(self):
    """ pixel duration in milliseconds
    """
    line = self[0][0]
    return line.get_duration() * \
           self.header.macro_time_resolution / \
           (1e3 * self.n_pixel)


def __repr__(self):
    return 'tttrlib.CLSMImage(%s, %s, %s)' % (
        self.n_frames,
        self.n_lines,
        self.n_pixel
    )


@property
def shape(self):
    return self.n_frames, self.n_lines, self.n_pixel


def __init__(
        self,
        tttr_data = None,
        marker_frame_start = None,
        marker_line_start = None,
        marker_line_stop = None,
        marker_event_type = 1,
        n_pixel_per_line = None,
        reading_routine = 'default',
        skip_before_first_frame_marker = False,
        **kwargs
):
# type: (tttrlib.TTTR, int, int, int, int, int, str, bool) -> None
    kwargs.update(
        {
            "reading_routine": reading_routine,
            "skip_before_first_frame_marker": skip_before_first_frame_marker
        }
    )
    if tttr_data is not None:
        header = tttr_data.header
        self.header = header
        if tttr_data.get_tttr_container_type() == 'PTU':
            try:
                kwargs["marker_frame_start"] = [2**(int(header.tag('ImgHdr_Frame')["value"])-1)]
            except IndexError:
                kwargs["marker_frame_start"] = [4]
            kwargs.update(
                {
                    "marker_line_start": 2**(int(header.tag('ImgHdr_LineStart')["value"]) -1 ),
                    "marker_line_stop": 2**(int(header.tag('ImgHdr_LineStop')["value"])-1),
                    "n_pixel_per_line": int(header.tag('ImgHdr_PixX')["value"]),
                    "marker_event_type": 1
                }
            )
        elif tttr_data.get_tttr_container_type() == 'HT3':
            kwargs.update(
                {
                    "marker_line_start": int(header.tag('ImgHdr_LineStart')["value"]),
                    "marker_line_stop": int(header.tag('ImgHdr_LineStop')["value"]),
                    "n_pixel_per_line": int(header.tag('ImgHdr_PixX')["value"]),
                    "marker_frame_start": [int(header.tag('ImgHdr_Frame')["value"])],
                    "marker_event_type": 1
                }
            )

    if reading_routine == 'SP5':
        kwargs["marker_event_type"] = 1
        kwargs["marker_frame_start"] = [4, 6]
    elif reading_routine == 'SP8':
        kwargs["marker_event_type"] = 15
        kwargs["marker_frame_start"] = [4, 6]
    if isinstance(marker_frame_start, int):
        kwargs['marker_frame_start'] = [marker_frame_start]
    if isinstance(marker_frame_start, list):
        kwargs['marker_frame_start'] = marker_frame_start
    if isinstance(marker_line_start, int):
        kwargs['marker_line_start'] = marker_line_start
    if isinstance(marker_line_stop, int):
        kwargs['marker_line_stop'] = marker_line_stop
    if isinstance(marker_event_type, int):
        kwargs['marker_event_type'] = marker_event_type
    if isinstance(n_pixel_per_line, int):
        kwargs['n_pixel_per_line'] = n_pixel_per_line
    kwargs['tttr_data'] = tttr_data
    this = _tttrlib.new_CLSMImage(**kwargs)
    try:
        self.this.append(this)
    except:
        self.this = this
