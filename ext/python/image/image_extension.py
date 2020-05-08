
@property
def intensity(self):
    return self.get_intensity_image()


def __repr__(self):
    return 'tttrlib.CLSMImage(%s, %s, %s)' % (
        self.n_frames,
        self.n_lines,
        self.n_pixel
    )


def __init__(
        self,
        tttr_data: tttrlib.TTTR = None,
        marker_frame_start: int = None,
        marker_line_start: int = None,
        marker_line_stop: int = None,
        marker_event_type: int = 1,
        n_pixel_per_line: int = None,
        reading_routine: str = 'default',
        skip_before_first_frame_marker: bool = False,
        **kwargs
):
    kwargs.update(
        {
            "reading_routine": reading_routine,
            "skip_before_first_frame_marker": skip_before_first_frame_marker
        }
    )
    if tttr_data is not None:
        if tttr_data.get_tttr_container_type() == 'PTU':
            header = tttr_data.header
            # Markers necessary to make FLIM image stack
            try:
                kwargs["marker_frame_start"] = [2**(int(header.data['ImgHdr_Frame'])-1)]
            except IndexError:
                kwargs["marker_frame_start"] = [4]
            kwargs.update(
                {
                    "marker_line_start": 2**(int(header.data['ImgHdr_LineStart'])-1),
                    "marker_line_stop": 2**(int(header.data['ImgHdr_LineStop'])-1),
                    "n_pixel_per_line": int(header.data['ImgHdr_PixX']),
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
