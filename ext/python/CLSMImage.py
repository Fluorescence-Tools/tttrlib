import numpy as np

@property
def shape(self):
    return self.n_frames, self.n_lines, self.n_pixel

def __repr__(self):
    return 'tttrlib.CLSMImage(%s, %s, %s)' % (
        self.n_frames,
        self.n_lines,
        self.n_pixel
    )

class _ChannelSlice(object):
    def __init__(self, parent, ch):
        self._img = parent
        # normalize negative channel index
        n_ch = int(parent.n_channels)
        if ch < 0:
            ch = n_ch + ch
        if ch < 0 or ch >= n_ch:
            raise IndexError("channel index out of range")
        self._ch = int(ch)

    def __len__(self):
        return int(self._img.get_channel_frame_count(self._ch))

    def __getitem__(self, i):
        n = len(self)
        if i < 0:
            i = n + i
        if i < 0 or i >= n:
            raise IndexError("frame index out of range in channel slice")
        return self._img.get_frame_for_channel(self._ch, int(i))

# Override __getitem__ to support CLSMImage[ch][frame][line][pixel] for multi-channel images
# Falls back to old behavior (frames-first) if only a single channel exists
def __getitem__(self, idx):
    if isinstance(idx, int) and int(self.n_channels) > 1:
        return _ChannelSlice(self, idx)
    # Fallback to flat frame access
    return self.frame_at(int(idx))

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

@staticmethod
def read_clsm_settings(tttr_data):
    settings = dict()
    if tttr_data is not None:
        header = tttr_data.header

        # Read PTU‐style tags
        if tttr_data.get_tttr_container_type() == 'PTU':
            try:
                settings["marker_frame_start"] = [2 ** (int(header.tag('ImgHdr_Frame')["value"]) - 1)]
            except:
                settings["marker_frame_start"] = []

            settings.update({
                "marker_line_start": 2 ** (int(header.tag('ImgHdr_LineStart')["value"]) - 1),
                "marker_line_stop":  2 ** (int(header.tag('ImgHdr_LineStop')["value"]) - 1),
                "n_pixel_per_line":  int(header.tag('ImgHdr_PixX')["value"]),
                "n_lines":           int(header.tag('ImgHdr_PixY')["value"]),
                "marker_event_type": 1
            })

            try:
                bd = int(header.tag('ImgHdr_BiDirect')["value"])
                settings["bidirectional_scan"] = (bd != 0)
            except:
                settings["bidirectional_scan"] = False

        # Read HT3‐style tags
        elif tttr_data.get_tttr_container_type() == 'HT3':
            settings.update({
                "marker_line_start":   int(header.tag('ImgHdr_LineStart')["value"]),
                "marker_line_stop":    int(header.tag('ImgHdr_LineStop')["value"]),
                "n_pixel_per_line":    int(header.tag('ImgHdr_PixX')["value"]),
                "n_lines":             int(header.tag('ImgHdr_PixY')["value"]),
                "marker_frame_start":  [int(header.tag('ImgHdr_Frame')["value"])],
                "marker_event_type":   1
            })

            # --- NEW: read bidirectional‐scan flag ---
            try:
                bd = int(header.tag('ImgHdr_BiDirect')["value"])
                settings["bidirectional_scan"] = (bd != 0)
            except:
                settings["bidirectional_scan"] = False

    return settings

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
        split_by_channel=False,
        **kwargs
):
    source = kwargs.get('source', None)
    rt = {
        'SP8': CLSM_SP8,
        'SP5': CLSM_SP5,
        'default': CLSM_DEFAULT
    }

    # Always include bidirectional_scan=False by default
    settings_kwargs = {
        "skip_before_first_frame_marker": skip_before_first_frame_marker,
        "skip_after_last_frame_marker":  skip_after_last_frame_marker,
        "reading_routine":               rt[reading_routine],
        "marker_line_start":             marker_line_start,
        "marker_line_stop":              marker_line_stop,
        "marker_frame_start":            marker_frame_start,
        "marker_event_type":             marker_event_type,
        "n_pixel_per_line":              n_pixel_per_line,
        "bidirectional_scan":            False,   # <-- new default
        "split_by_channel":              bool(split_by_channel),
    }

    if not isinstance(source, CLSMImage):
        # If the user provided TTTR data, try reading any header‐derived settings
        if tttr_data is not None:
            header = tttr_data.header
            self.header = header
            try:
                auto_settings = self.read_clsm_settings(tttr_data)
                # Merge in any auto‐read values (including bidirectional_scan, if present)
                settings_kwargs.update(auto_settings)
            except:
                print("Error reading TTTR CLSM header")

        # Override with user‐provided arguments (if not None/type‐correct)
        if isinstance(marker_frame_start, int):
            settings_kwargs['marker_frame_start'] = [marker_frame_start]
        elif isinstance(marker_frame_start, list):
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

        # Pre‐set routines override everything else
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
                settings_kwargs["marker_line_stop"]  = int(header.tag('ImgHdr_LineStop')["value"])
                # If ImgHdr_BiDirect exists, preserve it:
                try:
                    bd = int(header.tag('ImgHdr_BiDirect')["value"])
                    settings_kwargs["bidirectional_scan"] = (bd != 0)
                except:
                    pass

        clsm_settings = CLSMSettings(**settings_kwargs)

    else:
        # Copy settings from the source CLSMImage
        clsm_settings = source.get_settings()

    kwargs['settings'] = clsm_settings
    this = _tttrlib.new_CLSMImage(**kwargs)

    try:
        self.this.append(this)
    except:
        self.this = this

@staticmethod
def compute_frc(
        image_1,
        image_2,
        bin_width=2.0,
        apply_hann=True,
        eps=1e-12,
):
    """Compute the Fourier Ring Correlation (FRC) between two 2-D images."""

    image_1 = np.asarray(image_1, dtype=np.float64)
    image_2 = np.asarray(image_2, dtype=np.float64)

    if image_1.shape != image_2.shape:
        raise ValueError("Images passed to compute_frc must share the same shape.")

    total_1 = float(np.sum(image_1))
    total_2 = float(np.sum(image_2))
    if total_1 <= 0 or total_2 <= 0:
        raise ValueError("Images must have a positive sum for FRC computation.")

    image_1 = image_1 / total_1
    image_2 = image_2 / total_2

    if apply_hann and min(image_1.shape) > 1:
        window_x = np.hanning(image_1.shape[0])
        window_y = np.hanning(image_1.shape[1])
        window = np.outer(window_x, window_y)
        image_1 = image_1 * window
        image_2 = image_2 * window

    fft1 = np.fft.fftshift(np.fft.fft2(image_1))
    fft2 = np.fft.fftshift(np.fft.fft2(image_2))

    cross_power = np.real(fft1 * np.conj(fft2))
    auto_1 = np.abs(fft1) ** 2
    auto_2 = np.abs(fft2) ** 2

    ny, nx = image_1.shape
    if bin_width <= 0:
        raise ValueError("bin_width must be positive.")

    yy, xx = np.indices((ny, nx))
    cy = (ny - 1) / 2.0
    cx = (nx - 1) / 2.0
    radius = np.sqrt((yy - cy) ** 2 + (xx - cx) ** 2)

    bin_index = np.floor(radius / bin_width).astype(np.int32)
    max_bin = int(bin_index.max())
    bin_edges = np.arange(max_bin + 2, dtype=np.float64) * float(bin_width)

    flat_index = bin_index.ravel()
    cross_sum = np.bincount(flat_index, weights=cross_power.ravel(), minlength=max_bin + 1)
    auto1_sum = np.bincount(flat_index, weights=auto_1.ravel(), minlength=max_bin + 1)
    auto2_sum = np.bincount(flat_index, weights=auto_2.ravel(), minlength=max_bin + 1)

    denom = np.sqrt(auto1_sum * auto2_sum)
    valid = denom > eps

    density = np.zeros_like(cross_sum, dtype=np.float64)
    density[valid] = cross_sum[valid] / (denom[valid] + eps)
    density = np.clip(density, -1.0, 1.0)

    return density, bin_edges


def get_frc(
        self,
        other=None,
        bin_width=2.0,
        attribute="intensity",
        apply_hann=True,
):
    img1 = getattr(self, attribute)
    if other is None:
        im1 = img1[::2].sum(axis=0)
        im2 = img1[1::2].sum(axis=0)
    else:
        img2 = getattr(other, attribute)
        im1 = img1.sum(axis=0)
        im2 = img2.sum(axis=0)
    return CLSMImage.compute_frc(im1, im2, bin_width=bin_width, apply_hann=apply_hann)
