import numpy as np

@property
def intensity(self):
    """
    Get the intensity array of the CLSM image.
    
    Returns:
        numpy.ndarray: Intensity array with shape:
            - If split_by_channel is False or n_channels == 1: (n_frames, n_lines, n_pixel)
            - If split_by_channel is True with multiple channels: (n_channels, frames_per_channel, n_lines, n_pixel)
    """
    # Get the raw intensity array from C++ (shape: n_frames, n_lines, n_pixel)
    raw_intensity = self.get_intensity()

    n_ch = int(self.n_channels)
    if n_ch > 1:
        # Multi-channel mode: reshape to (n_channels, frames_per_channel, n_lines, n_pixel)
        # Build the reshaped array by stacking each channel's frames
        channel_arrays = []
        for ch in range(n_ch):
            # Calculate offset and count for this channel
            offset = sum(int(self.get_channel_frame_count(i)) for i in range(ch))
            count = int(self.get_channel_frame_count(ch))
            # Extract frames for this channel
            channel_data = raw_intensity[offset:offset+count]
            channel_arrays.append(channel_data)

        # Stack along new first dimension (channels)
        return np.array(channel_arrays)
    else:
        # Single-channel mode: return as-is (n_frames, n_lines, n_pixel)
        return raw_intensity

@property
def shape(self):
    """
    Return the shape of the CLSM image.
    
    - If split_by_channel is False or n_channels == 1: (n_frames, n_lines, n_pixel)
    - If split_by_channel is True with multiple channels: (n_channels, frames_per_channel, n_lines, n_pixel)
    """
    n_ch = int(self.n_channels)
    if n_ch > 1:
        # Multi-channel mode: return (n_channels, frames_per_channel, n_lines, n_pixel)
        frames_per_channel = int(self.get_channel_frame_count(0))
        return n_ch, frames_per_channel, self.n_lines, self.n_pixel
    else:
        # Single-channel mode: return (n_frames, n_lines, n_pixel)
        return self.n_frames, self.n_lines, self.n_pixel

def __repr__(self):
    """String representation showing the shape of the CLSM image."""
    n_ch = int(self.n_channels)
    if n_ch > 1:
        frames_per_channel = int(self.get_channel_frame_count(0))
        return 'tttrlib.CLSMImage(%s, %s, %s, %s)' % (
            n_ch,
            frames_per_channel,
            self.n_lines,
            self.n_pixel
        )
    else:
        return 'tttrlib.CLSMImage(%s, %s, %s)' % (
            self.n_frames,
            self.n_lines,
            self.n_pixel
        )

class _ChannelSlice(object):
    """Slice of a CLSMImage for a specific channel.
    
    Provides access to frames and properties (like intensity) for a single channel.
    """

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

    @property
    def intensity(self):
        """Get intensity array for this channel only."""
        full_intensity = self._img.get_intensity()
        # Calculate offset by summing frame counts of previous channels
        offset = sum(int(self._img.get_channel_frame_count(i)) for i in range(self._ch))
        count = int(self._img.get_channel_frame_count(self._ch))
        return full_intensity[offset:offset+count]

    @property
    def shape(self):
        """Shape of this channel: (n_frames, n_lines, n_pixel)."""
        return (len(self), self._img.n_lines, self._img.n_pixel)

    def __repr__(self):
        return (
            f'_ChannelSlice(channel={self._ch}, frames={len(self)}, shape={self.shape})'
        )

# Override __getitem__ to support CLSMImage[ch][frame][line][pixel] for multi-channel images
# Falls back to old behavior (frames-first) if only a single channel exists
def __getitem__(self, idx):
    if isinstance(idx, int) and int(self.n_channels) > 1:
        return self._ChannelSlice(self, idx)
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
    """
    Read CLSM settings from TTTR header metadata.

    Handles both PTU and HT3 file formats with robust error handling
    for missing or malformed metadata.

    FIXED VERSION - uses 2^idx for markers
    """
    settings = dict()
    if tttr_data is not None:
        header = tttr_data.header

        # Read PTU‐style tags
        if tttr_data.get_tttr_container_type() == 'PTU':
            try:
                line_start_tag = header.tag('ImgHdr_LineStart')
                line_stop_tag = header.tag('ImgHdr_LineStop')
                line_start_val = line_start_tag.get("value", None)
                line_stop_val = line_stop_tag.get("value", None)
                if line_start_val is None or line_stop_val is None:
                    return settings
                line_start_raw = int(line_start_val)
                line_stop_raw = int(line_stop_val)
            except (KeyError, ValueError, TypeError):
                # Missing line markers - not a CLSM image
                return settings

            if line_start_raw == 0:
                # Unusual file: ImgHdr_LineStart=0 means use 2^index encoding
                try:
                    frame_tag = header.tag('ImgHdr_Frame')
                    frame_val = frame_tag.get("value", None)
                    frame_raw = int(frame_val) if frame_val is not None else None
                except (KeyError, ValueError, TypeError):
                    frame_raw = None

                marker_frame = []
                if frame_raw is not None and frame_raw >= 0:
                    marker_frame = [1 << frame_raw]
                if not marker_frame:
                    marker_frame = [4]

                try:
                    pix_x = int(header.tag('ImgHdr_PixX').get("value", 0))
                    pix_y = int(header.tag('ImgHdr_PixY').get("value", 0))
                except (KeyError, ValueError, TypeError):
                    pix_x = 0
                    pix_y = 0

                settings.update({
                    "marker_line_start": 1 << line_start_raw,      # 2^0 = 1
                    "marker_line_stop":  1 << line_stop_raw,
                    "marker_frame_start": marker_frame,
                    "n_pixel_per_line":  pix_x,
                    "n_lines":           pix_y,
                    "marker_event_type": 1
                })
            else:
                try:
                    pix_x = int(header.tag('ImgHdr_PixX').get("value", 0))
                    pix_y = int(header.tag('ImgHdr_PixY').get("value", 0))
                except (KeyError, ValueError, TypeError):
                    pix_x = 0
                    pix_y = 0

                try:
                    frame_tag = header.tag('ImgHdr_Frame')
                    frame_val = frame_tag.get("value", None)
                    frame_raw = int(frame_val) if frame_val is not None else None
                except (KeyError, ValueError, TypeError):
                    frame_raw = None

                marker_frame = []
                if frame_raw is not None and frame_raw > 0:
                    marker_frame = [1 << (frame_raw - 1)]

                settings.update({
                    "marker_line_start": 1 << (line_start_raw - 1),      # 2^0 = 1
                    "marker_line_stop":  1 << (line_stop_raw - 1),
                    "marker_frame_start": marker_frame,
                    "n_pixel_per_line":  pix_x,
                    "n_lines":           pix_y,
                    "marker_event_type": 1
                })

        # Read HT3‐style tags
        elif tttr_data.get_tttr_container_type() == 'HT3':
            try:
                settings.update(
                    {
                        "marker_line_start": int(
                            header.tag('ImgHdr_LineStart').get("value", 0)
                        ),
                        "marker_line_stop": int(
                            header.tag('ImgHdr_LineStop').get("value", 0)
                        ),
                        "n_pixel_per_line": int(
                            header.tag('ImgHdr_PixX').get("value", 0)
                        ),
                        "n_lines": int(header.tag('ImgHdr_PixY').get("value", 0)),
                        "marker_frame_start": [
                            int(header.tag('ImgHdr_Frame').get("value", 0))
                        ],
                        "marker_event_type": 1,
                    }
                )
            except (KeyError, ValueError, TypeError):
                pass

        # --- NEW: read bidirectional‐scan flag ---
        try:
            bd_tag = header.tag('ImgHdr_BiDirect')
            bd = int(bd_tag.get("value", 0))
            settings["bidirectional_scan"] = (bd != 0)
        except:
            settings["bidirectional_scan"] = False

    return settings

@staticmethod
def get_metadata(tttr_data):
    """
    Extract all image-related metadata from TTTR header.
    
    Returns a dictionary with all ImgHdr_* tags and other imaging metadata.
    Handles missing tags gracefully by returning None for unavailable values.
    """
    metadata = {}
    if tttr_data is None:
        return metadata
    
    header = tttr_data.header
    
    # List of common image metadata tags
    img_tags = [
        'ImgHdr_Frame', 'ImgHdr_LineStart', 'ImgHdr_LineStop',
        'ImgHdr_PixX', 'ImgHdr_PixY', 'ImgHdr_TimePerPixel',
        'ImgHdr_PixResol', 'ImgHdr_X0', 'ImgHdr_Y0', 'ImgHdr_Z0',
        'ImgHdr_BiDirect', 'ImgHdr_Dimensions', 'ImgHdr_Ident'
    ]

    for tag_name in img_tags:
        try:
            tag_value = header.tag(tag_name)["value"]
            metadata[tag_name] = tag_value
        except:
            metadata[tag_name] = None
    
    # Add acquisition metadata
    acq_tags = [
        'MeasDesc_AcquisitionTime', 'TTResult_NumberOfRecords',
        'TTResult_SyncRate', 'MeasDesc_GlobalResolution',
        'MeasDesc_Resolution', 'MeasDesc_NumberMicrotimes'
    ]

    for tag_name in acq_tags:
        try:
            tag_value = header.tag(tag_name)["value"]
            metadata[tag_name] = tag_value
        except:
            metadata[tag_name] = None
    
    return metadata

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
        use_pixel_markers=False,
        marker_pixel=1,
        filename=None,
        channel_luts=None,
        channel_shifts=None,
        settings_file=None,
        settings=None,
        **kwargs
):
    """
    Initialize a CLSMImage object.
    
    Parameters:
    - tttr_data: TTTR object or path to TTTR file
    - marker_frame_start, marker_line_start, etc.: CLSM marker parameters
    - filename: Alternative way to specify TTTR file path
    - channel_luts, channel_shifts: TTTR correction parameters
    - settings_file: Path to JSON file with TTTR corrections and CLSM settings
    - settings: Dict with TTTR corrections and CLSM settings
    """
    import pathlib
    import json
    import tttrlib

    # Handle JSON settings first (they can override other parameters)
    json_settings = {}
    if settings_file is not None or settings is not None:
        if settings_file is not None:
            # Load settings from file
            settings_path = pathlib.Path(settings_file)
            if not settings_path.exists():
                raise FileNotFoundError(f"Settings file not found: {settings_file}")
            with open(settings_path, 'r') as f:
                json_settings = json.load(f)
        else:
            json_settings = settings
        
        # Extract TTTR corrections
        if 'channel_luts' in json_settings:
            if channel_luts is None:
                channel_luts = {}
            for ch_str, lut_list in json_settings['channel_luts'].items():
                ch = int(ch_str)
                channel_luts[ch] = lut_list
        if 'channel_shifts' in json_settings:
            if channel_shifts is None:
                channel_shifts = {}
            for ch_str, shift_val in json_settings['channel_shifts'].items():
                ch = int(ch_str)
                channel_shifts[ch] = shift_val
    
    import pathlib
    import tttrlib
    
    # Handle first argument: can be TTTR object, string, or Path
    # - If TTTR object: use as-is
    # - If string or Path: create TTTR object from filename
    # - If None: check filename parameter
    if tttr_data is not None:
        if isinstance(tttr_data, (str, pathlib.Path)):
            # Convert filename to TTTR object
            if channel_luts is not None or channel_shifts is not None:
                tttr_data = tttrlib.TTTR(
                    tttr_data, channel_luts=channel_luts, channel_shifts=channel_shifts
                )
            else:
                tttr_data = tttrlib.TTTR(tttr_data)
        # else: assume it's already a TTTR object, use as-is
    elif filename is not None:
        # filename parameter provided as keyword argument
        if isinstance(filename, (str, pathlib.Path)):
            if channel_luts is not None or channel_shifts is not None:
                tttr_data = tttrlib.TTTR(
                    filename, channel_luts=channel_luts, channel_shifts=channel_shifts
                )
            else:
                tttr_data = tttrlib.TTTR(filename)
        else:
            raise TypeError("filename must be a string or Path object")
    
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
        "use_pixel_markers":             bool(use_pixel_markers),
        "marker_pixel":                  int(marker_pixel),
    }
    
    # Override CLSM settings from JSON if provided
    if json_settings:
        # CLSM-specific settings that can be overridden from JSON
        clsm_keys = [
            'marker_line_start', 'marker_line_stop', 'marker_frame_start', 
            'marker_event_type', 'n_pixel_per_line', 'n_lines',
            'skip_before_first_frame_marker', 'skip_after_last_frame_marker',
            'bidirectional_scan', 'split_by_channel', 'reading_routine',
            'use_pixel_markers', 'marker_pixel'
        ]
        for key in clsm_keys:
            if key in json_settings:
                if key == 'reading_routine':
                    settings_kwargs[key] = rt.get(json_settings[key], rt['default'])
                elif key == 'marker_frame_start':
                    if isinstance(json_settings[key], list):
                        settings_kwargs[key] = json_settings[key]
                    else:
                        settings_kwargs[key] = [json_settings[key]]
                else:
                    settings_kwargs[key] = json_settings[key]

    if not isinstance(source, CLSMImage):
        # If the user provided TTTR data, try reading any header‐derived settings
        if tttr_data is not None:
            header = tttr_data.header
            self.header = header
            try:
                auto_settings = self.read_clsm_settings(tttr_data)
                # Merge in any auto‐read values (including bidirectional_scan, if present)
                # Only update if auto_settings is not empty (i.e., valid CLSM settings were found)
                if auto_settings:
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
        if isinstance(use_pixel_markers, bool):
            settings_kwargs['use_pixel_markers'] = use_pixel_markers
        if isinstance(marker_pixel, int):
            settings_kwargs['marker_pixel'] = marker_pixel

        # Consume our special parameters before passing to C++ constructor
        kwargs.pop('settings_file', None)
        kwargs.pop('settings', None)

        kwargs['tttr_data'] = tttr_data

        # Pre‐set routines override everything else
        if reading_routine == 'SP5':
            settings_kwargs['marker_event_type'] = 1
            settings_kwargs['marker_frame_start'] = [4, 6]
            settings_kwargs['marker_line_start'] = 1
            settings_kwargs['marker_line_stop'] = 2
        elif reading_routine == 'SP8':
            settings_kwargs['marker_event_type'] = 15
            settings_kwargs['marker_frame_start'] = [4, 6]
            settings_kwargs['marker_line_start'] = 1
            settings_kwargs['marker_line_stop'] = 2
            if tttr_data is not None:
                header = tttr_data.header
                settings_kwargs['marker_line_start'] = int(
                    header.tag('ImgHdr_LineStart')["value"]
                )
                settings_kwargs['marker_line_stop'] = int(
                    header.tag('ImgHdr_LineStop')["value"]
                )
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
    
    # Store TTTR object as an attribute for easy access
    self.tttr_data = tttr_data
    
    # Store metadata for easy access
    self.metadata = self.get_metadata(tttr_data) if tttr_data is not None else {}

def get_image_info(self):
    """
    Get formatted image information from metadata.
    
    Returns a dictionary with human-readable image parameters:
    - dimensions: (width, height) in pixels
    - pixel_size: time per pixel in seconds
    - bidirectional: whether bidirectional scanning was used
    - frame_marker: frame start marker value
    - line_markers: (start, stop) line marker values
    - num_records: total number of photon records
    - sync_rate: sync rate in Hz
    - acquisition_time: acquisition time in ms
    """
    info = {}
    
    if not self.metadata:
        return info

    # Image dimensions
    width = self.metadata.get('ImgHdr_PixX')
    height = self.metadata.get('ImgHdr_PixY')
    if width is not None and height is not None:
        info['dimensions'] = (int(width), int(height))
    
    # Pixel timing
    time_per_pixel = self.metadata.get('ImgHdr_TimePerPixel')
    if time_per_pixel is not None:
        info['pixel_time_ms'] = float(time_per_pixel)
    
    # Bidirectional scanning
    bidirect = self.metadata.get('ImgHdr_BiDirect')
    if bidirect is not None:
        info['bidirectional'] = bool(bidirect)
    
    # Markers
    frame_marker = self.metadata.get('ImgHdr_Frame')
    if frame_marker is not None:
        info['frame_marker'] = int(frame_marker)
    
    line_start = self.metadata.get('ImgHdr_LineStart')
    line_stop = self.metadata.get('ImgHdr_LineStop')
    if line_start is not None and line_stop is not None:
        info['line_markers'] = (int(line_start), int(line_stop))
    
    # Acquisition parameters
    num_records = self.metadata.get('TTResult_NumberOfRecords')
    if num_records is not None:
        info['num_photons'] = int(num_records)
    
    sync_rate = self.metadata.get('TTResult_SyncRate')
    if sync_rate is not None:
        info['sync_rate_hz'] = int(sync_rate)
    
    acq_time = self.metadata.get('MeasDesc_AcquisitionTime')
    if acq_time is not None:
        info['acquisition_time_ms'] = int(acq_time)
    
    # Resolution
    global_res = self.metadata.get('MeasDesc_GlobalResolution')
    if global_res is not None:
        info['global_resolution_s'] = float(global_res)
    
    return info

def __repr_with_metadata__(self):
    """Extended representation including metadata."""
    info = self.get_image_info()
    dims = info.get('dimensions', (self.n_pixel, self.n_lines))
    repr_str = f"tttrlib.CLSMImage({self.n_frames}, {self.n_lines}, {self.n_pixel})"

    if info:
        repr_str += f"\n  Dimensions: {dims[0]}x{dims[1]} pixels"
        if 'pixel_time_ms' in info:
            repr_str += f"\n  Pixel time: {info['pixel_time_ms']} ms"
        if 'bidirectional' in info:
            repr_str += f"\n  Bidirectional: {info['bidirectional']}"
        if 'num_photons' in info:
            repr_str += f"\n  Photons: {info['num_photons']}"

    return repr_str

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
