
// File: index.xml

// File: unionbh__overflow.xml

// File: unionbh__spc130__record.xml

// File: structbh__spc132__header__t.xml


%feature("docstring") bh_spc132_header_t "

Becker&Hickl SPC132 Header.  

C++ includes: header.h
";

// File: unionbh__spc600__256__record.xml

// File: unionbh__spc600__4096__record.xml

// File: class_c_l_s_m_frame.xml


%feature("docstring") CLSMFrame "
";

%feature("docstring") CLSMFrame::get_lines "
";

%feature("docstring") CLSMFrame::get_n_lines "

Get the number of lines per frame in the CLSMImage.  
";

%feature("docstring") CLSMFrame::CLSMFrame "
";

%feature("docstring") CLSMFrame::CLSMFrame "
";

%feature("docstring") CLSMFrame::CLSMFrame "
";

%feature("docstring") CLSMFrame::~CLSMFrame "
";

%feature("docstring") CLSMFrame::append "

Append a line to the current frame  

Parameters
----------
* `line` :  
";

// File: class_c_l_s_m_image.xml


%feature("docstring") CLSMImage "
";

%feature("docstring") CLSMImage::fill_pixels "

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

%feature("docstring") CLSMImage::get_fcs_image "

Computes the  

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

%feature("docstring") CLSMImage::clear_pixels "

Clear tttr_indices stored in the pixels  

Parameters
----------
* `channels` :  
";

%feature("docstring") CLSMImage::get_frames "
";

%feature("docstring") CLSMImage::get_intensity_image "
";

%feature("docstring") CLSMImage::get_fluorescence_decay_image "

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

%feature("docstring") CLSMImage::get_average_decay_of_pixels "

Computes micro time histograms for the stacks of images and a selection of
pixels. Photons in pixels that are selected by the selection array contribute to
the returned array of micro time histograms.  

Parameters
----------
* `tttr_data` :  
    pointer to a TTTR object  
* `selection` :  
    a stack of images used to select pixels  
* `d_selection_1` :  
    number of frames  
* `d_selection_2` :  
    number of lines  
* `d_selection_3` :  
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

%feature("docstring") CLSMImage::get_mean_micro_time_image "

Calculates an image stack where the value of each pixel corresponds to the mean
micro time.  

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

%feature("docstring") CLSMImage::get_mean_lifetime_image "

Computes an image of average lifetimes  

The average lifetimes are computed (not fitted) by the methods of moments (Irvin
Isenberg, 1973, Biophysical journal). This approach does not consider scattered
light.  

Pixels with few photons can be discriminated. Discriminated pixels are filled
with zeros.  

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

%feature("docstring") CLSMImage::get_n_frames "

Get the number of frames in the CLSM image.  
";

%feature("docstring") CLSMImage::get_n_lines "

Get the number of lines per frame in the CLSMImage.  
";

%feature("docstring") CLSMImage::get_n_pixel "

Get the number of pixels per line a frame of the CLSMImage.  
";

%feature("docstring") CLSMImage::copy "

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

Append a frame to the CLSM image.  

Parameters
----------
* `frame` :  
";

%feature("docstring") CLSMImage::CLSMImage "

Copy constructor.  
";

%feature("docstring") CLSMImage::CLSMImage "

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
* `macro_time_shift` :  
    Number of macro time counts a line start is shifted relative to the line
    start marker in the TTTR object (default 0)  
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
";

%feature("docstring") CLSMImage::~CLSMImage "

Destructor.  
";

%feature("docstring") CLSMImage::shift_line_start "

Shift the line starts at least by a specified number of macro time clock counts  

Parameters
----------
* `tttr_data` :  
    pointer [in] to the TTTR object used to construct the CLSM object that is
    shifted  
* `macro_time_shift` :  
    [in] the number of macro time counts that which which the lines are at least
    shifted.  
";

// File: class_c_l_s_m_line.xml


%feature("docstring") CLSMLine "
";

%feature("docstring") CLSMLine::get_pixels "
";

%feature("docstring") CLSMLine::get_pixel_duration "
";

%feature("docstring") CLSMLine::get_n_pixel "

Get the number of pixels per line a frame of the CLSMImage.  
";

%feature("docstring") CLSMLine::CLSMLine "
";

%feature("docstring") CLSMLine::CLSMLine "
";

%feature("docstring") CLSMLine::CLSMLine "
";

%feature("docstring") CLSMLine::CLSMLine "
";

%feature("docstring") CLSMLine::~CLSMLine "
";

%feature("docstring") CLSMLine::append "
";

// File: class_c_l_s_m_pixel.xml


%feature("docstring") CLSMPixel "
";

// File: class_correlator.xml


%feature("docstring") Correlator "
";

%feature("docstring") Correlator::set_time_axis_calibration "
";

%feature("docstring") Correlator::Correlator "

Parameters
----------
* `tttr` :  
    an optional TTTR object. The macro and micro time calibdation of the header
    in the TTTR object calibrate the correlator.  
* `method` :  
    name of correlation method that is used by the correlator  
* `n_bins` :  
    the number of equally spaced correlation bins per block  
* `n_casc` :  
    the number of blocks  
";

%feature("docstring") Correlator::~Correlator "

Destructor.  
";

%feature("docstring") Correlator::set_n_casc "

Sets the number of cascades (also called blocks) of the correlation curve and
updates the correlation axis.  

Parameters
----------
* `n_casc` :  
";

%feature("docstring") Correlator::get_n_casc "

Returns
-------
number of correlation blocks  
";

%feature("docstring") Correlator::set_n_bins "

Parameters
----------
* `v` :  
    the number of equally spaced correaltion channels per block  
";

%feature("docstring") Correlator::get_n_bins "

Returns
-------
the number of equally spaced correlation channels per block  
";

%feature("docstring") Correlator::get_n_corr "

Returns
-------
the overall (total number) of correlation channels  
";

%feature("docstring") Correlator::set_correlation_method "

Set method that to correlate the data  

Parameters
----------
* `cm` :  
    the name of the method  
";

%feature("docstring") Correlator::get_correlation_method "

Returns
-------
name of the used correlation method  
";

%feature("docstring") Correlator::set_microtimes "

Changes the time axis to consider the micro times.  

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
* `n_tac` :  
    The maximum number of TAC channels of the micro times.  
";

%feature("docstring") Correlator::set_macrotimes "

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
* `inplace` :  
    If true (default false) the arrays are modified in place  
";

%feature("docstring") Correlator::set_events "

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
* `inplace` :  
    If true (default false) the arrays are modified in place  
";

%feature("docstring") Correlator::get_x_axis_normalized "

Get the normalized x-axis of the correlation  

Parameters
----------
* `output` :  
    x_axis / time axis of the correlation  
* `n_out` :  
    number of elements in the axis of the x-axis  
";

%feature("docstring") Correlator::set_weights "

Set the weights that are used in the correlation channels  

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
* `inplace` :  
    If true (default false) the arrays are modified in place  
";

%feature("docstring") Correlator::get_corr_normalized "

Get the normalized correlation.  

Parameters
----------
* `output` :  
    an array that containing normalized correlation  
* `n_output` :  
    the number of elements of output  
";

%feature("docstring") Correlator::get_corr "

Get the correlation.  

Parameters
----------
* `corr` :  
    a pointer to an array that will contain the correlation  
* `n_out` :  
    a pointer to the an integer that will contain the number of elements of the
    x-axis  
";

%feature("docstring") Correlator::run "

Compute the correlation function. Usually calling this method is not necessary
the the validity of the correlation function is tracked by the attribute
is_valid.  
";

%feature("docstring") Correlator::set_tttr "

This method sets the time and the weights using TTTR objects.  

The header of the first TTTR object is used for calibration. Both TTTR objects
should have the same calibration (this is not checked).  

Parameters
----------
* `tttr_1` :  
* `tttr_2` :  
* `make_fine` :  
    if true a full correlation is computed that uses the micro time in the TTTR
    objects (default is false).  
* `set_weights` :  
    if set to true (default) the weights for all passed events is set to one.  
";

%feature("docstring") Correlator::set_filter "

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

// File: struct_curve_mapping__t.xml


%feature("docstring") CurveMapping_t "
";

// File: class_decay.xml


%feature("docstring") Decay "
";

%feature("docstring") Decay::compute_microtime_histogram "

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

%feature("docstring") Decay::compute_mean_lifetime "

Compute the mean lifetime by the moments of the decay and the instrument
response function.  

The computed lifetime is the first lifetime determined by the method of moments
(Irvin Isenberg, 1973, Biophysical journal).  

Parameters
----------
* `tttr_data` :  
    TTTR object for which the lifetime is computed  
* `tttr_irf` :  
    TTTR object that is used as IRF  
* `m0_irf` :  
    Number of counts in the IRF (used if no TTTR object for IRF provided.  
* `m1_irf` :  
    First moment of the IRF (used if no TTTR object for IRF provided.  

Returns
-------
The computed lifetime  
";

%feature("docstring") Decay::compute_decay "

Computes a fluorescence decay for a lifetime spectrum.  

The lifetime spectrum is a interleaved array of amplitudes and fluorescence
lifetimes. The fluorescence decay for the lifetime spectrum is computed. The
computed decay is convolved with a instrument response function (IRF). Before
convolution the IRF is corrected for a constant offset. The convolution
considers periodic excitation. The IRF is shifted by a specified value of micro
time channels. After convolution a constant fraction of scattered light is added
to the computed decay and the decay is scaled to the number of photons in an
experimental fluorescence decay. Finally, a constant background is added.
Optionally, pile-up is added to the computed fluorescence decay. Before the
fluorescence decay is computed an amplitude threshold can be applied to the
fluorescence lifetime spectrum to discrimintate fluorescence lifetimes with
small amplitudes.  

Parameters
----------
* `model_function[out]` :  
    The ouput array that will contain the computed fluorescence decay  
* `n_model_function[out]` :  
    The number of points in the model function  
* `data[in]` :  
    The data for which the model function is computed. The data will be used to
    scaled the computed decay.  
* `n_data[in]` :  
    The number of data points  
* `weights[in]` :  
    The weights of the data points. The data weights are used to scale the model
    function to the data (usually the data weights is Poissonian)  
* `n_weights[in]` :  
    The number of weights  
* `time_axis[in]` :  
    The time axiss for which the fluorescence decay is computed for  
* `n_time_axis[in]` :  
    The number of points in the time axis  
* `instrument_response_function[in]` :  
    The instrument response function used for convolution  
* `n_instrument_response_function[in]` :  
    The number of points in the instrument response function  
* `lifetime_spectrum[in]` :  
    The lifetime spectrum. A lifetime specturm is an interleaved array of
    amplitudes and fluorescence lifetimes, .e.g, (amplitude1, lifetime1,
    amplitude2, lifetime2, ...)  
* `n_lifetime_spectrum[in]` :  
    The number of points in the fluorescence lifetime spectrum  
* `start[in]` :  
    The start of the convolution  
* `stop[in]` :  
    The stop of the convoution. The decay will be computed in the range [start,
    stop]  
* `irf_background_counts[in]` :  
    The background counts of the instrument response function. This number will
    be subtracted from the IRF before convolution.  
* `irf_shift_channels[in]` :  
    The number of micro time channels the IRF will be shifted before the
    fluorescence lifetimes are convoluted with the IRF.  
* `scatter_areal_fraction[in]` :  
    The fraction (integrated fraction), i.e., the area the scattered light will
    have in the computed decay.  
* `excitation_period[in]` :  
    The excitation period (in units of the fluorescence lifetime, usually
    nanoseconds) that was used to excite the sample  
* `constant_background` :  
    A constant offset that is added to the fluorescence decay.  
* `total_area[in]` :  
    the area to which the model fluorescence decay is scaled to. If this value
    is negative (default value is -1) the model fluorescence decay is scaled to
    the data in the range defined by the parameters start, stop  
* `use_amplitude_threshold[in]` :  
    if set to true (default is false) a threshold will be applied. If this
    parameter is set to true, fluorescence lifetimes with a amplitude that is
    smaller than a threshold will be not considered.  
* `amplitude_threshold` :  
    The threshold that is used to discriminate fluorescence lifetimes that are
    smaller.  
* `add_pile_up` :  
    if set to true (default is false) pile up will be added to the model
    function.  
";

%feature("docstring") Decay::convolve_lifetime_spectrum "

Compute the fluorescence decay for a lifetime spectrum and a instrument response
function.  

Fills the pre-allocated output array `output_decay` with a fluorescence
intensity decay defined by a set of fluorescence lifetimes defined by the
parameter `lifetime_spectrum`. The fluorescence decay will be convolved (non-
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

%feature("docstring") Decay::convolve_lifetime_spectrum_periodic "

Compute the fluorescence decay for a lifetime spectrum and a instrument response
function considering periodic excitation.  

Fills the pre-allocated output array `output_decay` with a fluorescence
intensity decay defined by a set of fluorescence lifetimes defined by the
parameter `lifetime_spectrum`. The fluorescence decay will be convolved (non-
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
* `use_amplitude_threshold[in]` :  
    If this value is True (default False) fluorescence lifetimes in the lifetime
    spectrum which have an amplitude with an absolute value of that is smaller
    than `amplitude_threshold` are not omitted in the convolution.  
* `amplitude_threshold[in]` :  
    Threshold value for the amplitudes  
* `period` :  
    Period of repetition in units of the lifetime (usually, nano-seconds)  
";

%feature("docstring") Decay::compute_scale "

Compute a scaling factor for a given experimental histogram and model function
and scale (optionally) the model function to the data.  

The scaling factor is computed considering the weighted photon counts in a
specified range.  

Parameters
----------
* `model_function` :  
    array containing the model function  
* `n_model_function` :  
    number of elements in the model function  
* `data` :  
    array of the experimental data  
* `n_data` :  
    number of experimental data points  
* `weights` :  
    the weights of the experimental data  
* `n_weights` :  
    the number of weights of the experimental dat  
* `background` :  
    the background counts of the data  
* `start` :  
    starting index that is used to compute the scaling factor  
* `stop` :  
    stop index to compute the scaling factor  
* `scale_inplace` :  
    if set to true (default is true) the model function is scaled inplace  

Returns
-------
the computed scaling factor  
";

%feature("docstring") Decay::shift_array "

Shift an input array by a floating number.  

Parameters
----------
* `input[in]` :  
    the input array  
* `n_input[in]` :  
    length of the input array  
* `output` :  
    output array  
* `n_output` :  
    length of the output array  
* `shift[in]` :  
    the shift of the output  
";

%feature("docstring") Decay::add_curve "

Computes the sum of two arrays considering their respective areal fraction.  

A weighted sum of two curves is computed. The weighted sum is computed by the
area of the first curve and the areal fraction of the second curve. The area of
the computed curve equals to the area of the first input curve while the areal
fraction of the second input curve will be equal to the specified value in the
range specified by the input parameters.  

Parameters
----------
* `output` :  
    the computed output curve (array)  
* `n_output` :  
    the number of points in the output  
* `curve1[in]` :  
    the first input curve / array  
* `n_curve1[in]` :  
    number of points in the first array  
* `curve2[in]` :  
    second curve / array  
* `n_curve2[in]` :  
    number of points in the second array  
* `areal_fraction_curve2[in]` :  
    areal fraction of the second curve in the output array  
* `start[in]` :  
    start index used for the area calculation  
* `stop[in]` :  
    stop index used for the area calculation  
";

%feature("docstring") Decay::is_valid "
";

%feature("docstring") Decay::set_data "
";

%feature("docstring") Decay::get_data "
";

%feature("docstring") Decay::set_use_amplitude_threshold "
";

%feature("docstring") Decay::get_use_amplitude_threshold "
";

%feature("docstring") Decay::set_amplitude_threshold "
";

%feature("docstring") Decay::get_amplitude_threshold "
";

%feature("docstring") Decay::set_total_area "
";

%feature("docstring") Decay::get_total_area "
";

%feature("docstring") Decay::set_period "
";

%feature("docstring") Decay::get_period "
";

%feature("docstring") Decay::set_irf_shift_channels "
";

%feature("docstring") Decay::get_irf_shift_channels "
";

%feature("docstring") Decay::set_areal_scatter_fraction "
";

%feature("docstring") Decay::get_areal_scatter_fraction "
";

%feature("docstring") Decay::set_constant_background "
";

%feature("docstring") Decay::get_constant_background "
";

%feature("docstring") Decay::set_convolution_start "
";

%feature("docstring") Decay::get_convolution_start "
";

%feature("docstring") Decay::set_convolution_stop "
";

%feature("docstring") Decay::get_convolution_stop "
";

%feature("docstring") Decay::set_correct_pile_up "
";

%feature("docstring") Decay::get_correct_pile_up "
";

%feature("docstring") Decay::set_irf "
";

%feature("docstring") Decay::get_irf "
";

%feature("docstring") Decay::get_model "
";

%feature("docstring") Decay::set_lifetime_spectrum "
";

%feature("docstring") Decay::get_lifetime_spectrum "
";

%feature("docstring") Decay::set_weights "
";

%feature("docstring") Decay::get_weights "
";

%feature("docstring") Decay::set_time_axis "
";

%feature("docstring") Decay::get_time_axis "
";

%feature("docstring") Decay::set_irf_background_counts "
";

%feature("docstring") Decay::get_irf_background_counts "
";

%feature("docstring") Decay::Decay "

Parameters
----------
* `tttr_data` :  
    pointer to TTTR object that is used to construct a decay histogram  
* `micro_time_coarsening` :  
    an (optional) integer by which the micro times are divided to coarsen the
    time axis (default is 1)  
* `decay_data` :  
    the data to which the decay is fitted  
* `time_axis` :  
    the time axis that belongs to the data  
* `dt` :  
    the spacing between the points in the time axis. This optional parameter is
    used to compute a time axis if not time axis was provided by the parameter
    time_axis  
* `weights` :  
    the weights of the data points. If the weights are not provided (nullptr /
    None) the weights are computed assuming Poisson noise.  
* `instrument_response_function` :  
    The instrument response function (IRF) that is used for convolution. If no
    IRF is provided  
* `start` :  
    The start index in the IRF used for convolution. Points in the IRF before
    the start index are not used for convolution.  
* `stop` :  
    The stop index in the IRF used for convolution. Points beyond the stop index
    are not convolved.  
* `use_amplitude_threshold` :  
    If this is set to true (default value is true) the values that are smaller
    then a specified threshold are omitted  
* `amplitude_threshold` :  
    The amplitude threshold that is used if the parameter
    use_amplitude_threshold is set to true (the default value is 1e10)  
* `correct_pile_up` :  
    If this is set to true (the default value is false) the convolved model
    function is 'piled up' to match pile up artifacts in the data.  
* `excitation_period` :  
    the repetition period, .i.e, the time between subsequent excitation pulses.  
";

%feature("docstring") Decay::add_pile_up "

Correct the model function for pile up  

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
    The repetition-rate in MHz  
* `dead_time[in]` :  
    The dead-time of the detection system in nanoseconds  
* `measurement_time[in]` :  
    The measurement time in seconds  
";

%feature("docstring") Decay::get_weighted_residuals "
";

%feature("docstring") Decay::evaluate "
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

%feature("docstring") Header::get_effective_number_of_micro_time_channels "

The number of micro time channels that fit between two macro times.  

The total (possible) number of TAC channels can exceed the number that fit
between two macro time channels. This function returns the effective number,
i.e., the number of micro time channels between two macro times. The micro time
channels that are outside of this bound should (usually) not be filled.  

Returns
-------
effective_tac_channels (that fit between to macro times)  
";

%feature("docstring") Header::getTTTRRecordType "

Returns
-------
The TTTR container type of the associated TTTR file as a char  
";

%feature("docstring") Header::Header "

Default constructor  
";

%feature("docstring") Header::Header "

Copy constructor.  
";

%feature("docstring") Header::Header "
";

%feature("docstring") Header::~Header "
";

// File: class_histogram.xml


%feature("docstring") Histogram "
";

%feature("docstring") Histogram::getAxisDimensions "
";

%feature("docstring") Histogram::update "
";

%feature("docstring") Histogram::getHistogram "
";

%feature("docstring") Histogram::setAxis "
";

%feature("docstring") Histogram::setAxis "
";

%feature("docstring") Histogram::getAxis "
";

%feature("docstring") Histogram::Histogram "
";

%feature("docstring") Histogram::~Histogram "
";

// File: class_histogram_axis.xml


%feature("docstring") HistogramAxis "
";

%feature("docstring") HistogramAxis::update "

Recalculates the bin edges of the axis  
";

%feature("docstring") HistogramAxis::setAxisType "
";

%feature("docstring") HistogramAxis::getNumberOfBins "
";

%feature("docstring") HistogramAxis::getBinIdx "
";

%feature("docstring") HistogramAxis::getBins "
";

%feature("docstring") HistogramAxis::getBins "
";

%feature("docstring") HistogramAxis::getName "
";

%feature("docstring") HistogramAxis::setName "
";

%feature("docstring") HistogramAxis::HistogramAxis "
";

%feature("docstring") HistogramAxis::HistogramAxis "
";

// File: struct_param_struct__t.xml


%feature("docstring") ParamStruct_t "
";

// File: class_pda.xml


%feature("docstring") Pda "
";

%feature("docstring") Pda::evaluate "

Computes the S1S2 histogram.  
";

%feature("docstring") Pda::Pda "

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
";

%feature("docstring") Pda::append "

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

Clears the model and removes all species.  
";

%feature("docstring") Pda::get_amplitudes "

Returns the amplitudes of the species  

Parameters
----------
* `output[out]` :  
    A C type array containing the amplitude of the species  
* `n_output[out]` :  
    The number of species  
";

%feature("docstring") Pda::set_amplitudes "

Sets the amplitudes of the species.  

Parameters
----------
* `input[in]` :  
    A C type array that contains the amplitude of the species  
* `n_input[in]` :  
    The number of species  
";

%feature("docstring") Pda::set_callback "

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

Returns the amplitudes of the species  

Parameters
----------
* `output[out]` :  
    A C type array containing the amplitude of the species  
* `n_output[out]` :  
    The number of species  
";

%feature("docstring") Pda::set_probabilities_ch1 "

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

Set the probability P(F)  

Parameters
----------
* `input[in]` :  
* `n_input[in]` :  
";

%feature("docstring") Pda::getPF "

Set the probability P(F)  
";

%feature("docstring") Pda::get_1dhistogram "

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

The maximum number of photons in the SgSr matrix.  
";

%feature("docstring") Pda::set_max_number_of_photons "

Set the maximum number of photons in the S1S2 matrix  

Note: the size of the pF array must agree with the maximum number of photons!  

Parameters
----------
* `nmax[in]` :  
    the maximum number of photons  
";

%feature("docstring") Pda::get_min_number_of_photons "

The minimum number of photons in the SgSr matrix.  
";

%feature("docstring") Pda::set_min_number_of_photons "

Set the minimum number of photons in the SgSr matrix.  
";

%feature("docstring") Pda::get_ch1_background "

Get the background in the green channel.  
";

%feature("docstring") Pda::set_ch1_background "

Set the background in the green channel.  
";

%feature("docstring") Pda::get_ch2_background "

Get the background in the red channel.  
";

%feature("docstring") Pda::set_ch2_background "

Set the background in the red channel.  
";

%feature("docstring") Pda::is_valid_sgsr "

Returns true if the SgSr histogram is valid, i.e., if output is correct for the
input parameter. This value is set to true by evaluate.  
";

%feature("docstring") Pda::set_valid_sgsr "

Set the SgSr histogram to valid (only used for testing)  
";

%feature("docstring") Pda::compute_experimental_histograms "

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
";

%feature("docstring") PdaCallback::run "
";

%feature("docstring") PdaCallback::PdaCallback "
";

%feature("docstring") PdaCallback::~PdaCallback "
";

// File: unionph__ph__t2__record.xml

// File: unionpq__hh__t2__record.xml

// File: unionpq__hh__t3__record.xml

// File: structpq__ht3__ascii__t.xml


%feature("docstring") pq_ht3_ascii_t "

The following represents the readable ASCII file header portion.  

C++ includes: header.h
";

// File: structpq__ht3___bin_hdr__t.xml


%feature("docstring") pq_ht3_BinHdr_t "

The following is binary file header information.  

C++ includes: header.h
";

// File: structpq__ht3__board__settings__t.xml


%feature("docstring") pq_ht3_board_settings_t "
";

// File: structpq__ht3___board_hdr.xml


%feature("docstring") pq_ht3_BoardHdr "
";

// File: structpq__ht3___t_t_t_r_hdr.xml


%feature("docstring") pq_ht3_TTTRHdr "
";

// File: unionpq__ph__t3__record.xml

// File: structtag__head.xml


%feature("docstring") tag_head "

A Header Tag entry of a PTU file.  

C++ includes: header.h
";

// File: class_t_t_t_r.xml


%feature("docstring") TTTR "
";

%feature("docstring") TTTR::append "
";

%feature("docstring") TTTR::get_used_routing_channels "

Returns an array containing the routing channel numbers that are contained
(used) in the TTTR file.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_macro_time "

Returns an array containing the macro times of the valid TTTR events.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_micro_time "

Returns an array containing the micro times of the valid TTTR events.  

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::intensity_trace "

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

Parameters
----------
* `output` :  
    Pointer to the output array  
* `n_output` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_number_of_micro_time_channels "

Returns the number of micro time channels that fit between two macro time
clocks.  

Returns
-------
maximum valid number of micro time channels  
";

%feature("docstring") TTTR::get_n_valid_events "

Returns
-------
number of valid events in the TTTR file  
";

%feature("docstring") TTTR::get_tttr_container_type "

Returns
-------
the container type that was used to open the file  
";

%feature("docstring") TTTR::select "
";

%feature("docstring") TTTR::TTTR "

Constructor  

Parameters
----------
* `filename` :  
    is the filename of the TTTR file.  
* `container_type` :  
    specifies the file type. parent->children.push_back()  

PQ_PTU_CONTAINER 0 PQ_HT3_CONTAINER 1 BH_SPC130_CONTAINER 2
BH_SPC600_256_CONTAINER 3 BH_SPC600_4096_CONTAINER 4  
";

%feature("docstring") TTTR::TTTR "

Copy constructor.  
";

%feature("docstring") TTTR::TTTR "

Parameters
----------
* `filename` :  
    TTTR filename  
* `container_type` :  
    container type as int (0 = PTU; 1 = HT3; 2 = SPC-130; 3 = SPC-600_256; 4 =
    SPC-600_4096; 5 = PHOTON-HDF5)  
* `read_input` :  
    if true reads the content of the file  
";

%feature("docstring") TTTR::TTTR "

Parameters
----------
* `filename` :  
    TTTR filename  
* `container_type` :  
    container type as int (0 = PTU; 1 = HT3; 2 = SPC-130; 3 = SPC-600_256; 4 =
    SPC-600_4096; 5 = PHOTON-HDF5)  
";

%feature("docstring") TTTR::TTTR "

Parameters
----------
* `filename` :  
    TTTR filename  
* `container_type` :  
    container type as string (PTU; HT3; SPC-130; SPC-600_256; SPC-600_4096;
    PHOTON-HDF5)  
";

%feature("docstring") TTTR::TTTR "

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

Destructor.  
";

%feature("docstring") TTTR::get_filename "

getFilename Getter for the filename of the TTTR file  

Returns
-------
The filename of the TTTR file  
";

%feature("docstring") TTTR::get_selection_by_channel "

Returns a vector containing indices of records that  

Parameters
----------
* `input` :  
    channel number array used to select indices of photons  
* `n_input` :  
    the length of the channel number list.  
";

%feature("docstring") TTTR::get_selection_by_count_rate "

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

%feature("docstring") TTTR::get_time_window_ranges "

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
    Minimum number of photons a selected tw contains (optional) in units of ms  
* `maximum_number_of_photons_in_time_window[in]` :  
    Maximum number of photons a selected tw contains (optional)  
* `invert[in]` :  
    If set to true, the selection criteria are inverted.  
";

%feature("docstring") TTTR::get_header "

Get header returns the header (if present) as a map of strings.  
";

%feature("docstring") TTTR::get_n_events "

Returns the number of events in the TTTR file for cases no selection is
specified otherwise the number of selected events is returned.  

Returns
-------  
";

%feature("docstring") TTTR::write "

Write the contents of a opened TTTR file to a new TTTR file.  

Parameters
----------
* `fn` :  
    filename  
* `container_type` :  
    container type (PTU; HT3; SPC-130; SPC-600_256; SPC-600_4096; PHOTON-HDF5)  

Returns
-------  
";

%feature("docstring") TTTR::shift_macro_time "

Shift the macro time by a constant  

Parameters
----------
* `shift` :  
";

// File: class_t_t_t_r_range.xml


%feature("docstring") TTTRRange "
";

%feature("docstring") TTTRRange::TTTRRange "

Parameters
----------
* `start` :  
    start index of the TTTRRange  
* `stop` :  
    stop index of the TTTRRange  
* `start_time` :  
    start time of the TTTRRange  
* `stop_time` :  
    stop time of the TTTRRange  
* `pre_reserve` :  
    is the number of tttr indices that is pre-allocated in in memory upon
    creation of a TTTRRange object.  
";

%feature("docstring") TTTRRange::TTTRRange "

Copy constructor.  
";

%feature("docstring") TTTRRange::get_tttr_indices "

A vector containing a set of TTTR indices that was assigned to the range.  
";

%feature("docstring") TTTRRange::get_start_stop "

A vector of the start and the stop TTTR index of the range.  
";

%feature("docstring") TTTRRange::get_start_stop_time "

A vector of the start and stop time.  
";

%feature("docstring") TTTRRange::get_duration "

The difference between the start and the stop time of a range.  
";

%feature("docstring") TTTRRange::set_start "

The start index of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_start "

The start index of the TTTR range object.  
";

%feature("docstring") TTTRRange::set_stop "

The stop index of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_stop "

The stop index of the TTTR range object.  
";

%feature("docstring") TTTRRange::set_stop_time "

The stop time of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_stop_time "

The stop time of the TTTR range object.  
";

%feature("docstring") TTTRRange::set_start_time "

The start time of the TTTR range object.  
";

%feature("docstring") TTTRRange::get_start_time "

The start time of the TTTR range object.  
";

%feature("docstring") TTTRRange::append "

Append a index to the TTTR index vector.  
";

%feature("docstring") TTTRRange::clear "

Clears the TTTR index vector.  
";

%feature("docstring") TTTRRange::shift_start_time "
";

%feature("docstring") TTTRRange::update "

Update start, stop and the start and stop using the tttr_indices attribute  

Parameters
----------
* `tttr_data` :  
    [in] the TTTR dataset that is used to determine the start and stop time by
    the TTTR macro time.  
* `from_tttr_indices` :  
    [in] if set to true (default is true) the start stop indices and the start
    stop time are updated from the tttr_indices attribute. Otherwise, the start
    stop times are updated from the tttr object using the current start stop  
";

// File: correlation_8h.xml

// File: decay_8h.xml

// File: header_8h.xml

%feature("docstring") read_ptu_header "

Reads the header of a ptu file and sets the reading routing for  

Parameters
----------
* `fpin` :  
* `rewind` :  
* `tttr_record_type` :  
* `data` :  
* `macro_time_resolution` :  
* `micro_time_resolution` :  

Returns
-------
The position of the file pointer at the end of the header  
";

%feature("docstring") read_ht3_header "

Reads the header of a ht3 file and sets the reading routing for  

Parameters
----------
* `fpin` :  
* `rewind` :  
* `tttr_record_type` :  
* `data` :  
* `macro_time_resolution` :  
* `micro_time_resolution` :  

Returns
-------
The position of the file pointer at the end of the header  
";

%feature("docstring") read_bh132_header "

Reads the header of a Becker&Hickel SPC132 file and sets the reading routing for  

Parameters
----------
* `fpin` :  
* `rewind` :  
* `tttr_record_type` :  
* `data` :  
* `macro_time_resolution` :  
* `micro_time_resolution` :  
";

// File: histogram_8h.xml

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

%feature("docstring") linspace "
";

%feature("docstring") logspace "
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

%feature("docstring") bincount1D "
";

// File: image_8h.xml

// File: pda_8h.xml

// File: record__reader_8h.xml

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

// File: record__types_8h.xml

// File: tttr_8h.xml

%feature("docstring") determine_number_of_records_by_file_size "

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

%feature("docstring") selection_by_count_rate "

A count rate (cr) filter that returns an array containing a list of indices
where the cr was smaller than a specified cr.  

The filter is applied to a series of consecutive time events. The time events
are sliced into time windows (tw) which have at least a duration as specified by
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

%feature("docstring") get_ranges_channel "

Parameters
----------
* `ranges` :  
* `n_range` :  
* `time` :  
* `n_time` :  
* `channel` :  
";

%feature("docstring") selection_by_channels "

Selects a subset of indices by a list of routing channel numbers.  

The retuned set of indices will have routing channel numbers that are in the
list of the provided routing channel numbers.  

Parameters
----------
* `output[out]` :  
    output array that will contain the selected indices  
* `n_output[out]` :  
    the length of the output array  
* `input[int]` :  
    routing channel numbers defining the returned subset of indices  
* `n_input[int]` :  
    the length of the input array  
* `routing_channels[int]` :  
    array of routing channel numbers. A subset of this array will be selected by
    the input.  
* `n_routing_channels[int]` :  
    the length of the routing channel number array.  
";

%feature("docstring") get_array "
";

// File: dir_d44c64559bbebec7f509842c48db8b23.xml

