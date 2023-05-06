
// File: index.xml

// File: classbfgs.xml


%feature("docstring") bfgs "

C++ includes: i_lbfgs.h
";

%feature("docstring") bfgs::bfgs "
bfgs::bfgs";

%feature("docstring") bfgs::bfgs "
bfgs::bfgs";

%feature("docstring") bfgs::~bfgs "
bfgs::~bfgs";

%feature("docstring") bfgs::setN "
bfgs::setN";

%feature("docstring") bfgs::seteps "
bfgs::seteps";

%feature("docstring") bfgs::seteps "
bfgs::seteps";

%feature("docstring") bfgs::fix "
bfgs::fix";

%feature("docstring") bfgs::free "
bfgs::free";

%feature("docstring") bfgs::minimize "
bfgs::minimize";

// File: unionbh__overflow.xml

// File: unionbh__spc130__record.xml

// File: unionbh__spc132__header.xml

// File: unionbh__spc600__256__record.xml

// File: unionbh__spc600__4096__record.xml

// File: class_c_l_s_m_frame.xml


%feature("docstring") CLSMFrame "

C++ includes: CLSMFrame.h
";

%feature("docstring") CLSMFrame::get_lines "
CLSMFrame::get_lines";

%feature("docstring") CLSMFrame::size "
CLSMFrame::size
Get the number of lines in the CLSMFrame.  
";

%feature("docstring") CLSMFrame::CLSMFrame "
CLSMFrame::CLSMFrame";

%feature("docstring") CLSMFrame::CLSMFrame "
CLSMFrame::CLSMFrame
Copy constructor  

Parameters
----------
* `fill` :  
    if set to false the content of the pixels is not copied  
";

%feature("docstring") CLSMFrame::CLSMFrame "
CLSMFrame::CLSMFrame";

%feature("docstring") CLSMFrame::~CLSMFrame "
CLSMFrame::~CLSMFrame";

%feature("docstring") CLSMFrame::append "
CLSMFrame::append
Append a line to the current frame  

Parameters
----------
* `line` :  
";

%feature("docstring") CLSMFrame::crop "
CLSMFrame::crop
Crops a frame  
";

// File: class_c_l_s_m_image.xml


%feature("docstring") CLSMImage "

C++ includes: CLSMImage.h
";

%feature("docstring") CLSMImage::get_tttr "
CLSMImage::get_tttr";

%feature("docstring") CLSMImage::set_tttr "
CLSMImage::set_tttr";

%feature("docstring") CLSMImage::get_settings "
CLSMImage::get_settings";

%feature("docstring") CLSMImage::size "
CLSMImage::size
Get the number of frames in the CLSMImage.  
";

%feature("docstring") CLSMImage::fill "
CLSMImage::fill
Fill the tttr_indices of the pixels with the indices of the channels that are
within a pixel  

Parameters
----------
* `channels[in]` :  
    list of routing channels. Events that have routing channels in this vector
    are added to pixels of corresponding time.  
* `clear_pixel[in]` :  
    if set to true (default) the pixels are cleared before they are filled. If
    set to false new tttr indices are added to the pixels  
";

%feature("docstring") CLSMImage::fill_pixels "
CLSMImage::fill_pixels";

%feature("docstring") CLSMImage::clear "
CLSMImage::clear
Clear tttr_indices stored in the pixels  
";

%feature("docstring") CLSMImage::clear_pixels "
CLSMImage::clear_pixels";

%feature("docstring") CLSMImage::strip "
CLSMImage::strip
Strips tttr_indices from all pixels in Image assumes that each tttr index is
only once in an image  
";

%feature("docstring") CLSMImage::get_fcs_image "
CLSMImage::get_fcs_image
Computes the an image where pixels are correlation curves  

Parameters
----------
* `output[out]` :  
* `dim1[out]` :  
* `dim2[out]` :  
* `dim3[out]` :  
* `dim4[out]` :  
* `tttr_self` :  
* `tac_coarsening` :  
* `stack_frames` :  
";

%feature("docstring") CLSMImage::get_frames "
CLSMImage::get_frames
Get the frames in the CLSMImage.  
";

%feature("docstring") CLSMImage::get_intensity "
CLSMImage::get_intensity
Intensity image.  
";

%feature("docstring") CLSMImage::get_fluorescence_decay "
CLSMImage::get_fluorescence_decay
Computes an image stack where the value of each pixel corresponds to a histogram
of micro times in each pixel. The micro times can be coarsened by integer
numbers.  

Parameters
----------
* `tttr_data` :  
    pointer to a TTTR object  
* `out` :  
    pointer to output array of unsigned chars that will contain the image stack  
* `dim1` :  
    number of frames  
* `dim2` :  
    number of lines  
* `dim3` :  
    number of pixels  
* `dim4` :  
    number of micro time channels in the histogram  
* `micro_time_coarsening` :  
    constant used to coarsen the micro times. The default value is 1 and the
    micro times are binned without coarsening.  
* `stack_frames` :  
    if True the frames are stacked.  
";

%feature("docstring") CLSMImage::get_decay_of_pixels "
CLSMImage::get_decay_of_pixels
Computes micro time histograms for the stacks of images and a selection of
pixels. Photons in pixels that are selected by the selection array contribute to
the returned array of micro time histograms.  

Parameters
----------
* `tttr_data` :  
    pointer to a TTTR object  
* `mask` :  
    a stack of images used as a mask to select pixels  
* `dmask1` :  
    number of frames  
* `dmask2` :  
    number of lines  
* `dmask3` :  
    number of pixels per line  
* `out` :  
    pointer to output array of unsigned int contains the micro time histograms  
* `dim1` :  
    dimension of the output array, i.e., the number of stacks  
* `dim1` :  
    dimension the number of micro time channels  
* `tac_coarsening` :  
    constant used to coarsen the micro times  
* `stack_frames` :  
    if True the frames are stacked.  
";

%feature("docstring") CLSMImage::get_mean_micro_time "
CLSMImage::get_mean_micro_time
Calculates an image stack where the value of each pixel corresponds to the mean
micro time (in units of the micro channel resolution).  

Pixels with few photons can be discriminated. Discriminated pixels will be
filled with zeros.  

Parameters
----------
* `tttr_data[in]` :  
    pointer to a TTTR object  
* `out[out]` :  
    pointer to output array that will contain the image stack  
* `dim1[out]` :  
    returns the number of frames  
* `dim2[out]` :  
    returns the number of lines  
* `dim3[out]` :  
    returns the number of pixels per line  
* `minimum_number_of_photons[in]` :  
    the minimum number of photons in a micro time  
* `stack_frames[in]` :  
    if true the frames are stacked (default value is false). If stack frames is
    set to true the mean arrival time is computed using the tttr indices of all
    pixels (this corresponds to the photon weighted mean arrival time).  
";

%feature("docstring") CLSMImage::get_phasor "
CLSMImage::get_phasor
Computes the phasor values for every pixel  

Pixels with few photons can be discriminated. Discriminated pixels will be
filled with zeros.  

Parameters
----------
* `tttr_data[in]` :  
    pointer to a TTTR object  
* `out[out]` :  
    pointer to output array that will contain the image stack  
* `dim1[out]` :  
    returns the number of frames  
* `dim2[out]` :  
    returns the number of lines  
* `dim3[out]` :  
    returns the number of pixels per line  
* `dim4[out]` :  
    returns 2 (first is the g phasor value (cos), second the s phasor (sin)  
* `minimum_number_of_photons[in]` :  
    the minimum number of photons in a micro time (only used if frames are not
    stacked)  
* `stack_frames[in]` :  
    if true the frames are stacked (default value is false). If stack frames is
    set to true the mean arrival time is computed using the tttr indices of all
    pixels (this corresponds to the photon weighted mean arrival time).  
";

%feature("docstring") CLSMImage::get_mean_lifetime "
CLSMImage::get_mean_lifetime
Computes an image of average lifetimes  

The average lifetimes are computed (not fitted) by the methods of moments (Irvin
Isenberg, 1973, Biophysical journal). This approach does not consider scattered
light.  

Pixels with few photons can be discriminated. Discriminated pixels are filled
with zeros.  

By default the fluorescence lifetimes of the pixels are computed in units of
nanoseconds.  

Parameters
----------
* `tttr_data[in]` :  
    pointer to a TTTR object  
* `tttr_irf[in]` :  
    pointer to a TTTR object of the IRF  
* `out[out]` :  
    pointer to output array that will contain the image stack  
* `dim1[out]` :  
    returns the number of frames  
* `dim2[out]` :  
    returns the number of lines  
* `dim3[out]` :  
    returns the number of pixels per line  
* `minimum_number_of_photons[in]` :  
    the minimum number of photons in a micro time  
* `m0_irf` :  
    is the zero moment of the IRF (optional, default=1)  
* `m1_irf` :  
    is the first moment of the IRF (optional, default=1)  
";

%feature("docstring") CLSMImage::to1D "
CLSMImage::to1D
Convert frame, line, and pixel to 1D index.  
";

%feature("docstring") CLSMImage::to3D "
CLSMImage::to3D
Convert 1D index to frame, line, and pixel.  
";

%feature("docstring") CLSMImage::getPixel "
CLSMImage::getPixel";

%feature("docstring") CLSMImage::get_n_frames "
CLSMImage::get_n_frames
Get the number of frames in the CLSM image.  
";

%feature("docstring") CLSMImage::get_n_lines "
CLSMImage::get_n_lines
Get the number of lines per frame in the CLSMImage.  
";

%feature("docstring") CLSMImage::get_n_pixel "
CLSMImage::get_n_pixel
Get the number of pixels per line a frame of the CLSMImage.  
";

%feature("docstring") CLSMImage::copy "
CLSMImage::copy
Copy the information from another CLSMImage object  

Parameters
----------
* `p2` :  
    The information from this object is copied.  
* `fill` :  
    If this is set to true (default is false) the tttr indices of the pixels are
    copied.  

Returns
-------  
";

%feature("docstring") CLSMImage::append "
CLSMImage::append
Append a frame to the CLSM image.  

Parameters
----------
* `frame` :  
";

%feature("docstring") CLSMImage::transform "
CLSMImage::transform
Moves the content of the Pixels  

The input is an interleaved array or source and target pixel indices. A pixel
index is a mapping from a frames, lines, and pixel combination to an index.  

Parameters
----------
* `index` :  
* `n_index` :  
";

%feature("docstring") CLSMImage::rebin "
CLSMImage::rebin
Rebin a CLSMImage  

Note, rebinning redistributes photons and thus the macro times in pixels.  

Parameters
----------
* `bin_line` :  
    binning factor for lines  
* `bin_pixel` :  
    binning factor for pixel in lines  
";

%feature("docstring") CLSMImage::distribute "
CLSMImage::distribute
Distribute the photons of a pixel_id to a set of pixel ids in a target image
according to provided probabilities  
";

%feature("docstring") CLSMImage::crop "
CLSMImage::crop
Crop the image  

Parameters
----------
* `frame_start` :  
* `frame_stop` :  
* `line_start` :  
* `line_stop` :  
* `pixel_start` :  
* `pixel_stop` :  
";

%feature("docstring") CLSMImage::stack_frames "
CLSMImage::stack_frames";

%feature("docstring") CLSMImage::CLSMImage "
CLSMImage::CLSMImage
Copy constructor.  
";

%feature("docstring") CLSMImage::CLSMImage "
CLSMImage::CLSMImage
Parameters
----------
* `tttr_data` :  
    pointer to TTTR object  
* `marker_frame_start` :  
    routing channel numbers (default reading routine) or micro time channel
    number (SP8 reading routine) that serves as a marker informing on a new
    frame in the TTTR data stream.  
* `marker_line_start` :  
    routing channel number (default reading routine) or micro time channel
    number (SP8 reading routine) that serves as a marker informing on the start
    of a new line in a frame within the TTTR data stream  
* `marker_line_stop` :  
    routing channel number (default reading routine) or micro time channel
    number (SP8 reading routine) that serves as a marker informing on the stop
    of a new line in a frame within the TTTR data stream  
* `marker_event_type` :  
    event types that are interpreted as markers for frames and lines.  
* `n_pixel_per_line` :  
    number of pixels into which each line is separated. If the number of pixels
    per line is set to zero. The number of pixels per line will correspond to
    the number of lines in the first frame.  
* `reading_routine` :  
    an integer that specifies the reading routine used to read a CLSM image out
    of a TTTR data stream. A CLSM image can be encoded by several ways in a TTTR
    stream. Leica encodes frame and line markers in micro time channel numbers.
    PicoQuant and others use a more 'traditional' encoding for frame and line
    markers marking TTTR events as marker events and using the channel number to
    differentiate the different marker types.  
* `source` :  
    A CLSMImage object that is used as a template for the created object. All
    frames and lines are copied and empty pixels are created. If the parameter
    fill is set to true moreover the content of the pixels is copied.  
* `fill` :  
    if set to true (default) is false the lines are filled with pixels that will
    contain either the photons of the specified channels or the photons from the
    source CLSMImage instance.  
* `channels` :  
    The channel number of the events that will be used to fill the pixels.  
* `stack_frames` :  
    If set to true (default is false) the frames in the CLSM image are stacked
    and the resulting CLSMImage will hava a single frame.  
";

%feature("docstring") CLSMImage::~CLSMImage "
CLSMImage::~CLSMImage
Destructor.  
";

%feature("docstring") CLSMImage::get_line_duration "
CLSMImage::get_line_duration
Obtain line duration (in milliseconds)  

Parameters
----------
* `frame` :  
    number of frame in image  
* `line` :  
    number of line in image  

Returns
-------
duration of line in selected frame  
";

%feature("docstring") CLSMImage::get_pixel_duration "
CLSMImage::get_pixel_duration
Obtain pixel duration in milliseconds.  

Parameters
----------
* `frame` :  
    Selected frame number  
* `line` :  
    selected line number  

Returns
-------
duration of pixel in selected frame  
";

%feature("docstring") CLSMImage::compute_ics "
CLSMImage::compute_ics
Computes an image correlation via FFTs for a set of frames  

This function computes the image correlation for a set of frames. The frames can
be either specified by an array or by a CLSMImage object. This function can
compute image cross-correlation and image auto-correlations. The type of the
correlation is specified by a set of pairs that are cross- correlated.  

Parameters
----------
* `output` :  
    the array that will contain the ICS  
* `dim1` :  
    number of frames in the ICS  
* `dim2` :  
    number of lines (line shifts) in the ICS  
* `dim3` :  
    number of pixel (pixel shifts) in the ICS  
* `tttr_data` :  
* `clsm` :  
    an optional pointer to a CLSMImage object  
* `images` :  
    an optional pointer to an image array  
* `input_frames` :  
    number of frames in the image array  
* `input_lines` :  
    number of lines in the image array  
* `input_pixel` :  
    number of pixel in the image array  
* `x_range` :  
    defines the region of interest (ROI) in the image (pixel). This parameter is
    optional. The default value is [0,-1]. This means that the entire input
    pixel range is used  
* `y_range` :  
    region defines the ROI in y-direction (lines). The default value is [0,-1].
    By default all lines in the image are used.  
* `frames_index_pairs` :  
    A vector of integer pairs. The pairs correspond to the frame numbers in the
    input that will be cross-correlated. If no vector of frame pairs is
    specified the image auto-correlation will be computed  
* `subtract_average` :  
    the input image can be corrected for the background, i.e., a constant
    background can be subtracted from the frames. If this parameter is set to
    \"stack\" the average over all frames is computed and subtracted pixel-wise
    from each frame. If this parameter is set to \"frame\" the average of each
    frame is computed and subtracted from the each frame. By default no
    correction is applied.  
* `mask` :  
    a stack of images used as a to select pixels  
* `dmask1` :  
    number of frames  
* `dmask2` :  
    number of lines  
* `dmask3` :  
    number of pixels per line  
";

%feature("docstring") CLSMImage::get_roi "
CLSMImage::get_roi
Copies a region of interest (ROI) into a new image and does some background
correction.  

The ROI is defined by defining a range for the pixels and lines. The ROI can be
corrected by a constant background value, clipped to limit the range of the
output values, and corrected by the mean intensity of the frames.  

Parameters
----------
* `output` :  
    the array that will contain the ROI. The array is allocated by the function  
* `dim1` :  
    the number of frames in the output ROI  
* `dim2` :  
    the number of lines per frame in the output  
* `dim3` :  
    the number of pixels per line in the output ROI  
* `clsm` :  
    a pointer to a CLSMImage object  
* `x_range` :  
    the range (selection) of the pixels  
* `y_range` :  
    the range (selection) of the lines  
* `subtract_average` :  
    If this parameter is set to \"stack\" the mean image of the ROIs that is
    computed by the average over all frames is subtracted from each frame and
    the mean intensity of all frames and pixels is added to the pixels. If this
    parameter is set to \"frame\" the average of each frame is subtracted from
    each frame. The default behaviour is to do nothing.  
* `background` :  
    A constant number that is subtracted from each pixel.  
* `clip` :  
    If set to true (the default value is false) the values in the ROI are
    clipped to the range [clip_min, clip_max]  
* `clip_max` :  
    the maximum value when output ROIs are clipped  
* `clip_min` :  
    the minimum value when output ROIs are clipped  
* `images` :  
    Input array of images that are used to defined ROIs. If no CLSMImage object
    is specified. This array is used as an input.  
* `n_frames` :  
    The number of frames in the input array images  
* `n_lines` :  
    The number of lines in the input array images  
* `n_pixel` :  
    The number of pixel in the input array images  
* `selected_frames` :  
    A list of frames that is used to define the ROIs. If no frames are defined
    by this list, all frames in the input are used.  
* `mask` :  
    a stack of images used as a to select pixels  
* `dmask1` :  
    number of frames if the number of frames in the mask is smaller then the ROI
    the first mask frame will be applied to all ROI frames that are greater than
    dmask1  
* `dmask2` :  
    number of lines if smaller then ROI the outside region will be selected and
    the mask will be applied to all lines smaller than dmask2  
* `dmask3` :  
    number of pixels per line in the mask.  
";

%feature("docstring") CLSMImage::get_frame_edges "
CLSMImage::get_frame_edges
Get the tttr indices of frame markers for a SP8  

Parameters
----------
* `tttr` :  
    pointer to the TTTR object that is inspected  
* `marker_frame` :  
    vector of  
* `marker_event` :  
* `start_event` :  
* `stop_event` :  

Returns
-------  
";

%feature("docstring") CLSMImage::get_line_edges "
CLSMImage::get_line_edges
Read start stop marker to identify line edges.  
";

%feature("docstring") CLSMImage::get_line_edges_by_duration "
CLSMImage::get_line_edges_by_duration
Read start marker and use line duration as stop.  
";

// File: class_c_l_s_m_line.xml


%feature("docstring") CLSMLine "

C++ includes: CLSMLine.h
";

%feature("docstring") CLSMLine::size "
CLSMLine::size
Get the number of pixels per line a frame of the CLSMImage.  
";

%feature("docstring") CLSMLine::get_pixels "
CLSMLine::get_pixels";

%feature("docstring") CLSMLine::set_pixel_duration "
CLSMLine::set_pixel_duration";

%feature("docstring") CLSMLine::get_pixel_duration "
CLSMLine::get_pixel_duration";

%feature("docstring") CLSMLine::CLSMLine "
CLSMLine::CLSMLine";

%feature("docstring") CLSMLine::CLSMLine "
CLSMLine::CLSMLine";

%feature("docstring") CLSMLine::CLSMLine "
CLSMLine::CLSMLine";

%feature("docstring") CLSMLine::CLSMLine "
CLSMLine::CLSMLine";

%feature("docstring") CLSMLine::~CLSMLine "
CLSMLine::~CLSMLine";

%feature("docstring") CLSMLine::append "
CLSMLine::append";

%feature("docstring") CLSMLine::crop "
CLSMLine::crop";

// File: class_c_l_s_m_pixel.xml


%feature("docstring") CLSMPixel "

C++ includes: CLSMPixel.h
";

%feature("docstring") CLSMPixel::~CLSMPixel "
CLSMPixel::~CLSMPixel";

%feature("docstring") CLSMPixel::CLSMPixel "
CLSMPixel::CLSMPixel";

%feature("docstring") CLSMPixel::CLSMPixel "
CLSMPixel::CLSMPixel";

// File: class_c_l_s_m_settings.xml


%feature("docstring") CLSMSettings "

C++ includes: CLSMImage.h
";

%feature("docstring") CLSMSettings::CLSMSettings "
CLSMSettings::CLSMSettings
Parameters
----------
* `marker_frame_start` :  
    routing channel numbers (default reading routine) or micro time channel
    number (SP8 reading routine) that serves as a marker informing on a new
    frame in the TTTR data stream.  
* `marker_line_start` :  
    routing channel number (default reading routine) or micro time channel
    number (SP8 reading routine) that serves as a marker informing on the start
    of a new line in a frame within the TTTR data stream  
* `marker_line_stop` :  
    routing channel number (default reading routine) or micro time channel
    number (SP8 reading routine) that serves as a marker informing on the stop
    of a new line in a frame within the TTTR data stream  
* `marker_event` :  
    event types that are interpreted as markers for frames and lines.  
* `n_pixel_per_line` :  
    number of pixels into which each line is separated. If the number of pixels
    per line is set to zero. The number of pixels per line will correspond to
    the number of lines in the first frame.  
* `macro_time_shift` :  
    Number of macro time counts a line start is shifted relative to the line
    start marker in the TTTR object (default 0)  
* `reading_routine` :  
    an integer that specifies the reading routine used to read a CLSM image out
    of a TTTR data stream. A CLSM image can be encoded by several ways in a TTTR
    stream. Leica encodes frame and line markers in micro time channel numbers.
    PicoQuant and others use a more 'traditional' encoding for frame and line
    markers marking TTTR events as marker events and using the channel number to
    differentiate the different marker types.  
";

// File: struct_correlation_curve_settings.xml


%feature("docstring") CorrelationCurveSettings "

C++ includes: CorrelatorCurve.h
";

%feature("docstring") CorrelationCurveSettings::get_ncorr "
CorrelationCurveSettings::get_ncorr
The number of points in a correlation curve.  
";

// File: class_correlator.xml


%feature("docstring") Correlator "

C++ includes: Correlator.h
";

%feature("docstring") Correlator::dt "
Correlator::dt
Computes the the time difference in macro time units the first and the last
event.  
";

%feature("docstring") Correlator::Correlator "
Correlator::Correlator
Parameters
----------
* `tttr` :  
    optional TTTR object. If provided, the macro and micro time calibration of
    the TTTR object header calibrate the correlator.  
* `method` :  
    name of correlation method that is used by the correlator  
* `n_bins` :  
    number of equally spaced correlation bins per block (determines correlation
    bins)  
* `n_casc` :  
    number of blocks (determines correlation bins)  
* `make_fine` :  
    if true macro and micro time are combined.  
";

%feature("docstring") Correlator::~Correlator "
Correlator::~Correlator";

%feature("docstring") Correlator::set_n_casc "
Correlator::set_n_casc
Set correlation axis parameter and update axis.  

Parameters
----------
* `n_casc` :  
    number of cascades (also called blocks) of the correlation curve  
";

%feature("docstring") Correlator::get_curve "
Correlator::get_curve
get correlation  

computes correlation (if necessary) and returns correlation curve  

Returns
-------
correlation curve STOP STOP  
";

%feature("docstring") Correlator::get_n_casc "
Correlator::get_n_casc
Returns
-------
number of correlation blocks  
";

%feature("docstring") Correlator::set_n_bins "
Correlator::set_n_bins
Parameters
----------
* `v` :  
    number of equally spaced correlation channels per block  
";

%feature("docstring") Correlator::get_n_bins "
Correlator::get_n_bins
Returns
-------
the number of equally spaced correlation channels per block  
";

%feature("docstring") Correlator::set_correlation_method "
Correlator::set_correlation_method
Correlation method  

Parameters
----------
* `cm` :  
    the name of the method options: \"felekyan\", \"wahl\", or \"laurence\"  

Felekyan, S., Kühnemuth, R., Kudryavtsev, V., Sandhagen, C., Becker, W. and
Seidel, C.A.,  

1.  Full correlation from picoseconds to seconds by time-resolved and time-
    correlated single photon detection. Review of scientific instruments, 76(8),
    p.083104.  

Michael Wahl, Ingo Gregor, Matthias Patting, Joerg Enderlein, 2003, Fast
calculation of fluorescence correlation data with asynchronous time-correlated
single-photon counting, Opt Express Vol. 11, No. 26, p. 3383  

Ted A. Laurence, Samantha Fore, Thomas Huser, 2006. Fast, flexible algorithm for
calculating photon correlations, , Opt Lett. 15;31(6):829-31  
";

%feature("docstring") Correlator::get_correlation_method "
Correlator::get_correlation_method
Returns
-------
name of the used correlation method  
";

%feature("docstring") Correlator::set_microtimes "
Correlator::set_microtimes
Add microtime information to event stream.  

Parameters
----------
* `tac_1` :  
    The micro times of the first correlation channel  
* `n_tac_1` :  
    The number of events in the first correlation channel  
* `tac_2` :  
    The micro times of the second correlation channel  
* `n_tac_2` :  
    The number of events in the second correlation channel  
* `number_of_microtime_channels` :  
    The maximum number of TAC channels of the micro times.  
";

%feature("docstring") Correlator::set_macrotimes "
Correlator::set_macrotimes
Parameters
----------
* `t1` :  
    time events in the the first correlation channel  
* `n_t1` :  
    The number of time events in the first channel  
* `t1` :  
    time events in the the second correlation channel  
* `n_t2` :  
    The number of time events in the second channel  
";

%feature("docstring") Correlator::get_macrotimes "
Correlator::get_macrotimes
get event times of first and second correlation channel  

Returns
-------
event times of first and second correlation channel  
";

%feature("docstring") Correlator::set_events "
Correlator::set_events
Parameters
----------
* `time` :  
    events of the first correlation channel  
* `n_t1` :  
    The number of time events in the first channel  
* `w1` :  
    A vector of weights for the time events of the first channel  
* `n_weights_ch1` :  
    The number of weights of the first channel  
* `t2` :  
    A vector of the time events of the second channel  
* `n_t2` :  
    The number of time events in the second channel  
* `w2` :  
    A vector of weights for the time events of the second channel  
* `n_weights_ch2` :  
    The number of weights of the second channel  
";

%feature("docstring") Correlator::set_weights "
Correlator::set_weights
Set weights used for correlation.  

Set and update weights of the events in first and second correlation channel  

Parameters
----------
* `w1` :  
    A vector of weights for the time events of the first channel  
* `n_weights_ch1` :  
    The number of weights of the first channel  
* `w2` :  
    A vector of weights for the time events of the second channel  
* `n_weights_ch2` :  
    The number of weights of the second channel  
";

%feature("docstring") Correlator::get_weights "
Correlator::get_weights
Returns
-------
weights in first and second correlation channel  
";

%feature("docstring") Correlator::get_x_axis "
Correlator::get_x_axis
Get correlation bins (axis)  

Parameters
----------
* `output` :  
    x_axis / time axis of the correlation  
* `n_out` :  
    number of elements in the axis of the x-axis  
";

%feature("docstring") Correlator::get_corr_normalized "
Correlator::get_corr_normalized
Get the normalized correlation.  

Parameters
----------
* `output` :  
    an array that containing normalized correlation  
* `n_output` :  
    the number of elements of output  
";

%feature("docstring") Correlator::get_corr "
Correlator::get_corr
Get the (unnormalized) correlation.  

Parameters
----------
* `output` :  
    a pointer to an array that will contain the correlation  
* `n_output` :  
    a pointer to the an integer that will contain the number of elements of the
    x-axis  
";

%feature("docstring") Correlator::run "
Correlator::run
compute the correlation  

Compute the correlation function. Usually calling this method is not necessary
the the validity of the correlation function is tracked by the attribute
is_valid.  
";

%feature("docstring") Correlator::set_tttr "
Correlator::set_tttr
Sets the time and the weights using TTTR objects.  

Set the event times (and weights) using TTTR objects. By default the weights are
all set to one.  

The header of the first TTTR object is used for calibration. Both TTTR objects
should have the same calibration (this is not checked).  

Parameters
----------
* `tttr_1` :  
* `tttr_2` :  
* `make_fine` :  
    if true a full correlation is computed that uses the micro time in the TTTR
    objects (default is false).  
";

%feature("docstring") Correlator::get_tttr "
Correlator::get_tttr";

%feature("docstring") Correlator::set_filter "
Correlator::set_filter
Updates the weights. Non-zero weights are assigned a filter value that is
defined by a filter map and the micro time of the event.  

Parameters
----------
* `micro_times[in]` :  
* `routing_channels[in]` :  
* `filter[in]` :  
    map of filters the first element in the map is the routing channel number,
    the second element of the map is a vector that maps a micro time to a filter
    value.  
";

// File: class_correlator_curve.xml


%feature("docstring") CorrelatorCurve "

C++ includes: CorrelatorCurve.h
";

%feature("docstring") CorrelatorCurve::size "
CorrelatorCurve::size";

%feature("docstring") CorrelatorCurve::get_x_axis "
CorrelatorCurve::get_x_axis
Get the x-axis of the correlation.  

Parameters
----------
* `x_axis` :  
    a pointer to an array that will contain the x-axis  
* `n_out` :  
    a pointer to the an integer that will contain the number of elements of the
    x-axis  
";

%feature("docstring") CorrelatorCurve::set_x_axis "
CorrelatorCurve::set_x_axis
Set the x-axis to arbitray bin values.  

Attention: Make sure that the correlation method supports arbitray bin spacing  
";

%feature("docstring") CorrelatorCurve::set_n_bins "
CorrelatorCurve::set_n_bins
Parameters
----------
* `v` :  
    the number of equally spaced correaltion channels per block  
";

%feature("docstring") CorrelatorCurve::get_n_bins "
CorrelatorCurve::get_n_bins";

%feature("docstring") CorrelatorCurve::set_n_casc "
CorrelatorCurve::set_n_casc
Sets the number of cascades (also called blocks) of the correlation curve and
updates the correlation axis.  

Parameters
----------
* `n_casc` :  
";

%feature("docstring") CorrelatorCurve::get_n_casc "
CorrelatorCurve::get_n_casc
Returns
-------
number of correlation blocks  
";

%feature("docstring") CorrelatorCurve::get_corr "
CorrelatorCurve::get_corr";

%feature("docstring") CorrelatorCurve::get_corr_normalized "
CorrelatorCurve::get_corr_normalized";

// File: class_correlator_photon_stream.xml


%feature("docstring") CorrelatorPhotonStream "

CorrelatorPhotonStream gathers event times and weights.  

C++ includes: CorrelatorPhotonStream.h
";

%feature("docstring") CorrelatorPhotonStream::CorrelatorPhotonStream "
CorrelatorPhotonStream::CorrelatorPhotonStream";

%feature("docstring") CorrelatorPhotonStream::CorrelatorPhotonStream "
CorrelatorPhotonStream::CorrelatorPhotonStream";

%feature("docstring") CorrelatorPhotonStream::~CorrelatorPhotonStream "
CorrelatorPhotonStream::~CorrelatorPhotonStream";

%feature("docstring") CorrelatorPhotonStream::empty "
CorrelatorPhotonStream::empty";

%feature("docstring") CorrelatorPhotonStream::size "
CorrelatorPhotonStream::size";

%feature("docstring") CorrelatorPhotonStream::clear "
CorrelatorPhotonStream::clear";

%feature("docstring") CorrelatorPhotonStream::resize "
CorrelatorPhotonStream::resize
Parameters
----------
* `n` :  
* `x` :  
    initial value of the weights  
";

%feature("docstring") CorrelatorPhotonStream::make_fine "
CorrelatorPhotonStream::make_fine";

%feature("docstring") CorrelatorPhotonStream::set_weights "
CorrelatorPhotonStream::set_weights";

%feature("docstring") CorrelatorPhotonStream::set_events "
CorrelatorPhotonStream::set_events";

%feature("docstring") CorrelatorPhotonStream::coarsen "
CorrelatorPhotonStream::coarsen
Coarsens the time events  

This method coarsens the time events by dividing the times by two. In case two
consecutive time events in the array have the same time, the weights of the two
events are added to the following weight element and the value of the previous
weight is set to zero.  
";

%feature("docstring") CorrelatorPhotonStream::dt "
CorrelatorPhotonStream::dt";

%feature("docstring") CorrelatorPhotonStream::sum_of_weights "
CorrelatorPhotonStream::sum_of_weights";

%feature("docstring") CorrelatorPhotonStream::mean_count_rate "
CorrelatorPhotonStream::mean_count_rate";

%feature("docstring") CorrelatorPhotonStream::set_time_axis_calibration "
CorrelatorPhotonStream::set_time_axis_calibration
Set time axis calibration. The time axis calibration if the duration of between
two sync signals (macro time clock)  

Parameters
----------
* `v` :  
    time axis calibration (duration between sync clock signals) in seconds  
";

%feature("docstring") CorrelatorPhotonStream::get_time_axis_calibration "
CorrelatorPhotonStream::get_time_axis_calibration
Returns
-------
The calibration of the time axis in seconds. The time axis calibration is the
duration of a sync signal (macro time clock).  
";

%feature("docstring") CorrelatorPhotonStream::set_tttr "
CorrelatorPhotonStream::set_tttr";

%feature("docstring") CorrelatorPhotonStream::get_tttr "
CorrelatorPhotonStream::get_tttr";

%feature("docstring") CorrelatorPhotonStream::make_fine_times "
CorrelatorPhotonStream::make_fine_times
Changes the time events by adding the micro time to the macro time  

Changes the time events by adding the micro time to the macro time. The micro
times should match the macro time, i.e., the length of the micro time array
should be the at least the same length as the macro time array.  

Parameters
----------
* `t` :  
    An array containing the time events (macro times)  
* `n_times` :  
    The number of macro times.  
* `tac` :  
    An array containing the micro times of the corresponding macro times !  
";

// File: struct_curve_mapping__t.xml


%feature("docstring") CurveMapping_t "

C++ includes: TTTRHeaderTypes.h
";

// File: class_decay_fit.xml


%feature("docstring") DecayFit "

C++ includes: DecayFit.h
";

%feature("docstring") DecayFit::modelf "
DecayFit::modelf
Function to compute a model fluorescence decay.  

Parameters
----------
* `param` :  
    array containing the model parameters  
* `irf` :  
    instrument response function in Jordi format (parallel, perpendicular)  
* `bg[in]` :  
    background pattern in Jordi format (parallel, perpendicular)  
* `Nchannels[in]` :  
    number of channels (half the length of the Jordi arrays)  
* `dt[in]` :  
    time difference between two consecutive counting channels  
* `corrections[in]` :  
    array with corrections (details see implementations)  
* `mfunction[out]` :  
    output array of the computed decay in Jordi format. The output array has to
    have twice the number of channels. It needs to be allocated by beforehand.  

Returns
-------
integer For reporting failures (default 0)  
";

%feature("docstring") DecayFit::targetf "
DecayFit::targetf
Target function (to minimize)  

Computes the model function and returns a score that quantifies the discrepancy
between the data and the model.  

Parameters
----------
* `x[in`, `out]` :  
    a vector of length that that contains the model parameters  
* `pv[in]` :  
    a pointer to a MParam structure that contains the data and a set of
    corrections.  

Returns
-------
a normalized chi2  
";

%feature("docstring") DecayFit::fit "
DecayFit::fit
Function that optimizes parameters of model23 to data.  

Parameters
----------
* `x[in`, `out]` :  
    a vector of length that that contains the starting parameters  
* `fixed` :  
    an array that specifies if a parameter is optimized. If a value is set to 1,
    the parameter is optimized.  
* `p` :  
    an instance of MParam that contains all relevant information  

Returns
-------  
";

%feature("docstring") DecayFit::correct_input "
DecayFit::correct_input
Correct input parameters and compute values  

Parameters
----------
* `x[in`, `out]` :  
    input output array (see implementations of derived classes)  
* `xm[in`, `out]` :  
    array that will contain the corrected parameters  
* `corrections[in]` :  
    array with correction parameters  
* `return_r[in]` :  
    if set to true (positive) computes the anisotropy and returns the scatter
    corrected and the signal (no scatter correction) anisotropy and writes the
    values to the input/output vector x.  
";

// File: class_decay_fit23.xml


%feature("docstring") DecayFit23 "

C++ includes: DecayFit23.h
";

%feature("docstring") DecayFit23::modelf "
DecayFit23::modelf
Single exponential model function with single rotational correlation time, with
scatter contribution (BIFL scatter model)  

This function computes the fluorescence decay in the parallel and perpendicular
detection channel for a single exponential decay with a fluorescence lifetime
tau, a single rotational correlation time rho, and separate instrument response
functions for the parallel and perpendicular detection channels. The model
considers the faction of scattered light in the two detection channels by the
parameter gamma. The scattered light contribution is handled by patterns for the
light in the parallel and perpendicular detection channel.  

The instrument response function, the background, and the computed model
function are in the Jordi format, i.e., stacked one-dimensional arrays of the
parallel and perpendicular detection channel.  

Parameters
----------
* `param` :  
    array containing the model parameters [0] tau, [1] gamma, [2] r0, [3] rho  
* `irf` :  
    instrument response function in Jordi format (parallel, perpendicular)  
* `bg[in]` :  
    background pattern in Jordi format (parallel, perpendicular)  
* `Nchannels[in]` :  
    number of channels (half the length of the Jordi arrays)  
* `dt[in]` :  
    time difference between two consecutive counting channels  
* `corrections[in]` :  
    [0] excitation period, [1] g factor, [2] l1, [3] l2, [4] convolution stop
    channel number  
* `mfunction[out]` :  
    output array of the computed decay in Jordi format. The output array has to
    have twice the number of channels. It needs to be allocated by beforehand.  

Returns
-------
integer (not used, 0 by default)  
";

%feature("docstring") DecayFit23::targetf "
DecayFit23::targetf
Target function (to minimize) for fit23.  

Computes the model function 23 and returns a score that quantifies the
discrepancy between the data and the model.  

Parameters
----------
* `x[in`, `out]` :  
    a vector of length that that contains the starting parameters for the
    optimization and is used to return the optimized parameters. [0]
    fluorescence lifetime - tau (in) [1] fraction of scattered light - gamma
    (in) [2] fundamental anisotropy - r0 (in) [3] rotational correlation time -
    rho (in) [4] if negative reduce contribution of background photons from
    scoring function - Soft BIFL scatter fit? (flag) (in) [5] specifies type of
    score that is returned - 2I*: P+2S? (flag), this parameter only affects the
    returned score and does not influence the fitting (in) [6] background
    corrected anisotropy - r Scatter (out) [7] anisotropy without background
    correction - r Experimental (out)  
* `pv[in]` :  
    a pointer to a MParam structure that contains the data and a set of
    corrections.  

Returns
-------
a normalized chi2  
";

%feature("docstring") DecayFit23::fit "
DecayFit23::fit
Function that optimizes parameters of model23 to data.  

Parameters
----------
* `x[in`, `out]` :  
    a vector of length that that contains the starting parameters for the
    optimization and is used to return the optimized parameters. [0]
    fluorescence lifetime - tau (in,out) [1] fraction of scattered light - gamma
    (in,out) [2] fundamental anisotropy - r0 () [3] rotational correlation time
    - rho (in,out) [4] if negative reduce contribution of background photons
    from scoring function - Soft BIFL scatter fit? (flag) (in) [5] specifies
    type of score that is returned - 2I*: P+2S? (flag), this parameter only
    affects the returned score and does not influence the fitting (in) [6]
    background corrected anisotropy - r Scatter (out) [7] anisotropy without
    background correction - r Experimental (out)  
* `fixed` :  
    an array at least of length 4 for the parameters tau, gamma, r0, and rho
    that specifies if a parameter is optimized. If a value is set to 1, the
    parameter is optimized.  
* `p` :  
    an instance of MParam that contains all relevant information, i.e.,
    experimental data, the instrument response function, the needed corrections
    ( g-factor, l1, l2)  

Returns
-------  
";

%feature("docstring") DecayFit23::correct_input "
DecayFit23::correct_input
Correct input parameters and compute anisotropy  

This function corrects the input parameters for fit23 and takes care of
unreasonable values. The fluorescence lifetime is constraint to positive values,
gamma (the fraction of scattered light) is constraint to values between 0.0 and
0.999, the rotational correlation time rho is (if the global variable fixedrho
is set to true) to the value that corresponds to the Perrin equation (for the
computed, experimental anisotropy). Moreover, this function computes the
anisotropy based on the corrected (g-factor, l1, l2, background) intensities, if
the variable return_r is set to true.  

Parameters
----------
* `x[in`, `out]` :  
    array of length 8 that contains parameters x[0] fluorescence lifetime - tau;
    x[1] fraction of scattered light - gamma; x[2] fundamental anisotropy - r0
    x[3] rotational time - rho; x[4] softbifl - flag specifying the type of bifl
    fit (not used here) x[5] p2s_twoIstar - flag specifying the type of chi2
    calculation (not used here) x[6] background corrected anisotropy x[7]
    anisotropy without background correction  
* `xm[in`, `out]` :  
    array that will contain the corrected parameters  
* `corrections[in]` :  
* `return_r[in]` :  
    if set to true (positive) computes the anisotropy and returns the scatter
    corrected and the signal (no scatter correction) anisotropy and writes the
    values to the input/output vector x.  
";

// File: class_decay_fit24.xml


%feature("docstring") DecayFit24 "

C++ includes: DecayFit24.h
";

%feature("docstring") DecayFit24::modelf "
DecayFit24::modelf
Bi-exponential model function.  

Bi-exponential model function with two fluorescence lifetimes tau1, tau2 and
amplitude of the second lifetime A2, fraction scattered light gamma, and a
constant offset. A2 (A1 + A2 = 1)  

The model function does not describe anisotropy. The decays passed as a Jordi
format are treated identical in first and the second channel of the stacked
arrays.  

mfunction[i] * (1. - gamma) / sum_m + bg[i] * gamma / sum_s + offset  

Parameters
----------
* `param` :  
    array containing the parameters of the model [0] tau1, [1] gamma, [2] tau2,
    [3] A2, [4] offset  
* `irf` :  
    instrument response function in Jordi format  
* `bg[in]` :  
    background pattern in Jordi format  
* `Nchannels[in]` :  
    number of channels (half the length of the Jordi arrays)  
* `dt[in]` :  
    time difference between two consecutive counting channels  
* `corrections[in]` :  
    [0] excitation period, [1] unused, [2] unused, [3] unused, [4] convolution
    stop channel.  
* `mfunction[out]` :  
    output array of the computed decay in Jordi format. The output array has to
    have twice the number of channels. It needs to be allocated by beforehand.  

Returns
-------  
";

%feature("docstring") DecayFit24::targetf "
DecayFit24::targetf
Target function (to minimize) for fit23.  

Parameters
----------
* `x` :  
    array containing the parameters of the model [0] tau1, [1] gamma, [2] tau2,
    [3] A2, [4] offset  
* `pv[in]` :  
    a pointer to a MParam structure that contains the data and a set of
    corrections.  

Returns
-------
a normalized chi2  
";

%feature("docstring") DecayFit24::fit "
DecayFit24::fit
Fit a bi-exponential decay model  

This function fits a bi-exponential decay model to two decays that are stacked
using global parameters for the lifetimes and amplitudes.  

Bi-exponential model function with two fluorescence lifetimes tau1, tau2 and
amplitude of the second lifetime A2, fraction scattered light gamma, and a
constant offset. A2 (A1 + A2 = 1)  

The model function does not describe anisotropy. The decays passed as a Jordi
format are treated identical in first and the second channel of the stacked
arrays.  

The anisotropy is computed assuming that the first and the second part of the
Jordi input arrays are for parallel and perpendicular using the correction array
of the attribute p of the type MParam.  

Parameters
----------
* `x` :  
    array containing the parameters of the model [0] tau1, [1] gamma, [2] tau2,
    [3] A2, [4] offset, [5] BIFL scatter fit? (flag) - if smaller than 0 uses
    soft bifl scatter fit (seems to be unused) [6] r Scatter (output only), [7]
    r Experimental (output only)  
* `fixed` :  
    an array at least of length 5 for the parameters [0] tau1, [1] gamma, [2]
    tau2, [3] A2, [4] offset. If a value is not set to fixed the parameter is
    optimized.  
* `p` :  
    an instance of MParam that contains relevant information. Here, experimental
    data, the instrument response function, and the background decay are used.  

Returns
-------
Quality parameter 2I*  
";

%feature("docstring") DecayFit24::correct_input "
DecayFit24::correct_input
Correct input parameters and compute anisotropy for fit24.  

limits (0.001 < A2 < 0.999), (0.001 < gamma < 0.999), (tau1 > 0), (tau2 > 0),
background > 0 (called offset in other places)  

Parameters
----------
* `x[in`, `out]` :  
    [0] tau1, [1] gamma [2] tau2, [3] A2, [4] background, [5] BIFL scatter fit?
    (flag, not used), [6] anisotropy r (scatter corrected, output), [7]
    anisotropy (no scatter correction, output)  
* `xm[out]` :  
    array for corrected parameters (amplied range)  
* `corrections` :  
    [1] g factor, [2] l1, [3] l3  
* `return_r` :  
    if true computes the anisotropy.  

Returns
-------  
";

// File: class_decay_fit25.xml


%feature("docstring") DecayFit25 "

C++ includes: DecayFit25.h
";

%feature("docstring") DecayFit25::correct_input "
DecayFit25::correct_input
adjust parameters for fit25 and compute anisotropy  

Makes sure that (0 < gamma < 0.999) and (0<rho).  

Parameters
----------
* `x` :  
* `xm` :  
* `corrections` :  
* `return_r` :  

Returns
-------  
";

%feature("docstring") DecayFit25::targetf "
DecayFit25::targetf
Function used to compute the target value in fit 25  

This is misleadingly named target25. Fit25 selects out of a set of 4 lifetimes
the lifetime that describes best the data.  

Parameters
----------
* `x` :  
* `pv` :  

Returns
-------  
";

%feature("docstring") DecayFit25::fit "
DecayFit25::fit
Selects the lifetime out of a set of 4 fixed lifetimes that best describes the
data.  

This function selects out of a set of 4 lifetimes tau the lifetime that fits
best the data and returns the lifetime through the parameter x[0].  

If softBIFL flag is set to (x[6] < 0) and fixed[4] is zero gamma is optimized
for each lifetime tau and the best gamma is returned by x[4]. The gamma is
fitted with fit23.  

Parameters
----------
* `x` :  
    array containing the parameters [0] tau1 output for best tau (always fixed),
    [1] tau2 (always fixed), [2] tau3 (always fixed), [3] tau4 (always fixed),
    [4] gamma (input, output), [5] fundamental anisotropy r0, [6] BIFL scatter
    fit? (flag), [7] r Scatter (output only), [8] r Experimental (output only)  
* `fixed` :  
    array that is of least of length 5. Only the element fixed[4] is used. If
    fixed[4] is zero gamma is optimized for each lifetime.  
* `p` :  
    an instance of MParam that contains all relevant information, i.e.,
    experimental data, the instrument response function, the needed corrections
    for the anisotropy (g-factor, l1, l2)  

Returns
-------  
";

// File: class_decay_fit26.xml


%feature("docstring") DecayFit26 "

C++ includes: DecayFit26.h
";

%feature("docstring") DecayFit26::correct_input "
DecayFit26::correct_input
Correct input for fit 26  

Constrains the fraction x1 of the first pattern to (0 < x1 < 1).  

Parameters
----------
* `x[in]` :  
    x[0] fraction of first pattern  
* `xm[out]` :  
    xm[0] corrected fraction of first pattern  
";

%feature("docstring") DecayFit26::targetf "
DecayFit26::targetf";

%feature("docstring") DecayFit26::fit "
DecayFit26::fit
Pattern-fit  

Fits the fraction of a mixture of two patterns  

The two patterns are set by the attributes irf and bg of the MParam structure.  

Parameters
----------
* `x` :  
    [0] fraction of pattern 1  
* `fixed` :  
    not used  
* `p` :  
    an instance of MParam that contains the patterns. The fist pattern is
    contained in the instrument response function array, the second in the
    background, array, the experimental data is in the array expdata.  

Returns
-------  
";

// File: struct_decay_fit_corrections.xml


%feature("docstring") DecayFitCorrections "

C++ includes: DecayFit.h
";

%feature("docstring") DecayFitCorrections::set_gamma "
DecayFitCorrections::set_gamma";

%feature("docstring") DecayFitCorrections::str "
DecayFitCorrections::str";

%feature("docstring") DecayFitCorrections::DecayFitCorrections "
DecayFitCorrections::DecayFitCorrections";

// File: struct_decay_fit_integrate_signals.xml


%feature("docstring") DecayFitIntegrateSignals "

C++ includes: DecayFit.h
";

%feature("docstring") DecayFitIntegrateSignals::Fp "
DecayFitIntegrateSignals::Fp";

%feature("docstring") DecayFitIntegrateSignals::Fs "
DecayFitIntegrateSignals::Fs";

%feature("docstring") DecayFitIntegrateSignals::r "
DecayFitIntegrateSignals::r";

%feature("docstring") DecayFitIntegrateSignals::rho "
DecayFitIntegrateSignals::rho";

%feature("docstring") DecayFitIntegrateSignals::rs "
DecayFitIntegrateSignals::rs";

%feature("docstring") DecayFitIntegrateSignals::compute_signal_and_background "
DecayFitIntegrateSignals::compute_signal_and_background
Computes the total number of photons in the parallel and perpendicular detection
channel for the background and the measured signal. The computed number of
photons are stored in the static variables Sp, Ss, Bp, Bs.  

Parameters
----------
* `p[in]` :  
    a pointer to a MParam object  
";

%feature("docstring") DecayFitIntegrateSignals::normM "
DecayFitIntegrateSignals::normM
Normalizes the number of photons in the entire model function to the number of
experimental photons.  

Here, the Number of experimental photons is Sp + Ss (signal in parallel and
perpendicular). Sp and Ss are global variables that can be computed by
`compute_signal_and_background`.  

Parameters
----------
* `M[in`, `out]` :  
    array containing the model function in Jordi format  
* `Nchannels[in]` :  
    number of channels in the experiment(half length of
    M array)  
";

%feature("docstring") DecayFitIntegrateSignals::normM "
DecayFitIntegrateSignals::normM
Normalizes a model function (that is already normalized to a unit area) to the
total number of photons in parallel and perpendicular,  

Parameters
----------
* `M[in`, `out]` :  
    array containing the model function in Jordi format  
* `s[in]` :  
    a scaling factor by which the model function is divided.  
* `Nchannels[in]` :  
    the number of channels in the model function (half length of M array)  
";

%feature("docstring") DecayFitIntegrateSignals::normM_p2s "
DecayFitIntegrateSignals::normM_p2s
Normalizes the number of photons in the model function for Ss and Sp
individually to the number of experimental photons in Ss and Sp.  

Here, the number of experimental photons are global variables that can be
computed by `compute_signal_and_background`.  

Parameters
----------
* `M` :  
    array[in,out] containing the model function in Jordi format  
* `Nchannels[in]` :  
    number of channels in the experiment (half length of M array)  
";

%feature("docstring") DecayFitIntegrateSignals::str "
DecayFitIntegrateSignals::str";

%feature("docstring") DecayFitIntegrateSignals::DecayFitIntegrateSignals "
DecayFitIntegrateSignals::DecayFitIntegrateSignals";

// File: struct_decay_fit_settings.xml


%feature("docstring") DecayFitSettings "

C++ includes: DecayFit.h
";

%feature("docstring") DecayFitSettings::str "
DecayFitSettings::str";

// File: class_decay_phasor.xml


%feature("docstring") DecayPhasor "

C++ includes: DecayPhasor.h
";

%feature("docstring") DecayPhasor::compute_phasor "
DecayPhasor::compute_phasor
Compute the phasor (g,s) for a selection of micro times  

This function computes the phasor (g,s) for a set of micro times that are
selected out of an vector. The microtimes are selected by a second vector. The
second vector speciefies which indices of the microtime vector are used to
compute the phasor.  

Parameters
----------
* `micro_times` :  
    vector of micro times  
* `idxs` :  
    vector of selected indices  
* `minimum_number_of_photons` :  
* `frequency` :  
    the frequency of the phasor  
* `g_irf` :  
    g-value of instrument response phasor  
* `s_irf` :  
    s-value of instrument response phasor  

Returns
-------
vector of length 2: first element g-value, second element s-value  
";

%feature("docstring") DecayPhasor::compute_phasor_bincounts "
DecayPhasor::compute_phasor_bincounts
Compute the phasor (g,s) for a histogram / bincounts  

This function computes the phasor (g,s) for bincounted micro times  

Parameters
----------
* `bincounts` :  
    vector bincounts  
* `minimum_number_of_photons` :  
* `frequency` :  
    the frequency of the phasor  
* `g_irf` :  
    g-value of instrument response phasor  
* `s_irf` :  
    s-value of instrument response phasor  

Returns
-------
vector of length 2: first element g-value, second element s-value  
";

%feature("docstring") DecayPhasor::g "
DecayPhasor::g
https://journals.plos.org/plosone/article/file?type=supplementary&id=info:doi/10.1371/journal.pone.0194578.s001  

Parameters
----------
* `g_irf` :  
    g-value of instrument response phasor  
* `s_irf` :  
    s-value of instrument response phasor  
* `g_exp` :  
* `s_exp` :  

Returns
-------  
";

%feature("docstring") DecayPhasor::s "
DecayPhasor::s
https://journals.plos.org/plosone/article/file?type=supplementary&id=info:doi/10.1371/journal.pone.0194578.s001  

Parameters
----------
* `g_irf` :  
* `s_irf` :  
* `g_exp` :  
* `s_exp` :  

Returns
-------  
";

// File: class_header.xml


%feature("docstring") Header "

Constructor for the a file pointer and the container type of the file
represented by the file pointer. The container type refers either to a PicoQuant
(PQ) PTU or HT3 file, or a BeckerHickl (BH) spc file. There are three different
types of BH spc files SPC130, SPC600_256 (256 bins in micro time) or SPC600_4096
(4096 bins in micro time). PQ HT3 files may contain different TTTR record types
depending on the counting device (HydraHarp, PicoHarp) and firmware revision of
the counting device. Similarly, PTU files support a diverse set of TTTR records.  

Parameters
----------
* `fpin` :  
    the file pointer to the TTTR file  
* `tttr_container_type` :  
    the container type  
";

// File: class_histogram.xml


%feature("docstring") Histogram "

C++ includes: Histogram.h
";

%feature("docstring") Histogram::update "
Histogram::update";

%feature("docstring") Histogram::get_histogram "
Histogram::get_histogram";

%feature("docstring") Histogram::set_axis "
Histogram::set_axis";

%feature("docstring") Histogram::set_axis "
Histogram::set_axis";

%feature("docstring") Histogram::get_axis "
Histogram::get_axis";

%feature("docstring") Histogram::Histogram "
Histogram::Histogram";

%feature("docstring") Histogram::~Histogram "
Histogram::~Histogram";

// File: class_histogram_axis.xml


%feature("docstring") HistogramAxis "

C++ includes: HistogramAxis.h
";

%feature("docstring") HistogramAxis::update "
HistogramAxis::update
Recalculates the bin edges of the axis  
";

%feature("docstring") HistogramAxis::setAxisType "
HistogramAxis::setAxisType";

%feature("docstring") HistogramAxis::getNumberOfBins "
HistogramAxis::getNumberOfBins";

%feature("docstring") HistogramAxis::getBinIdx "
HistogramAxis::getBinIdx";

%feature("docstring") HistogramAxis::getBins "
HistogramAxis::getBins";

%feature("docstring") HistogramAxis::getBins "
HistogramAxis::getBins";

%feature("docstring") HistogramAxis::getName "
HistogramAxis::getName";

%feature("docstring") HistogramAxis::setName "
HistogramAxis::setName";

%feature("docstring") HistogramAxis::HistogramAxis "
HistogramAxis::HistogramAxis";

%feature("docstring") HistogramAxis::HistogramAxis "
HistogramAxis::HistogramAxis";

// File: struct_l_v_double_array.xml


%feature("docstring") LVDoubleArray "

C++ includes: LvArrays.h
";

%feature("docstring") LVDoubleArray::~LVDoubleArray "
LVDoubleArray::~LVDoubleArray";

%feature("docstring") LVDoubleArray::str "
LVDoubleArray::str";

// File: struct_l_v_i32_array.xml


%feature("docstring") LVI32Array "

Structures and functions used for LabView interface.  

fit2x was originally developed as a C backend for LabView software. Therefore,
the interface with fit2x uses structures that can be accessed by Labview. In
order to make an interfacing with Python and other languages possible there is a
this files defines a set of functions that facilitate the creation of the
LabView structures.  

C++ includes: LvArrays.h
";

%feature("docstring") LVI32Array::~LVI32Array "
LVI32Array::~LVI32Array";

%feature("docstring") LVI32Array::str "
LVI32Array::str";

// File: struct_m_param.xml


%feature("docstring") MParam "

C++ includes: LvArrays.h
";

%feature("docstring") MParam::~MParam "
MParam::~MParam";

// File: struct_param_struct__t.xml


%feature("docstring") ParamStruct_t "

C++ includes: TTTRHeaderTypes.h
";

// File: class_pda.xml


%feature("docstring") Pda "

C++ includes: Pda.h
";

%feature("docstring") Pda::evaluate "
Pda::evaluate
Computes the S1S2 histogram.  
";

%feature("docstring") Pda::Pda "
Pda::Pda
Constructor creating a new Pda object  

A Pda object can be used to compute Photon Distribution Analysis histograms.  

Parameters
----------
* `hist2d_nmax` :  
    the maximum number of photons  
* `hist2d_nmin` :  
    the minimum number of photons considered  
";

%feature("docstring") Pda::~Pda "
Pda::~Pda";

%feature("docstring") Pda::append "
Pda::append
Appends a species.  

A species is defined by the probability of detecting a photon in the first
detection channel.  

Parameters
----------
* `amplitude` :  
    the amplitude (fraction) of the species  
* `probability_ch1` :  
    the probability of detecting the species in the first detection channel  
";

%feature("docstring") Pda::clear_probability_ch1 "
Pda::clear_probability_ch1
Clears the model and removes all species.  
";

%feature("docstring") Pda::get_amplitudes "
Pda::get_amplitudes
Returns the amplitudes of the species  

Parameters
----------
* `output[out]` :  
    A C type array containing the amplitude of the species  
* `n_output[out]` :  
    The number of species  
";

%feature("docstring") Pda::set_amplitudes "
Pda::set_amplitudes
Sets the amplitudes of the species.  

Parameters
----------
* `input[in]` :  
    A C type array that contains the amplitude of the species  
* `n_input[in]` :  
    The number of species  
";

%feature("docstring") Pda::set_callback "
Pda::set_callback
Set the callback (cb) for the computation of a 1D histogram.  

The cb function recudes two dimensional values, i.e., the intensity in channel
(ch1) and ch2 to a one dimensional number. The cb is used to compute either FRET
efficiencies, etc.  

Parameters
----------
* `callback[in]` :  
    object that computes the value on a 1D histogram.  
";

%feature("docstring") Pda::get_S1S2_matrix "
Pda::get_S1S2_matrix
Returns the S1S2 matrix that contains the photon counts in the two channels  

Parameters
----------
* `output[out]` :  
    the S1S2 matrix  
* `n_output1[out]` :  
    dimension 1 of the matrix  
* `n_output2[out]` :  
    dimension 2 of the matrix  
";

%feature("docstring") Pda::set_probability_spectrum_ch1 "
Pda::set_probability_spectrum_ch1
Set the theoretical probability spectrum of detecting a photon in the first
channel  

The probability spectrum is an interleaved array of the amplitudes and the
probabilities of detecting a photon in the first channel  

Parameters
----------
* `input[in]` :  
    a C type array containing the probability spectrum  
* `n_input[in]` :  
    the number of array elements  
";

%feature("docstring") Pda::get_probabilities_ch1 "
Pda::get_probabilities_ch1
Returns the amplitudes of the species  

Parameters
----------
* `output[out]` :  
    A C type array containing the amplitude of the species  
* `n_output[out]` :  
    The number of species  
";

%feature("docstring") Pda::set_probabilities_ch1 "
Pda::set_probabilities_ch1
Sets the theoretical probabilities for detecting a the species in the first
channel.  

Parameters
----------
* `input[in]` :  
    A C type array that contains the probabilities of the species  
* `n_input[in]` :  
    The number of species  
";

%feature("docstring") Pda::get_probability_spectrum_ch1 "
Pda::get_probability_spectrum_ch1
Get the theoretical probability spectrum of detecting a photon in the first
channel  

The probability spectrum is an interleaved array of the amplitudes and the
probabilities of detecting a photon in the first channel  

Parameters
----------
* `output[out]` :  
    array containing the probability spectrum  
* `n_output[out]` :  
    number of elements in the output array  
";

%feature("docstring") Pda::setPF "
Pda::setPF
Set the probability P(F)  

Parameters
----------
* `input[in]` :  
* `n_input[in]` :  
";

%feature("docstring") Pda::getPF "
Pda::getPF
Set the probability P(F)  
";

%feature("docstring") Pda::get_1dhistogram "
Pda::get_1dhistogram
Returns a one dimensional histogram of the 2D counting array of the two
channels.  

Parameters
----------
* `histogram_x[out]` :  
    histogram X axis  
* `n_histogram_x[out]` :  
    dimension of the x-axis  
* `histogram_y[out]` :  
    array containing the computed histogram  
* `n_histogram_y[out]` :  
    dimension of the histogram  
* `x_max[in]` :  
    maximum x value of the histogram  
* `x_min[in]` :  
    minimum x value of the histogram  
* `nbins[int]` :  
    number of histogram bins  
* `log_x[in]` :  
    If set to true (default is true) the values on the x-axis are
    logarithmically spaced otherwise they have a linear spacing.  
* `s1s2[in]` :  
    Optional input for the S1S2 matrix. If this is set to a nullptr (default)
    the S1S2 matrix of the Pda object is used to compute the 1D histogram. If
    this is not set to nullptr and both dimensions set by n_input1 and n_input2
    are larger than zero. The input is used as S1S2 matrix. The input matrix
    must be quadratic.  
* `n_min[in]` :  
    Minimum number of photons in the histogram. If set to -1 the number set when
    the Pda object was instancitated is used.  
* `skip_zero_photon[in]` :  
    When this option is set to true only elements of the s1s2 matrix i,j (i>0
    and j>0) are considered.  
* `amplitudes[in]` :  
    The species amplitudes (optional). This updates the s1s2 matrix of the
    object.  
* `probabilities_ch1[in]` :  
    The theoretical probabilities of detecting the species in channel
    1(optional)This updates the s1s2 matrix of the object.  
";

%feature("docstring") Pda::get_max_number_of_photons "
Pda::get_max_number_of_photons
The maximum number of photons in the SgSr matrix.  
";

%feature("docstring") Pda::set_max_number_of_photons "
Pda::set_max_number_of_photons
Set the maximum number of photons in the S1S2 matrix  

Note: the size of the pF array must agree with the maximum number of photons!  

Parameters
----------
* `nmax[in]` :  
    the maximum number of photons  
";

%feature("docstring") Pda::get_min_number_of_photons "
Pda::get_min_number_of_photons
The minimum number of photons in the SgSr matrix.  
";

%feature("docstring") Pda::set_min_number_of_photons "
Pda::set_min_number_of_photons
Set the minimum number of photons in the SgSr matrix.  
";

%feature("docstring") Pda::get_ch1_background "
Pda::get_ch1_background
Get the background in the green channel.  
";

%feature("docstring") Pda::set_ch1_background "
Pda::set_ch1_background
Set the background in the green channel.  
";

%feature("docstring") Pda::get_ch2_background "
Pda::get_ch2_background
Get the background in the red channel.  
";

%feature("docstring") Pda::set_ch2_background "
Pda::set_ch2_background
Set the background in the red channel.  
";

%feature("docstring") Pda::is_valid_sgsr "
Pda::is_valid_sgsr
Returns true if the SgSr histogram is valid, i.e., if output is correct for the
input parameter. This value is set to true by evaluate.  
";

%feature("docstring") Pda::set_valid_sgsr "
Pda::set_valid_sgsr
Set the SgSr histogram to valid (only used for testing)  
";

%feature("docstring") Pda::compute_experimental_histograms "
Pda::compute_experimental_histograms
Parameters
----------
* `tttr_data[in]` :  
* `s1s2[out]` :  
* `dim1[out]` :  
* `dim2[out]` :  
* `ps[out]` :  
* `dim_ps[out]` :  
* `channels_1` :  
    routing channel numbers that are used for the first channel in the S1S2
    matrix. Photons with this channel number are counted and increment values in
    the S1S2 matrix.  
* `channels_2` :  
    routing channel numbers that are used for the second channel in the S1S2
    matrix.Photons with this channel number are counted and increment values in
    the S1S2 matrix.  
* `maximum_number_of_photons` :  
    The maximum number of photons in the computed S1S2 matrix  
* `minimum_number_of_photons` :  
    The minimum number of photons in a time window and in the S1S2 matrix  
* `minimum_time_window_length` :  
    The minimum length of a time windows in units of milli seconds.  
";

%feature("docstring") Pda::S1S2_pF "
Pda::S1S2_pF
calculating p(G,R), several ratios using the same same P(F)  

Parameters
----------
* `S1S2[]` :  
    see sgsr_pN  
* `pF[in]` :  
    input: p(F)  
* `Nmax[in]` :  
* `background_ch1[in]` :  
* `background_ch2[in]` :  
* `p_ch1[in]` :  
* `amplitudes[in]` :  
    corresponding amplitudes  
";

%feature("docstring") Pda::conv_pF "
Pda::conv_pF
Convolves the Fluorescence matrix F1F2 with the background to yield the signal
matrix S1S2  

Parameters
----------
* `S1S2[out]` :  
* `F1F2[in]` :  
* `Nmax` :  
* `background_ch1` :  
* `background_ch2` :  
";

%feature("docstring") Pda::poisson_0toN "
Pda::poisson_0toN
Writes a Poisson distribution with an average lam, for 0..N into a vector
starting at a specified index.  

Parameters
----------
* `return_p[in`, `out]` :  
* `lam[in]` :  
* `return_dim[in]` :  
";

// File: class_pda_callback.xml


%feature("docstring") PdaCallback "

C++ includes: PdaCallback.h
";

%feature("docstring") PdaCallback::run "
PdaCallback::run";

%feature("docstring") PdaCallback::PdaCallback "
PdaCallback::PdaCallback";

%feature("docstring") PdaCallback::~PdaCallback "
PdaCallback::~PdaCallback";

// File: unionph__ph__t2__record.xml

// File: unionpq__hh__t2__record.xml

// File: unionpq__hh__t3__record.xml

// File: structpq__ht3__board__settings__t.xml


%feature("docstring") pq_ht3_board_settings_t "

C++ includes: TTTRHeaderTypes.h
";

// File: structpq__ht3___channel_header__t.xml


%feature("docstring") pq_ht3_ChannelHeader_t "

C++ includes: TTTRHeaderTypes.h
";

// File: structpq__ht3___header__t.xml


%feature("docstring") pq_ht3_Header_t "

The following represents the readable ASCII file header portion in a HT3 file.  

C++ includes: TTTRHeaderTypes.h
";

// File: structpq__ht3___t_t_mode_header__t.xml


%feature("docstring") pq_ht3_TTModeHeader_t "

C++ includes: TTTRHeaderTypes.h
";

// File: unionpq__ph__t3__record.xml

// File: structtag__head.xml


%feature("docstring") tag_head "

A Header Tag entry of a PTU file.  

C++ includes: TTTRHeaderTypes.h
";

// File: class_t_t_t_r.xml


%feature("docstring") TTTR "

C++ includes: TTTR.h
";

%feature("docstring") TTTR::Get "
TTTR::Get
Make shared pointer.  
";

%feature("docstring") TTTR::copy_from "
TTTR::copy_from
Copy the information from another TTTR object  

Parameters
----------
* `p2` :  
    the TTTR object which which the information is copied from  
* `include_big_data` :  
    if this is true also the macro time, micro time etc. are copied. Otherwise
    all other is copied  
";

%feature("docstring") TTTR::read_file "
TTTR::read_file
Reads the TTTR data contained in a file into the TTTR object  

Parameters
----------
* `fn` :  
    The filename that is read. If fn is a nullptr (default value is nullptr) the
    filename attribute of the TTTR object is used as filename.  
* `container_type` :  
    The container type.  

Returns
-------
Returns 1 in case the file was read without errors. Otherwise 0 is returned.  
";

%feature("docstring") TTTR::append_events "
TTTR::append_events";

%feature("docstring") TTTR::append_event "
TTTR::append_event";

%feature("docstring") TTTR::append "
TTTR::append";

%feature("docstring") TTTR::size "
TTTR::size";

%feature("docstring") TTTR::get_used_routing_channels "
TTTR::get_used_routing_channels
Returns an array containing the routing channel numbers that are contained
(used) in the TTTR file.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_macro_times "
TTTR::get_macro_times
Returns an array containing the macro times of the valid TTTR events.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_micro_times "
TTTR::get_micro_times
Returns an array containing the micro times of the valid TTTR events.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_intensity_trace "
TTTR::get_intensity_trace
Returns a intensity trace that is computed for a specified integration window  

Parameters
----------
* `output` :  
    the returned intensity trace  
* `n_output` :  
    the number of points in the intensity trace  
* `time_window_length` :  
    the length of the integration time windows in units of milliseconds.  
";

%feature("docstring") TTTR::get_routing_channel "
TTTR::get_routing_channel
Returns an array containing the routing channel numbers of the valid TTTR
events.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_event_type "
TTTR::get_event_type
Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_number_of_micro_time_channels "
TTTR::get_number_of_micro_time_channels
Returns the number of micro time channels that fit between two macro time
clocks.  

Returns
-------
maximum valid number of micro time channels  
";

%feature("docstring") TTTR::get_n_valid_events "
TTTR::get_n_valid_events
Returns
-------
number of valid events in the TTTR file  
";

%feature("docstring") TTTR::get_tttr_container_type "
TTTR::get_tttr_container_type
Returns
-------
the container type that was used to open the file  
";

%feature("docstring") TTTR::select "
TTTR::select";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
Constructor  
";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
Copy constructor.  
";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
Constructor that can read a file  

Parameters
----------
* `filename` :  
    TTTR filename  
* `container_type` :  
    container type as int (0 = PTU; 1 = HT3; 2 = SPC-130; 3 = SPC-600_256; 4 =
    SPC-600_4096; 5 = PHOTON-HDF5)  
* `read_input` :  
    if true reads the content of the file  

PQ_PTU_CONTAINER 0 PQ_HT3_CONTAINER 1 BH_SPC130_CONTAINER 2
BH_SPC600_256_CONTAINER 3 BH_SPC600_4096_CONTAINER 4  
";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
Parameters
----------
* `filename` :  
    TTTR filename  
* `container_type` :  
    container type as int (0 = PTU; 1 = HT3; 2 = SPC-130; 3 = SPC-600_256; 4 =
    SPC-600_4096; 5 = PHOTON-HDF5)  
";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
Parameters
----------
* `filename` :  
    TTTR filename  
* `container_type` :  
    container type as string (PTU; HT3; SPC-130; SPC-600_256; SPC-600_4096;
    PHOTON-HDF5)  
";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
Constructor of TTTR object using arrays of the TTTR events  

If arrays of different size are used to initialize a TTTR object the shortest
array of all provided arrays is used to construct the TTTR object.  

Parameters
----------
* `macro_times` :  
    input array containing the macro times  
* `n_macrotimes` :  
    number of macro times  
* `micro_times` :  
    input array containing the microtimes  
* `n_microtimes` :  
    length of the of micro time array  
* `routing_channels` :  
    routing channel array  
* `n_routing_channels` :  
    length of the routing channel array  
* `event_types` :  
    array of event types  
* `n_event_types` :  
    number of elements in the event type array  
* `find_used_channels` :  
    if set to true (default) searches all indices to find the used routing
    channels  
";

%feature("docstring") TTTR::TTTR "
TTTR::TTTR
This constructor can be used to create a new TTTR object that only contains
records that are specified in the selection array.  

The selection array is an array of indices. The events with indices in the
selection array are copied in the order of the selection array to a new TTTR
object.  

Parameters
----------
* `parent` :  
* `selection` :  
* `n_selection` :  
* `find_used_channels` :  
    if set to true (default) searches all indices to find the used routing
    channels  
";

%feature("docstring") TTTR::~TTTR "
TTTR::~TTTR
Destructor.  
";

%feature("docstring") TTTR::get_filename "
TTTR::get_filename
getFilename Getter for the filename of the TTTR file  

Returns
-------
The filename of the TTTR file  
";

%feature("docstring") TTTR::get_tttr_by_selection "
TTTR::get_tttr_by_selection
Get a ptr to a TTTR object that is based on a selection on the current TTTR
object. A selection is an array of indices of the TTTR events.  

Parameters
----------
* `selection` :  
* `n_selection` :  

Returns
-------  
";

%feature("docstring") TTTR::get_ranges_by_time_window "
TTTR::get_ranges_by_time_window
Returns time windows (tw), i.e., the start and the stop indices for a minimum tw
size, a minimum number of photons in a tw.  

Parameters
----------
* `output` :  
    [out] Array containing the interleaved start and stop indices of the tws in
    the TTTR object.  
* `n_output` :  
    [out] Length of the output array  
* `minimum_window_length` :  
    [in] Minimum length of a tw (mandatory).  
* `maximum_window_length` :  
    [in] Maximum length of a tw (optional).  
* `minimum_number_of_photons_in_time_window` :  
    [in] Minimum number of photons a selected tw contains (optional)  
* `maximum_number_of_photons_in_time_window` :  
    [in] Maximum number of photons a selected tw contains (optional)  
* `invert` :  
    [in] If set to true, the selection criteria are inverted.  
";

%feature("docstring") TTTR::get_selection_by_channel "
TTTR::get_selection_by_channel
Get events indices by the routing channel number  

This method returns an array that contains the event / photon indices of events
with routing channel numbers that are found in the selection input array.  

Parameters
----------
* `output` :  
    indices of the events  
* `n_output` :  
    number of selected events  
* `input` :  
    routing channel number for selection of events  
* `n_input` :  
    number of routing channels for selection of events  
";

%feature("docstring") TTTR::get_tttr_by_channel "
TTTR::get_tttr_by_channel";

%feature("docstring") TTTR::get_selection_by_count_rate "
TTTR::get_selection_by_count_rate
List of indices where the count rate is smaller than a maximum count rate  

The count rate is specified by providing a time window that slides over the time
array and the maximum number of photons within the time window.  

Parameters
----------
* `output` :  
    the output array that will contain the selected indices  
* `n_output` :  
    the number of elements in the output array  
* `time_window` :  
    the length of the time window in milliseconds  
* `n_ph_max` :  
    the maximum number of photons within a time window  
";

%feature("docstring") TTTR::get_tttr_by_count_rate "
TTTR::get_tttr_by_count_rate";

%feature("docstring") TTTR::get_time_window_ranges "
TTTR::get_time_window_ranges
Returns time windows (tw), i.e., the start and the stop indices for a minimum tw
size, a minimum number of photons in a tw.  

Parameters
----------
* `output[out]` :  
    Array containing the interleaved start and stop indices of the tws in the
    TTTR object.  
* `n_output[out]` :  
    Length of the output array  
* `minimum_window_length[in]` :  
    Minimum length of a tw in units of ms (mandatory).  
* `maximum_window_length[in]` :  
    Maximum length of a tw (optional).  
* `minimum_number_of_photons_in_time_window[in]` :  
    Minimum number of photons a selected tw contains (optional) in units of
    seconds  
* `maximum_number_of_photons_in_time_window[in]` :  
    Maximum number of photons a selected tw contains (optional)  
* `invert[in]` :  
    If set to true, the selection criteria are inverted.  
";

%feature("docstring") TTTR::get_header "
TTTR::get_header
Get header returns the header (if present) as a map of strings.  
";

%feature("docstring") TTTR::set_header "
TTTR::set_header
Set header.  
";

%feature("docstring") TTTR::get_n_events "
TTTR::get_n_events
Returns the number of events in the TTTR file for cases no selection is
specified otherwise the number of selected events is returned.  

Returns
-------  
";

%feature("docstring") TTTR::write "
TTTR::write
Write the contents of a opened TTTR file to a new TTTR file.  

Parameters
----------
* `fn` :  
    filename  
* `container_type` :  
    container type (PTU; HT3; SPC-130; SPC-600_256; SPC-600_4096; PHOTON-HDF5)
    @oaram write_a_header if set to false no header is written - Writing correct
    headers is not implemented. Therefore, the default value is false.  

Returns
-------  
";

%feature("docstring") TTTR::write_spc132_events "
TTTR::write_spc132_events";

%feature("docstring") TTTR::write_hht3v2_events "
TTTR::write_hht3v2_events";

%feature("docstring") TTTR::write_header "
TTTR::write_header";

%feature("docstring") TTTR::shift_macro_time "
TTTR::shift_macro_time
Shift the macro time by a constant  

Parameters
----------
* `shift` :  
";

%feature("docstring") TTTR::get_microtime_histogram "
TTTR::get_microtime_histogram";

%feature("docstring") TTTR::mean_lifetime "
TTTR::mean_lifetime
Compute the mean lifetime by the moments of the decay and the instrument
response function.  
";

%feature("docstring") TTTR::get_count_rate "
TTTR::get_count_rate";

%feature("docstring") TTTR::get_mean_microtime "
TTTR::get_mean_microtime";

%feature("docstring") TTTR::get_number_of_records_by_file_size "
TTTR::get_number_of_records_by_file_size
Determines the number of records in a TTTR files (not for use with HDF5)  

Calculates the number of records in the file based on the file size. if  

Parameters
----------
* `offset` :  
    is passed the number of records is calculated by the file size the number of
    bytes in the file - offset and  
* `bytes_per_record.` :  
    If  
* `offset` :  
    is not specified the current location of the file pointer is used as an
    offset. If  
* `bytes_per_record` :  
    is not specified the attribute value bytes_per_record of the class instance
    is used.  
* `offset` :  
* `bytes_per_record` :  
";

%feature("docstring") TTTR::compute_microtime_histogram "
TTTR::compute_microtime_histogram
Computes a histogram of the TTTR data's micro times  

Parameters
----------
* `tttr_data` :  
    a pointer to the TTTR data  
* `histogram` :  
    pointer to which the histogram will be written (the memory is allocated but
    the method)  
* `n_histogram` :  
    the number of points in the histogram  
* `time` :  
    pointer to the time axis of the histogram (the memory is allocated by the
    method)  
* `n_time` :  
    the number of points in the time axis  
* `micro_time_coarsening` :  
    a factor by which the micro times in the TTTR object are divided (default
    value is 1).  
";

%feature("docstring") TTTR::compute_mean_lifetime "
TTTR::compute_mean_lifetime
Compute a mean lifetime by the moments of the decay and the instrument response
function.  

The computed lifetime is the first lifetime determined by the method of moments
(Irvin Isenberg, 1973, Biophysical journal).  

Parameters
----------
* `tttr_data` :  
    TTTR object for which the lifetime is computed  
* `tttr_irf` :  
    TTTR object that is used as IRF  
* `m0_irf[in]` :  
    Number of counts in the IRF (used if no TTTR object for IRF provided.  
* `m1_irf[in]` :  
    First moment of the IRF (used if no TTTR object for IRF provided.  
* `tttr_indices[in]` :  
    Optional list of indices for selecting a subset of the TTTR  
* `dt[in]` :  
    Time resolution of the micro time. If not provided extracted from the header
    (slow)  
* `minimum_number_of_photons[in]` :  
    Minimum number of photons. If less photons are in the dataset returns -1 as
    computed lifetime  
* `background` :  
    background pattern  
* `m0_bg` :  
    sum of background photons (overwritten if background pattern not empty)  
* `m1_bg` :  
    first moment of background pattern (overwritten if background pattern not
    empty)  
* `background_fraction` :  
    background fraction (if negative background is not scaled)  

Returns
-------
The computed lifetime  
";

%feature("docstring") TTTR::compute_count_rate "
TTTR::compute_count_rate
Compute the count rate  

Parameters
----------
* `tttr_data[in]` :  
    TTTR object for which the lifetime is computed  
* `macrotime_resolution[in]` :  
    If negative (default) reads macrotime resolution from header (slow)  

Returns
-------
Count rate  
";

%feature("docstring") TTTR::compute_mean_microtime "
TTTR::compute_mean_microtime";

// File: class_t_t_t_r_header.xml


%feature("docstring") TTTRHeader "

C++ includes: TTTRHeader.h
";

%feature("docstring") TTTRHeader::get_tttr_record_type "
TTTRHeader::get_tttr_record_type
Returns
-------
The TTTR container type of the associated TTTR file as a char  
";

%feature("docstring") TTTRHeader::set_tttr_record_type "
TTTRHeader::set_tttr_record_type
Parameters
----------
* `v` :  
    record type  
";

%feature("docstring") TTTRHeader::get_tttr_container_type "
TTTRHeader::get_tttr_container_type
The container type  

Returns
-------  
";

%feature("docstring") TTTRHeader::set_tttr_container_type "
TTTRHeader::set_tttr_container_type
Parameters
----------
* `v` :  
    container type  
";

%feature("docstring") TTTRHeader::get_bytes_per_record "
TTTRHeader::get_bytes_per_record
Stores the bytes per TTTR record of the associated TTTR file This attribute is
changed when a header is read  
";

%feature("docstring") TTTRHeader::end "
TTTRHeader::end";

%feature("docstring") TTTRHeader::size "
TTTRHeader::size
Number of meta data entries  
";

%feature("docstring") TTTRHeader::get_number_of_micro_time_channels "
TTTRHeader::get_number_of_micro_time_channels
The total (possible) number of micro time channels.  

The number of TAC channels (TAC - Time to analog converter) refers to the number
of micro time channels.  
";

%feature("docstring") TTTRHeader::get_macro_time_resolution "
TTTRHeader::get_macro_time_resolution
Resolution for the macro time in nanoseconds.  
";

%feature("docstring") TTTRHeader::get_micro_time_resolution "
TTTRHeader::get_micro_time_resolution
Resolution for the micro time in nanoseconds.  
";

%feature("docstring") TTTRHeader::get_pixel_duration "
TTTRHeader::get_pixel_duration
Duration of a pixel in LSM in units of macro time clock.  
";

%feature("docstring") TTTRHeader::get_line_duration "
TTTRHeader::get_line_duration
Duration of a line in LSM in units of macro time clock.  
";

%feature("docstring") TTTRHeader::get_effective_number_of_micro_time_channels "
TTTRHeader::get_effective_number_of_micro_time_channels
The number of micro time channels that fit between two macro times.  

The total (possible) number of TAC channels can exceed the number that fit
between two macro time channels. This function returns the effective number,
i.e., the number of micro time channels between two macro times. The micro time
channels that are outside of this bound should (usually) not be filled.  

Returns
-------
effective_tac_channels (that fit between to macro times)  
";

%feature("docstring") TTTRHeader::TTTRHeader "
TTTRHeader::TTTRHeader
Default constructor  
";

%feature("docstring") TTTRHeader::TTTRHeader "
TTTRHeader::TTTRHeader";

%feature("docstring") TTTRHeader::TTTRHeader "
TTTRHeader::TTTRHeader
Copy constructor.  
";

%feature("docstring") TTTRHeader::TTTRHeader "
TTTRHeader::TTTRHeader";

%feature("docstring") TTTRHeader::TTTRHeader "
TTTRHeader::TTTRHeader";

%feature("docstring") TTTRHeader::~TTTRHeader "
TTTRHeader::~TTTRHeader";

%feature("docstring") TTTRHeader::get_json "
TTTRHeader::get_json
Get a representation of the TTTRHeader meta data as a JSON string  

Parameters
----------
* `tag_name` :  
    name of requested tag (if no name is provided) the entire information in the
    TTTRHeader is returned  
* `idx` :  
    index of the tag  
* `indent` :  
    an integer that controls the indent in the returned JSON string  

Returns
-------  
";

%feature("docstring") TTTRHeader::set_json "
TTTRHeader::set_json
Set / update the TTTRHeader meta data using a JSON string  

Parameters
----------
* `json_string` :  
";

%feature("docstring") TTTRHeader::get_tag "
TTTRHeader::get_tag
Get a tag / entry from the meta data list in a JSON dict  

Parameters
----------
* `json_data` :  
* `name` :  
* `idx` :  

Returns
-------  
";

%feature("docstring") TTTRHeader::find_tag "
TTTRHeader::find_tag
Find the index of a tag in the JSON data by name type and index  

Parameters
----------
* `json_data` :  
* `name` :  
* `type` :  
* `idx` :  

Returns
-------  
";

%feature("docstring") TTTRHeader::add_tag "
TTTRHeader::add_tag
Add a meta data tag. If the tag already exists the value of the meta data tag is
replaced.  

Parameters
----------
* `json_data` :  
* `name` :  
* `value` :  
* `type` :  
* `idx` :  
";

%feature("docstring") TTTRHeader::read_ptu_header "
TTTRHeader::read_ptu_header
Reads the header of a ptu file and sets the reading routing for  

Parameters
----------
* `fpin` :  
* `rewind` :  
* `tttr_record_type` :  
* `json_data` :  
* `macro_time_resolution` :  
* `micro_time_resolution` :  

Returns
-------
The position of the file pointer at the end of the header  
";

%feature("docstring") TTTRHeader::read_ht3_header "
TTTRHeader::read_ht3_header
Reads the header of a ht3 file and sets the reading routing for  

Parameters
----------
* `fpin` :  
* `rewind` :  
* `tttr_record_type` :  
* `data` :  

Returns
-------
The position of the file pointer at the end of the header  
";

%feature("docstring") TTTRHeader::read_bh132_header "
TTTRHeader::read_bh132_header
Reads the header of a Becker&Hickel SPC132 file and sets the reading routing  

Parameters
----------
* `fpin` :  
* `rewind` :  
* `tttr_record_type` :  
* `data` :  
    JSON dictionary that will contain the header information  
";

%feature("docstring") TTTRHeader::write_spc132_header "
TTTRHeader::write_spc132_header
Write a spc132 header to a file  

WARNING: If the default write mode is \"wb\". Existing files are overwritten.  

Parameters
----------
* `fn` :  
    filename  
* `header` :  
    pointer to the TTTRHeader object that is written to the file  
* `modes` :  
    the writing modes (default 'w+b')  
";

%feature("docstring") TTTRHeader::write_ptu_header "
TTTRHeader::write_ptu_header
Write a PTU header to a file  

WARNING: If the default write mode is \"wb\". Existing files are overwritten.  

Parameters
----------
* `fn` :  
    filename  
* `header` :  
    pointer to the TTTRHeader object that is written to the file  
* `modes` :  
    the writing modes (default 'wb')  
";

%feature("docstring") TTTRHeader::write_ht3_header "
TTTRHeader::write_ht3_header
Write a HT3 header to a file  

WARNING: If the default write mode is \"wb\". Existing files are overwritten.  

Parameters
----------
* `fn` :  
    filename  
* `header` :  
    pointer to the TTTRHeader object that is written to the file  
* `modes` :  
    the writing modes (default 'wb')  
";

// File: class_t_t_t_r_mask.xml


%feature("docstring") TTTRMask "

C++ includes: TTTRMask.h
";

%feature("docstring") TTTRMask::~TTTRMask "
TTTRMask::~TTTRMask";

%feature("docstring") TTTRMask::TTTRMask "
TTTRMask::TTTRMask";

%feature("docstring") TTTRMask::TTTRMask "
TTTRMask::TTTRMask";

%feature("docstring") TTTRMask::size "
TTTRMask::size";

%feature("docstring") TTTRMask::set_mask "
TTTRMask::set_mask";

%feature("docstring") TTTRMask::get_mask "
TTTRMask::get_mask";

%feature("docstring") TTTRMask::set_tttr "
TTTRMask::set_tttr";

%feature("docstring") TTTRMask::select_channels "
TTTRMask::select_channels
Selects a subset of indices by a list of routing channel numbers.  

The returned set of indices will have routing channel numbers that are in the
list of the provided routing channel numbers.  

Parameters
----------
* `tttr` :  
    pointer to TTTR object  
* `routing_channels[int]` :  
    routing channel numbers. A subset of this array will be selected by the
    input.  
* `n_routing_channels[int]` :  
    length of the routing channel number array.  
";

%feature("docstring") TTTRMask::select_count_rate "
TTTRMask::select_count_rate
Selects a subset of indices a count rate of a sliding time-window  

Parameters
----------
* `tttr` :  
    pointer to TTTR object  
* `time_window` :  
    time window size in units of seconds  
* `n_ph_max` :  
    maximum number of photons in time window  
* `invert` :  
    boolean used to invert selection  
";

%feature("docstring") TTTRMask::select_microtime_ranges "
TTTRMask::select_microtime_ranges
Masks outside the provides micro time ranges  

Parameters
----------
* `tttr` :  
* `micro_time_ranges` :  
* `mask` :  
";

%feature("docstring") TTTRMask::get_indices "
TTTRMask::get_indices
Parameters
----------
* `selected` :  
    if selected is true returns selected (unmasked) indices otherwise masked
    indices are returned  

Returns
-------  
";

%feature("docstring") TTTRMask::get_selected_ranges "
TTTRMask::get_selected_ranges";

// File: class_t_t_t_r_range.xml


%feature("docstring") TTTRRange "

C++ includes: TTTRRange.h
";

%feature("docstring") TTTRRange::TTTRRange "
TTTRRange::TTTRRange";

%feature("docstring") TTTRRange::TTTRRange "
TTTRRange::TTTRRange";

%feature("docstring") TTTRRange::TTTRRange "
TTTRRange::TTTRRange
Copy constructor.  
";

%feature("docstring") TTTRRange::size "
TTTRRange::size";

%feature("docstring") TTTRRange::get_tttr_indices "
TTTRRange::get_tttr_indices
A vector containing a set of TTTR indices that was assigned to the range.  
";

%feature("docstring") TTTRRange::get_start "
TTTRRange::get_start
The start index of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_stop "
TTTRRange::get_stop
The stop index of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_start_stop "
TTTRRange::get_start_stop
A vector of the start and the stop TTTR index of the range.  
";

%feature("docstring") TTTRRange::get_stop_time "
TTTRRange::get_stop_time
The stop time of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_start_time "
TTTRRange::get_start_time
The start time of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_start_stop_time "
TTTRRange::get_start_stop_time
A vector of the start and stop time.  
";

%feature("docstring") TTTRRange::get_duration "
TTTRRange::get_duration
The difference between the start and the stop time of a range.  
";

%feature("docstring") TTTRRange::insert "
TTTRRange::insert
Append a index to the TTTR index vector.  
";

%feature("docstring") TTTRRange::clear "
TTTRRange::clear
Clears the TTTR index vector.  
";

%feature("docstring") TTTRRange::strip "
TTTRRange::strip
Strip tttr_indices from a range starting at tttr_indices[offset] the
tttr_indices need to be sorted in ascending size  
";

%feature("docstring") TTTRRange::get_mean_microtime "
TTTRRange::get_mean_microtime
Computes to the mean micro time (in units of the micro channel resolution).  

If there are less then the minimum number of photons in a TTTRRange the function
returns zero.  

Parameters
----------
* `tttr_data[in]` :  
    pointer to a TTTR object  
* `minimum_number_of_photons[in]` :  
    the minimum number of photons in a micro time  
";

%feature("docstring") TTTRRange::get_microtime_histogram "
TTTRRange::get_microtime_histogram";

%feature("docstring") TTTRRange::get_mean_lifetime "
TTTRRange::get_mean_lifetime
Return the average lifetime  

If a TTTRRange has not enough photons return -1  

By default the fluorescence lifetimes are computed in units of the micro time if
no dt is provided.  

Parameters
----------
* `tttr_data[in]` :  
    pointer to a TTTR object  
* `tttr_irf[in]` :  
    pointer to a TTTR object of the IRF  
* `minimum_number_of_photons[in]` :  
    the minimum number of photons in a micro time  
* `m0_irf` :  
    is the zero moment of the IRF (optional, default=1)  
* `m1_irf` :  
    is the first moment of the IRF (optional, default=1)  
* `dt` :  
    time resolution of the micro time  
";

%feature("docstring") TTTRRange::compute_mean_lifetime "
TTTRRange::compute_mean_lifetime
Compute the average lifetime for a set of TTTR indices  

The average lifetimes are computed (not fitted) by the methods of moments (Irvin
Isenberg, 1973, Biophysical journal). This approach does not consider scattered
light.  

If a TTTRRange has not enough photons it is filled with zeros.  

By default the fluorescence lifetimes are computed in units of the micro time if
no dt is provided.  

Parameters
----------
* `tttr_data[in]` :  
    pointer to a TTTR object  
* `tttr_irf[in]` :  
    pointer to a TTTR object of the IRF  
* `minimum_number_of_photons[in]` :  
    the minimum number of photons in a micro time  
* `m0_irf` :  
    is the zero moment of the IRF (optional, default=1)  
* `m1_irf` :  
    is the first moment of the IRF (optional, default=1)  
* `dt` :  
    time resolution of the micro time  
* `background_fraction` :  
    fraction of background pattern in data (if negative no background)  
";

// File: class_t_t_t_r_selection.xml


%feature("docstring") TTTRSelection "

C++ includes: TTTRSelection.h
";

%feature("docstring") TTTRSelection::get_tttr "
TTTRSelection::get_tttr";

%feature("docstring") TTTRSelection::set_tttr "
TTTRSelection::set_tttr";

%feature("docstring") TTTRSelection::TTTRSelection "
TTTRSelection::TTTRSelection";

%feature("docstring") TTTRSelection::TTTRSelection "
TTTRSelection::TTTRSelection
Copy constructor.  
";

%feature("docstring") TTTRSelection::TTTRSelection "
TTTRSelection::TTTRSelection";

// File: namespacestatistics.xml

%feature("docstring") statistics::neyman "
statistics::neyman";

%feature("docstring") statistics::poisson "
statistics::poisson";

%feature("docstring") statistics::pearson "
statistics::pearson";

%feature("docstring") statistics::gauss "
statistics::gauss";

%feature("docstring") statistics::cnp "
statistics::cnp";

%feature("docstring") statistics::sswr "
statistics::sswr
Sum of squared weighted residuals.  
";

%feature("docstring") statistics::chi2_counting "
statistics::chi2_counting
Different chi2 measures for counting data:  

https://arxiv.org/pdf/1903.07185.pdf  

Parameters
----------
* `data` :  
* `model` :  
* `x_min` :  
* `x_max` :  
* `type` :  

Returns
-------  
";

// File: namespacestd.xml

// File: _c_l_s_m_frame_8h.xml

// File: _c_l_s_m_image_8h.xml

%feature("docstring") find_clsm_start_stop "
";

// File: _c_l_s_m_line_8h.xml

// File: _c_l_s_m_pixel_8h.xml

// File: _correlator_8h.xml

// File: _correlator_curve_8h.xml

// File: _correlator_photon_stream_8h.xml

// File: _decay_convolution_8h.xml

%feature("docstring") rescale "

Scale model function to the data (old version)  

This function rescales the model function (fit) to the data by the number of
photons between a start and a stop micro time counting channel. The number of
photons between start and stop are counted and the model function is scaled to
match the data by area.  

This rescaling function does not consider the noise in the data when rescaling
the model.  

Parameters
----------
* `fit[in`, `out]` :  
    model function that is scaled (modified in-place)  
* `decay[in]` :  
    the experimental data to which the model function is scaled  
* `scale[out]` :  
    the scaling parameter (the factor) by which the model function is
    multiplied.  
* `start[in]` :  
    The start micro time channel  
* `stop[in]` :  
    The stop micro time channel  
";

%feature("docstring") rescale_w "

Scale model function to the data (with weights)  

This function rescales the model function (fit) to the data by the number of
photons between a start and a stop micro time counting channel. The number of
photons between start and stop are counted and the model function is scaled to
match the data by area considering the noise of the data.  

The scaling factor is computed by:  

scale = sum(fit*decay/w^2)/sum(fit^2/w^2)  

Parameters
----------
* `fit[in`, `out]` :  
    model function that is scaled (modified in-place)  
* `decay[in]` :  
    the experimental data to which the model function is scaled  
* `w_sq[in]` :  
    squared weights of the data.  
* `scale[out]` :  
    the scaling parameter (the factor) by which the model function is
    multiplied.  
* `start[in]` :  
    The start micro time channel  
* `stop[in]` :  
    The stop micro time channel  
";

%feature("docstring") rescale_w_bg "

Scale model function to the data (with weights and background)  

This function scales the model function (fit) to the data by the number of
photons between a start and a stop micro time counting channel. The number of
photons between start and stop are counted and the model function is scaled to
match the data by area considering the noise of the data and a constant offset
of the data.  

scale = sum(fit*(decay-bg)/w^2)/sum(fit^2/w^2)  

Parameters
----------
* `fit[in`, `out]` :  
    model function that is scaled (modified in-place)  
* `decay[in]` :  
    the experimental data to which the model function is scaled  
* `w_sq[in]` :  
    squared weights of the data.  
* `bg[in]` :  
    constant background of the data  
* `scale[out]` :  
    the scaling parameter (the factor) by which the model function is
    multiplied.  
* `start[in]` :  
    The start micro time channel  
* `stop[in]` :  
    The stop micro time channel  
";

%feature("docstring") fconv "

Convolve lifetime spectrum with instrument response (fast convolution, low
repetition rate)  

This function computes the convolution of a lifetime spectrum (a set of
lifetimes with corresponding amplitudes) with a instrument response function
(irf). This function does not consider periodic excitation and is suited for
experiments at low repetition rate.  

Parameters
----------
* `fit[out]` :  
    model function. The convoluted decay is written to this array  
* `x[in]` :  
    lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)  
* `lamp[in]` :  
    instrument response function  
* `numexp[in]` :  
    number of fluorescence lifetimes  
* `start[in]` :  
    start micro time index for convolution (not used)  
* `stop[in]` :  
    stop micro time index for convolution.  
* `dt[in]` :  
    time difference between two micro time channels  
";

%feature("docstring") fconv_avx "

Convolve lifetime spectrum with instrument response (fast convolution, AVX
optimized for large lifetime spectra)  

This function is a modification of fconv for large lifetime spectra. The
lifetime spectrum is processed by AVX intrinsics. Four lifetimes are convolved
at once. Spectra with lifetimes that are not multiple of four are zero padded.  

Parameters
----------
* `fit` :  
* `x` :  
* `lamp` :  
* `numexp` :  
* `start` :  
* `stop` :  
* `n_points` :  
* `dt` :  
";

%feature("docstring") fconv_per "

Convolve lifetime spectrum with instrument response (fast convolution, high
repetition rate)  

This function computes the convolution of a lifetime spectrum (a set of
lifetimes with corresponding amplitudes) with a instrument response function
(irf). This function does consider periodic excitation and is suited for
experiments at high repetition rate.  

Parameters
----------
* `fit[out]` :  
    model function. The convoluted decay is written to this array  
* `x[in]` :  
    lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)  
* `lamp[in]` :  
    instrument response function  
* `numexp[in]` :  
    number of fluorescence lifetimes  
* `start[in]` :  
    start micro time index for convolution (not used)  
* `stop[in]` :  
    stop micro time index for convolution.  
* `n_points` :  
    number of points in the model function.  
* `period` :  
    excitation period in units of the fluorescence lifetimes (typically
    nanoseconds)  
* `dt[in]` :  
    time difference between two micro time channels  
";

%feature("docstring") fconv_per_avx "

Convolve lifetime spectrum with instrument response (fast convolution, high
repetition rate), AVX optimized version.  

This function computes the convolution of a lifetime spectrum (a set of
lifetimes with corresponding amplitudes) with a instrument response function
(irf). This function does consider periodic excitation and is suited for
experiments at high repetition rate.  

Parameters
----------
* `fit[out]` :  
    model function. The convoluted decay is written to this array  
* `x[in]` :  
    lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)  
* `lamp[in]` :  
    instrument response function  
* `numexp[in]` :  
    number of fluorescence lifetimes  
* `start[in]` :  
    start micro time index for convolution (not used)  
* `stop[in]` :  
    stop micro time index for convolution.  
* `n_points` :  
    number of points in the model function.  
* `period` :  
    excitation period in units of the fluorescence lifetimes (typically
    nanoseconds)  
* `dt[in]` :  
    time difference between two micro time channels  
";

%feature("docstring") fconv_per_cs "

Convolve lifetime spectrum - fast convolution, high repetition rate, with
convolution stop.  

fast convolution, high repetition rate, with convolution stop for Paris  

Parameters
----------
* `fit[out]` :  
    model function. The convoluted decay is written to this array  
* `x[in]` :  
    lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)  
* `lamp[in]` :  
    instrument response function  
* `numexp[in]` :  
    number of fluorescence lifetimes  
* `stop[in]` :  
    stop micro time index for convolution.  
* `n_points` :  
    number of points in the model function.  
* `period` :  
    excitation period in units of the fluorescence lifetimes (typically
    nanoseconds)  
* `conv_stop` :  
    convolution stop micro channel number  
* `dt[in]` :  
    time difference between two micro time channels  
";

%feature("docstring") fconv_ref "

Convolve lifetime spectrum - fast convolution with reference compound decay.  

This function convolves a set of fluorescence lifetimes and with associated
amplitudes with an instrument response function. The provided amplitudes are
scaled prior to the convolution by area using a reference fluorescence lifetime.
The amplitudes are computed by  

amplitude_corrected = a * ( 1 /tauref - 1 / tau)  

where a and tau are provided amplitudes.  

Parameters
----------
* `fit[out]` :  
    model function. The convoluted decay is written to this array  
* `x[in]` :  
    lifetime spectrum (amplitude1, lifetime1, amplitude2, lifetime2, ...)  
* `lamp[in]` :  
    instrument response function  
* `numexp[in]` :  
    number of fluorescence lifetimes  
* `start[in]` :  
    start micro time index for convolution (not used)  
* `stop[in]` :  
    stop micro time index for convolution.  
* `tauref` :  
    a reference lifetime used to rescale the amplitudes of the fluorescence
    lifetime spectrum  
* `dt[in]` :  
    time difference between two micro time channels  
";

%feature("docstring") sconv "

Convolve fluorescence decay curve with irf.  

This function computes a convolved model function for a fluorescence decay
curve.  

Parameters
----------
* `fit` :  
    convolved model function  
* `p` :  
    model function before convolution - fluorescence decay curve  
* `lamp` :  
    instrument response function  
* `start` :  
    start index of the convolution  
* `stop` :  
    stop index of the convolution  
";

%feature("docstring") shift_lamp "

shift instrumnet response function  

Parameters
----------
* `lampsh` :  
* `lamp` :  
* `ts` :  
* `n_points` :  
* `out_value` :  
    the value of the shifted response function outside of the valid indices  
";

%feature("docstring") add_pile_up_to_model "

Add a pile-up distortion to the model function.  

This function adds a pile up distortion to a model fluorescence decay. The model
used to compute the pile-up distortion follows the description of Coates (1968,
eq. 2 and eq. 4)  

Reference: Coates, P.: The correction for photonpile-up in the measurement of
radiative lifetimes. J. Phys. E: Sci. Instrum. 1(8), 878–879 (1968)  

Parameters
----------
* `model[in`, `out]` :  
    The array containing the model function  
* `n_model[in]` :  
    Number of elements in the model array  
* `data[in]` :  
    The array containing the experimental decay  
* `n_data[in]` :  
    number of elements in experimental decay  
* `repetition_rate[in]` :  
    The repetition-rate (excitation rate) in MHz  
* `instrument_dead_time[in]` :  
    The overall dead-time of the detection system in nanoseconds  
* `measurement_time[in]` :  
    The measurement time in seconds  
* `pile_up_model[in]` :  
    The model used to compute the pile up distortion.  
* `start` :  
    Start index for pile up  
* `stop` :  
    Stop index for pile up (default \"coates\")  
";

%feature("docstring") discriminate_small_amplitudes "

Threshold the amplitudes  

Amplitudes with absolute values smaller than the specified threshold are set to
zero.  

Parameters
----------
* `lifetime_spectrum` :  
    interleaved lifetime spectrum (amplitude, lifetime)  
* `n_lifetime_spectrum` :  
    number of elements in lifetime spectrum  
* `amplitude_threshold` :  
";

%feature("docstring") fconv_per_cs_time_axis "

Compute the fluorescence decay for a lifetime spectrum and a instrument response
function considering periodic excitation.  

Fills the pre-allocated output array `output_decay` with a fluorescence
intensity decay defined by a set of fluorescence lifetimes defined by the
parameter `lifetime_handler`. The fluorescence decay will be convolved (non-
periodically) with an instrumental response function that is defined by
`instrument_response_function`.  

This function calculates a fluorescence intensity model_decay that is convolved
with an instrument response function (IRF). The fluorescence intensity
model_decay is specified by its fluorescence lifetime spectrum, i.e., an
interleaved array containing fluorescence lifetimes with corresponding
amplitudes.  

This convolution only works with evenly linear spaced time axes.  

Parameters
----------
* `inplace_output[in`, `out]` :  
    Inplace output array that is filled with the values of the computed
    fluorescence intensity decay model  
* `n_output[in]` :  
    Number of elements in the output array  
* `time_axis[in]` :  
    the time-axis of the model_decay  
* `n_time_axis[in]` :  
    length of the time axis  
* `irf[in]` :  
    the instrument response function array  
* `n_irf[in]` :  
    length of the instrument response function array  
* `lifetime_spectrum[in]` :  
    Interleaved array of amplitudes and fluorescence lifetimes of the form
    (amplitude, lifetime, amplitude, lifetime, ...)  
* `n_lifetime_spectrum[in]` :  
    number of elements in the lifetime spectrum  
* `convolution_start[in]` :  
    Start channel of convolution (position in array of IRF)  
* `convolution_stop[in]` :  
    convolution stop channel (the index on the time-axis)  
* `period` :  
    Period of repetition in units of the lifetime (usually, nano-seconds)  
";

%feature("docstring") fconv_cs_time_axis "

Compute the fluorescence decay for a lifetime spectrum and a instrument response
function.  

Fills the pre-allocated output array `output_decay` with a fluorescence
intensity decay defined by a set of fluorescence lifetimes defined by the
parameter `lifetime_handler`. The fluorescence decay will be convolved (non-
periodically) with an instrumental response function that is defined by
`instrument_response_function`.  

This function calculates a fluorescence intensity model_decay that is convolved
with an instrument response function (IRF). The fluorescence intensity
model_decay is specified by its fluorescence lifetime spectrum, i.e., an
interleaved array containing fluorescence lifetimes with corresponding
amplitudes.  

This convolution works also with uneven spaced time axes.  

Parameters
----------
* `inplace_output[in`, `out]` :  
    Inplace output array that is filled with the values of the computed
    fluorescence intensity decay model  
* `n_output[in]` :  
    Number of elements in the output array  
* `time_axis[in]` :  
    the time-axis of the model_decay  
* `n_time_axis[in]` :  
    length of the time axis  
* `irf[in]` :  
    the instrument response function array  
* `n_irf[in]` :  
    length of the instrument response function array  
* `lifetime_spectrum[in]` :  
    Interleaved array of amplitudes and fluorescence lifetimes of the form
    (amplitude, lifetime, amplitude, lifetime, ...)  
* `n_lifetime_spectrum[in]` :  
    number of elements in the lifetime spectrum  
* `convolution_start[in]` :  
    Start channel of convolution (position in array of IRF)  
* `convolution_stop[in]` :  
    convolution stop channel (the index on the time-axis)  
* `use_amplitude_threshold[in]` :  
    If this value is True (default False) fluorescence lifetimes in the lifetime
    spectrum which have an amplitude with an absolute value of that is smaller
    than `amplitude_threshold` are not omitted in the convolution.  
* `amplitude_threshold[in]` :  
    Threshold value for the amplitudes  
";

// File: _decay_fit_8h.xml

// File: _decay_fit23_8h.xml

// File: _decay_fit24_8h.xml

// File: _decay_fit25_8h.xml

// File: _decay_fit26_8h.xml

// File: _decay_phasor_8h.xml

// File: _decay_statistics_8h.xml

%feature("docstring") statistics::init_fact "

Initialize an array containing pre-computed logratithms  
";

%feature("docstring") statistics::loggammaf "

Approximation of log(gamma function). See wikipedia  

https://en.wikipedia.org/wiki/Gamma_function#The_log-gamma_function  

Parameters
----------
* `t` :  
    input of the gamma function  

Returns
-------
approximation of the logarithm of the gamma function  
";

%feature("docstring") statistics::wcm "

log-likelihood w(C|m) for Cp + 2Cs  

Parameters
----------
* `C` :  
    number of counts in channel  
* `m` :  
    model function  

Returns
-------
log-likelihood w(C|m) for Cp + 2Cs  
";

%feature("docstring") statistics::wcm_p2s "

Compute the -log-likelihood for Cp + 2Cs of a single micro time channel.  

Compute score of model counts in a parallel and perpendicular detection channel
and the experimental counts for a micro time channel.  

This function computes a score for the experimental counts (C) in a channel
where the experimental counts were computed by the sum of the counts in the
parallel (P) and the perpendicular (S) channel by the equation C = P + 2 S.  

This function considers that the number of counts C = P + 2S is not Poissonian.
The score relates to a maximum likelihood function.  

Parameters
----------
* `C` :  
    number of experimental counts (P + 2 S) in a micro time channel  
* `mp` :  
    number of counts of the model in parallel detection channel  
* `ms` :  
    number of counts of the model in the perpendicular detection channel  

Returns
-------  
";

%feature("docstring") statistics::Wcm_p2s "

Compute the overall -log-likelihood for Cp + 2Cs for all micro time channels  

Parameters
----------
* `C` :  
    array of experimental counts in Jordi format  
* `M` :  
    array model function in Jordi format  
* `Nchannels` :  
    number of micro time channels in parallel and perpendicular (half the number
    of elements in C and M).  

Returns
-------
-log-likelihood for Cp + 2Cs for all micro time channels  
";

%feature("docstring") statistics::twoIstar_p2s "

Compute overall 2I* for Cp + 2Cs  

This function computes the overall 2I* for the model function Cp + 2Cs that is
computed by parallel signal (Cp) and the perpendicular signal (Cs). For the
definition of function 2I* see \"An Experimental Comparison of the
Maximum Likelihood Estimation and Nonlinear Least-Squares Fluorescence Lifetime
Analysis of Single Molecules, Michael Maus, Mircea Cotlet, Johan Hofkens,
Thomas Gensch, Frans C. De Schryver, J. Schaffer, and C. A. M. Seidel, Anal.
Chem. 2001, 73, 9, 2078–2086\".  

Parameters
----------
* `C` :  
    array of experimental counts in Jordi format  
* `M` :  
    array model function in Jordi format  
* `Nchannels` :  
    number of micro time channels in parallel and perpendicular (half the number
    of elements in C and M).  

Returns
-------
2I* for Cp + 2Cs  
";

%feature("docstring") statistics::twoIstar "

Compute overall 2I* for Cp & Cs  

This function computes 2I* for Cp and Cs. Cp and Cs are the model signals in the
parallel and perpendicular channel. Contrary to twoIstar_p2s the overall 2I* is
the sum of 2I* for Cp and Cs.  

Parameters
----------
* `C` :  
    array of experimental counts in Jordi format  
* `M` :  
    array model function in Jordi format  
* `Nchannels` :  
    number of micro time channels in parallel and perpendicular (half the number
    of elements in C and M).  

Returns
-------
2I* for Cp & Cs  
";

%feature("docstring") statistics::Wcm "

Compute overall -log-likelihood for Cp & Cs  

Parameters
----------
* `C` :  
    array of experimental counts in Jordi format  
* `M` :  
    array model function in Jordi format  
* `Nchannels` :  
    number of micro time channels in parallel and perpendicular (half the number
    of elements in C and M).  

Returns
-------
-log-likelihood for Cp & Cs  
";

// File: _histogram_8h.xml

%feature("docstring") bincount1D "
";

%feature("docstring") histogram1D "

templateparam
-------------
* `T` :  

Parameters
----------
* `data` :  
* `n_data` :  
* `weights` :  
* `n_weights` :  
* `bin_edges` :  
    contains the edges of the histogram in ascending order (from small to large)  
* `n_bins` :  
    the number of bins in the histogram  
* `hist` :  
* `n_hist` :  
* `axis_type` :  
* `use_weights` :  
    if true the weights specified by  
* `weights` :  
    are used for the calculation of the histogram instead of simply counting the
    frequency.  
";

// File: _histogram_axis_8h.xml

%feature("docstring") linspace "
";

%feature("docstring") logspace "
";

%feature("docstring") search_bin_idx "

Searches for the bin index of a value within a list of bin edges  

If a value is inside the bounds find the bin. The search partitions the
bin_edges in upper and lower ranges and adapts the edge for the upper and lower
range depending if the target value is bigger or smaller than the bin in the
middle.  

templateparam
-------------
* `T` :  

Parameters
----------
* `value` :  
* `bin_edges` :  
* `n_bins` :  

Returns
-------
negative value if the search value is out of the bounds. Otherwise the bin
number is returned.  
";

%feature("docstring") calc_bin_idx "

Calculates for a linear axis the bin index for a particular value.  

templateparam
-------------
* `T` :  

Parameters
----------
* `begin` :  
* `bin_width` :  
* `value` :  

Returns
-------  
";

// File: i__lbfgs_8h.xml

%feature("docstring") fjac1 "
";

%feature("docstring") fgrad1 "
";

%feature("docstring") fjac2 "
";

%feature("docstring") fgrad2 "
";

%feature("docstring") fjac4 "
";

%feature("docstring") fgrad4 "
";

// File: _image_localization_8h.xml

// File: info_8h.xml

// File: _lv_arrays_8h.xml

%feature("docstring") CreateLVI32Array "

Parameters
----------
* `len` :  

Returns
-------  
";

%feature("docstring") CreateLVDoubleArray "

Parameters
----------
* `len` :  

Returns
-------  
";

%feature("docstring") CreateMParam "
";

// File: _pda_8h.xml

// File: _pda_callback_8h.xml

// File: _t_t_t_r_8h.xml

%feature("docstring") selection_by_count_rate "

A count rate (cr) filter that returns an array containing a list of indices
where the cr was smaller than a specified cr.  

The filter is applied to a series of consecutive time events. The time events
are sliced into time windows tw) which have at least a duration as specified by
time_window. The tttr indices of the time windows are written to the output
parameter output. Moreover, for every tw the number of photons is determined. If
in a tw the number of photons exceeds n_ph_max and invert is false (default) the
tw is not written to output. If If in a tw the number of photons is less then
n_ph_max and invert is true the tw is not written to output.  

Parameters
----------
* `selection` :  
    output array  
* `n_selected` :  
    number of elements in output array  
* `time` :  
    array of times  
* `n_time` :  
    number of times  
* `time_window` :  
    length of the time window  
* `n_ph_max` :  
    maximum number of photons in a time window  
* `macro_time_calibration` :  
* `invert` :  
    if invert is true (default false) only indices where the number of photons
    exceeds n_ph_max are selected  
";

%feature("docstring") ranges_by_time_window "

Returns time windows (tw), i.e., the start and the stop indices for a minimum tw
size, a minimum number of photons in a tw.  

Parameters
----------
* `output` :  
    [out] Array containing the interleaved start and stop indices of the tws in
    the TTTR object.  
* `n_output` :  
    [out] Length of the output array  
* `input` :  
    [in] Array containing the macro times  
* `n_input` :  
    [in] Number of macro times  
* `minimum_window_length` :  
    [in] Minimum length of a tw (mandatory).  
* `maximum_window_length` :  
    [in] Maximum length of a tw (optional).  
* `minimum_number_of_photons_in_time_window` :  
    [in] Minimum number of photons a selected tw contains (optional)  
* `maximum_number_of_photons_in_time_window` :  
    [in] Maximum number of photons a selected tw contains (optional)  
* `invert` :  
    [in] If set to true, the selection criteria are inverted.  
";

%feature("docstring") compute_intensity_trace "

Computes a intensity trace for a sequence of time events  

The intensity trace is computed by splitting the trace of time events into time
windows (tws) with a minimum specified length and counts the number of photons
in each tw.  

Parameters
----------
* `output` :  
    number of photons in each time window  
* `n_output` :  
    number of time windows  
* `input` :  
    array of time points  
* `n_input` :  
    number number of time points  
* `time_window` :  
    time window size in units of the macro time resolution  
* `macro_time_resolution` :  
    the resolution of the macro time clock  
";

%feature("docstring") get_array "
";

// File: _t_t_t_r_header_8h.xml

// File: _t_t_t_r_header_types_8h.xml

// File: _t_t_t_r_mask_8h.xml

// File: _t_t_t_r_range_8h.xml

// File: _t_t_t_r_record_reader_8h.xml

%feature("docstring") ProcessSPC130 "
";

%feature("docstring") ProcessSPC600_4096 "
";

%feature("docstring") ProcessSPC600_256 "
";

%feature("docstring") ProcessHHT2v2 "
";

%feature("docstring") ProcessHHT2v1 "
";

%feature("docstring") ProcessHHT3v2 "
";

%feature("docstring") ProcessHHT3v1 "
";

%feature("docstring") ProcessPHT3 "
";

%feature("docstring") ProcessPHT2 "
";

// File: _t_t_t_r_record_types_8h.xml

// File: _t_t_t_r_selection_8h.xml

// File: dir_d44c64559bbebec7f509842c48db8b23.xml

