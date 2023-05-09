@property
def shape(self):
    return self.n_frames, self.n_lines, self.n_pixel

def __repr__(self):
    return 'tttrlib.CLSMImage(%s, %s, %s)' % (
        self.n_frames,
        self.n_lines,
        self.n_pixel
    )

def __getattr__(self, item):
    """
    If an attribute `attribute` is accesses that does not exist
    the corresponding getter by calling 'get_attribute' is called

    :param self:
    :param item:
    :return:
    """
    item = "get_" + str(item)
    if hasattr(self.__class__, item):
        call = getattr(self, item)
        return call()
    else:
        raise AttributeError

def __init__(
        self,
        tttr_data=None,
        marker_frame_start=None,
        marker_line_start=None,
        marker_line_stop=None,
        marker_event_type=1,
        n_pixel_per_line=None,
        reading_routine='default',
        skip_before_first_frame_marker=False,
        skip_after_last_frame_marker=False,
        **kwargs
):
    source = kwargs.get('source', None)
    rt = {
        'SP8': CLSM_SP8,
        'SP5': CLSM_SP5,
        'default': CLSM_DEFAULT
    }

    settings_kwargs = {
        "skip_before_first_frame_marker": skip_before_first_frame_marker,
        "skip_after_last_frame_marker": skip_after_last_frame_marker,
        "reading_routine": rt[reading_routine],
        "marker_line_start": marker_line_start,
        "marker_line_stop": marker_line_stop,
        "marker_frame_start": marker_frame_start,
        "marker_event_type": marker_event_type,
        "n_pixel_per_line": n_pixel_per_line
    }

    if not isinstance(source, CLSMImage):
        settings_kwargs.update(
            {
                "marker_frame_start": marker_frame_start,
                "marker_line_start": marker_line_start,
                "marker_line_stop": marker_line_stop,
                "marker_event_type": marker_event_type,
                "n_pixel_per_line": n_pixel_per_line,
                "reading_routine": rt[reading_routine],
                "skip_before_first_frame_marker": skip_before_first_frame_marker
            }
        )
        if tttr_data is not None:
            header = tttr_data.header
            self.header = header
            if tttr_data.get_tttr_container_type() == 'PTU':
                try:
                    settings_kwargs["marker_frame_start"] = [2**(int(header.tag('ImgHdr_Frame')["value"])-1)]
                except:
                    settings_kwargs["marker_frame_start"] = [8]
                settings_kwargs.update(
                    {
                        "marker_line_start": 2**(int(header.tag('ImgHdr_LineStart')["value"])-1),
                        "marker_line_stop": 2**(int(header.tag('ImgHdr_LineStop')["value"])-1),
                        "n_pixel_per_line": int(header.tag('ImgHdr_PixX')["value"]),
                        "n_lines": int(header.tag('ImgHdr_PixY')["value"]),
                        "marker_event_type": 1
                    }
                )
            elif tttr_data.get_tttr_container_type() == 'HT3':
                settings_kwargs.update(
                    {
                        "marker_line_start": int(header.tag('ImgHdr_LineStart')["value"]),
                        "marker_line_stop": int(header.tag('ImgHdr_LineStop')["value"]),
                        "n_pixel_per_line": int(header.tag('ImgHdr_PixX')["value"]),
                        "n_lines": int(header.tag('ImgHdr_PixY')["value"]),
                        "marker_frame_start": [int(header.tag('ImgHdr_Frame')["value"])],
                        "marker_event_type": 1
                    }
                )

        # Overwrite if user defined inputs make sense
        if isinstance(marker_frame_start, int):
            settings_kwargs['marker_frame_start'] = [marker_frame_start]
        if isinstance(marker_frame_start, list):
            settings_kwargs['marker_frame_start'] = marker_frame_start
        if isinstance(marker_line_start, int):
            settings_kwargs['marker_line_start'] = marker_line_start
        if isinstance(marker_line_stop, int):
            settings_kwargs['marker_line_stop'] = marker_line_stop
        if isinstance(marker_event_type, int):
            settings_kwargs['marker_event_type'] = marker_event_type
        if isinstance(n_pixel_per_line, int):
            settings_kwargs['n_pixel_per_line'] = n_pixel_per_line
        kwargs['tttr_data'] = tttr_data

        # Defined setups overrule all setting
        if reading_routine == 'SP5':
            settings_kwargs["marker_event_type"] = 1
            settings_kwargs["marker_frame_start"] = [4, 6]
            settings_kwargs["marker_line_start"] = 1
            settings_kwargs["marker_line_stop"] = 2
        elif reading_routine == 'SP8':
            settings_kwargs["marker_event_type"] = 15
            settings_kwargs["marker_frame_start"] = [4, 6]
            settings_kwargs["marker_line_start"] = 1
            settings_kwargs["marker_line_stop"] = 2
            if tttr_data is not None:
                header = tttr_data.header
                settings_kwargs["marker_line_start"] = int(header.tag('ImgHdr_LineStart')["value"])
                settings_kwargs["marker_line_stop"] = int(header.tag('ImgHdr_LineStop')["value"])
        clsm_settings = CLSMSettings(**settings_kwargs)
    else:
        clsm_settings = source.get_settings()
    kwargs['settings'] = clsm_settings
    this = _tttrlib.new_CLSMImage(**kwargs)
    try:
        self.this.append(this)
    except:
        self.this = this

@staticmethod
def compute_frc(image_1, image_2, bin_width = 2.0):
    # type: (np.ndarray, np.ndarray, int) -> (np.ndarray, np.ndarray)
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


def get_frc(
        self,                 # type: tttrlib.CLSMImage,
        other = None,         # type: tttrlib.CLSMImage
        bin_width = 2.0,      # type: int
        attribute="intensity" # type: str
):
    img1 = getattr(self, attribute)
    if other is None:
        im1 = img1[::2].sum(axis=0)
        im2 = img1[1::2].sum(axis=0)
    else:
        img2 = getattr(other, attribute)
        im1 = img1.sum(axis=0)
        im2 = img2.sum(axis=0)
    return CLSMImage.compute_frc(im1, im2, bin_width)
