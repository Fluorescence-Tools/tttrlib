
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

%feature("docstring") CLSMFrame::CLSMFrame "
";

%feature("docstring") CLSMFrame::CLSMFrame "
";

%feature("docstring") CLSMFrame::CLSMFrame "
";

%feature("docstring") CLSMFrame::~CLSMFrame "
";

%feature("docstring") CLSMFrame::append "
";

// File: class_c_l_s_m_image.xml


%feature("docstring") CLSMImage "
";

%feature("docstring") CLSMImage::fill_pixels "

Fill the tttr_indices of the pixels with the indices of the channels that are
within a pixel  

Parameters
----------
* `channels` :  
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

%feature("docstring") CLSMImage::get_decay_image "

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
* `tac_coarsening` :  
    constant used to coarsen the micro times  
* `stack_frames` :  
    if True the frames are stacked.  
";

%feature("docstring") CLSMImage::get_decays "

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

%feature("docstring") CLSMImage::get_mean_tac_image "

Calculates an image stack where the value of each pixel corresponds to the mean
micro times per pixel discriminating micro time channels with few counts  

Parameters
----------
* `tttr_data` :  
    pointer to a TTTR object  
* `out` :  
    pointer to output array that will contain the image stack  
* `dim1` :  
    returns the number of frames  
* `dim2` :  
    returns the number of lines  
* `dim3` :  
    returns the number of pixels per line  
* `n_ph_min` :  
    the minimum number of photons in a micro time  
* `stack_frames` :  
    if true the frames are stacked and a single the frame containing the photon
    count weighted average arrival time is returned  
";

%feature("docstring") CLSMImage::append "

Append a frame to the CLSM image.  

Parameters
----------
* `frame` :  
";

%feature("docstring") CLSMImage::CLSMImage "
";

%feature("docstring") CLSMImage::CLSMImage "
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
";

%feature("docstring") CLSMImage::~CLSMImage "
";

// File: class_c_l_s_m_line.xml


%feature("docstring") CLSMLine "
";

%feature("docstring") CLSMLine::get_pixels "
";

%feature("docstring") CLSMLine::get_pixel_duration "
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

%feature("docstring") CLSMPixel::get_tttr_indices "
";

%feature("docstring") CLSMPixel::append "
";

%feature("docstring") CLSMPixel::CLSMPixel "
";

%feature("docstring") CLSMPixel::CLSMPixel "
";

%feature("docstring") CLSMPixel::~CLSMPixel "
";

// File: classsetup_1_1_c_make_build.xml


%feature("docstring") setup::CMakeBuild "
";

%feature("docstring") setup::CMakeBuild::run "
";

%feature("docstring") setup::CMakeBuild::build_extension "
";

// File: classsetup_1_1_c_make_extension.xml


%feature("docstring") setup::CMakeExtension "
";

%feature("docstring") setup::CMakeExtension::__init__ "
";

// File: class_correlator.xml


%feature("docstring") Correlator "
";

%feature("docstring") Correlator::Correlator "
";

%feature("docstring") Correlator::~Correlator "
";

%feature("docstring") Correlator::set_n_casc "

Sets the number of cascades of the correlation curve and updates the correlation
axis.  

Parameters
----------
* `n_casc` :  
";

%feature("docstring") Correlator::get_n_casc "

Returns
-------  
";

%feature("docstring") Correlator::set_n_bins "
";

%feature("docstring") Correlator::get_n_bins "
";

%feature("docstring") Correlator::get_n_corr "
";

%feature("docstring") Correlator::make_fine "

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

%feature("docstring") Correlator::get_x_axis "

Get the x-axis of the correlation  

Parameters
----------
* `x_axis` :  
    a pointer to an array that will contain the x-axis  
* `n_out` :  
    a pointer to the an integer that will contain the number of elements of the
    x-axis  
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

%feature("docstring") Correlator::get_x_axis_normalized "

Get the normalized x-axis of the correlation  

Parameters
----------
* `x_axis` :  
    a pointer to an array that will contain the x-axis  
* `n_out` :  
    a pointer to the an integer that will contain the number of elements of the
    x-axis  
";

%feature("docstring") Correlator::get_corr_normalized "

Get the normalized correlation.  

Parameters
----------
* `corr` :  
    a pointer to an array that will contain the normalized correlation  
* `n_out` :  
    a pointer to the an integer that will contain the number of elements of the
    normalized x-axis  
";

%feature("docstring") Correlator::set_events "

Parameters
----------
* `Array` :  
    t1 of the time events of the first channel (the array is modified in place)  
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

%feature("docstring") Correlator::normalize "

Calculates the normalized correlation amplitudes and x-axis  

Makes a copy of the current correlation curve, i.e., the x-axis and and the
corresponding correlation amplitudes and calculates the values of the normalized
correlation.  
";

%feature("docstring") Correlator::run "
";

// File: struct_curve_mapping__t.xml


%feature("docstring") CurveMapping_t "
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

%feature("docstring") Header::getTTTRRecordType "

Returns
-------
The TTTR container type of the associated TTTR file as a char  
";

%feature("docstring") Header::Header "

Default constructor  
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

%feature("docstring") Histogram::setWeights "
";

%feature("docstring") Histogram::setData "
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

%feature("docstring") Pda::~Pda "
";

%feature("docstring") Pda::append "
";

%feature("docstring") Pda::set_probability_green "
";

%feature("docstring") Pda::clear_probability_green "
";

%feature("docstring") Pda::get_amplitudes "
";

%feature("docstring") Pda::get_probability_green "
";

%feature("docstring") Pda::get_max_number_of_photons "
";

%feature("docstring") Pda::set_max_number_of_photons "
";

%feature("docstring") Pda::get_green_background "
";

%feature("docstring") Pda::set_green_background "
";

%feature("docstring") Pda::get_red_background "
";

%feature("docstring") Pda::set_red_background "
";

%feature("docstring") Pda::setPF "
";

%feature("docstring") Pda::get_SgSr_matrix "
";

%feature("docstring") Pda::evaluate "
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

%feature("docstring") TTTR::get_used_routing_channels "

Returns an array containing the routing channel numbers that are contained
(used) in the TTTR file.  

Parameters
----------
* `out` :  
    Pointer to the output array  
* `n_out` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_macro_time "

Returns an array containing the macro times of the valid TTTR events.  

Parameters
----------
* `out` :  
    Pointer to the output array  
* `n_out` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_micro_time "

Returns an array containing the micro times of the valid TTTR events.  

Parameters
----------
* `out` :  
    Pointer to the output array  
* `n_out` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_routing_channel "

Returns an array containing the routing channel numbers of the valid TTTR
events.  

Parameters
----------
* `out` :  
    Pointer to the output array  
* `n_out` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_event_type "

Parameters
----------
* `out` :  
    Pointer to the output array  
* `n_out` :  
    Pointer to the number of elements in the output array  
";

%feature("docstring") TTTR::get_number_of_tac_channels "

Returns
-------
maximum valid number of micro time channels  
";

%feature("docstring") TTTR::get_n_valid_events "

Returns
-------
number of valid events in the TTTR file  
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
";

%feature("docstring") TTTR::TTTR "
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
* `in` :  
    a pointer to an array of int16_tchannel numbers that are used to select
    indices of photons  
* `n_in` :  
    the length of the channel list.  
";

%feature("docstring") TTTR::get_selection_by_count_rate "

Parameters
----------
* `out` :  
* `n_out` :  
* `tw` :  
* `n_ph_max` :  
";

%feature("docstring") TTTR::get_ranges_by_count_rate "
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

%feature("docstring") TTTR::write_file "

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
";

%feature("docstring") TTTRRange::~TTTRRange "
";

%feature("docstring") TTTRRange::get_tttr_indices "
";

%feature("docstring") TTTRRange::get_start_stop "
";

%feature("docstring") TTTRRange::get_start_stop_time "
";

%feature("docstring") TTTRRange::get_duration "
";

// File: namespace_pda_functions.xml

%feature("docstring") PdaFunctions::sgsr_pN "

calculating p(G,R) according to Matthew  

Parameters
----------
* `SgSr` :  
    SgSr(i,j) = p(Sg=i, Sr=j)  
* `pN` :  
    p(N)  
* `Nmax` :  
    max number of photons (max N)  
* `Bg` :  
    <background green>=\"\">, per time window (!)  
* `Br` :  
    <background red>=\"\">, -\"-  
* `pg_theor` :  
";

%feature("docstring") PdaFunctions::sgsr_pF "

calculating p(G,R), one ratio, one P(F)  

Parameters
----------
* `SgSr` :  
    sgsr_pN  
* `pF` :  
    input p(F)  
* `Nmax` :  
* `Bg` :  
* `Br` :  
* `pg_theor` :  
";

%feature("docstring") PdaFunctions::sgsr_pN_manypg "

calculating p(G,R), several ratios, same P(N)  

Parameters
----------
* `SgSr` :  
    see sgsr_pN  
* `pN` :  
    input: p(N)  
* `Nmax` :  
* `Bg` :  
* `Br` :  
* `N_pg` :  
    size of pg_theor  
* `pg_theor` :  
* `a` :  
    corresponding amplitudes  
";

%feature("docstring") PdaFunctions::sgsr_pF_manypg "

calculating p(G,R), several ratios, same P(F)  

Parameters
----------
* `SgSr` :  
    see sgsr_pN  
* `pF` :  
    input: p(F)  
* `Nmax` :  
* `Bg` :  
* `Br` :  
* `N_pg` :  
    size of pg_theor  
* `pg_theor` :  
* `a` :  
    corresponding amplitudes  
";

%feature("docstring") PdaFunctions::sgsr_manypF "
";

%feature("docstring") PdaFunctions::conv_pF "

Parameters
----------
* `SgSr` :  
* `FgFr` :  
* `Nmax` :  
* `Bg` :  
* `Br` :  
";

%feature("docstring") PdaFunctions::conv_pN "

Parameters
----------
* `SgSr` :  
* `FgFr` :  
* `Nmax` :  
* `Bg` :  
* `Br` :  
* `pN` :  
";

%feature("docstring") PdaFunctions::poisson_0toN "

generates Poisson distribution witn average= lambda, for 0..N  

Parameters
----------
* `return_p` :  
* `lambda` :  
* `return_dim` :  
";

%feature("docstring") PdaFunctions::poisson_0toN_multi "

generates Poisson distribution for a set of lambdas  
";

%feature("docstring") PdaFunctions::polynom2_conv "

convolves vectors p and [p2 1-p2]  
";

// File: namespacesetup.xml

// File: correlate_8h.xml

%feature("docstring") normalize_correlation "

Normalizes a correlation curve.  

This normalization applied to correlation curves that were calculated using a
linear/logrithmic binning as described in  

*   Fast calculation of fluorescence correlation data with asynchronous time-
    correlated single-photon counting, Michael Wahl, Ingo Gregor, Matthias
    Patting, Joerg Enderlein, Optics Express Vol. 11, No. 26, p. 3383  

Parameters
----------
* `np1` :  
    The sum of the weights in the first correlation channel  
* `dt1` :  
    The time difference between the first event and the last event in the first
    correlation channel  
* `np2` :  
    The sum of the weights in the second correlation channel  
* `dt2` :  
    The time difference between the first event and the last event in the second
    correlation channel  
* `x_axis` :  
    The x-axis of the correlation  
* `corr` :  
    The array that contains the original correlation that is modified in place.  
* `n_bins` :  
    The number of bins per cascade of the correlation  
";

%feature("docstring") make_fine_times "

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
    An array containing the micro times of the corresponding macro times  
* `tac` :  
    The number of micro time channels (TAC channels)  
";

%feature("docstring") correlate "

Calculates the cross-correlation between two arrays containing time events.  

Cross-correlates two weighted arrays of events using an approach that utilizes a
linear spacing of the bins within a cascade and a logarithmic spacing of the
cascades. The function works inplace on the input times, i.e, during the
correlation the values of the input times and weights are changed to coarsen the
times and weights for every correlation cascade.  

The start position parameters  

Parameters
----------
* `start_1` :  
    and  
* `start_2` :  
    and the end position parameters  
* `end_1` :  
    and  
* `end_1` :  
    define which part of the time array of the first and second correlation
    channel are used for the correlation analysis.  

The correlation algorithm combines approaches of the following papers:  

*   Fast calculation of fluorescence correlation data with asynchronous time-
    correlated single-photon counting, Michael Wahl, Ingo Gregor, Matthias
    Patting, Joerg Enderlein, Optics Express Vol. 11, No. 26, p. 3383  
*   Fast, flexible algorithm for calculating photon correlations, Ted A.
    Laurence, Samantha Fore, Thomas Huser, Optics Express Vol. 31 No. 6, p.829  

Parameters
----------
* `start_1` :  
    The start position on the time event array of the first channel.  
* `end_1` :  
    The end position on the time event array of the first channel.  
* `start_2` :  
    The start position on the time event array of the second channel.  
* `end_2` :  
    The end position on the time event array of the second channel.  
* `i_casc` :  
    The number of the current cascade  
* `n_bins` :  
    The number of bins per cascase  
* `taus` :  
    A vector containing the correlation times of all cascades  
* `corr` :  
    A vector to that the correlation is written by the function  
* `t1` :  
    A vector of the time events of the first channel  
* `w1` :  
    A vector of weights for the time events of the first channel  
* `nt1` :  
    The number of time events in the first channel  
* `t2` :  
    A vector of the time events of the second channel  
* `w2` :  
    A vector of weights for the time events of the second channel  
* `nt2` :  
    The number of time events in the second channel  
";

%feature("docstring") coarsen "

Coarsens the time events  

This function coarsens a set of time events by dividing the times by two. In
case two consecutive time events in the array have the same time, the weights of
the two events are added to the following weight element and the value of the
previous weight is set to zero.  

Parameters
----------
* `t` :  
    A vector of the time events of the first channel  
* `w` :  
    A vector of weights for the time events of the first channel  
* `nt` :  
    The number of time events in the first channel  
";

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

The filter is applied to a series of consecutive time events specified by the C
type array  

Parameters
----------
* `.` :  

The filter piecewise determines if the number of photons within a time window
(tw) exceeds  

Parameters
----------
* `n_ph_max.` :  
    If the within a tw the number of photons is smaller than  
* `n_ph_max`, `the` :  
    time events within the tw are added to the selection.  
* `selection` :  
* `n_selected` :  
* `time` :  
* `n_time` :  
* `tw` :  
* `n_ph_max` :  
";

%feature("docstring") histogram_trace "

Splits the time trace into bins that are at least of the length specified by  

Parameters
----------
* `time_window` :  
    and counts the number of photons in each time interval  
* `out` :  
    array of counts  
* `n_out` :  
    number of elements in  
* `out` :  
* `time` :  
    array of detection times  
* `n_time` :  
    number of elements in the  
* `time` :  
    array  
* `time_window` :  
    The size of the  
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

Parameters
----------
* `out` :  
* `n_out` :  
* `in` :  
* `n_in` :  
* `routing_channels` :  
* `n_routing_channels` :  
";

%feature("docstring") get_array "
";

%feature("docstring") ranges_by_time_window "

Determines time windows (tw) for an array of consecutive time events based on a
minimum tw size  

Parameters
----------
* `tw_min`, `a` :  
    minimum number of photons in a tw  
* `n_ph_min.` :  
    Optionally, the tw bigger than  
* `tw_max` :  
    and tw with more photons than  
* `n_ph_max` :  
    are disregarded.  

The function determines for an array of consecutive time events passed to the
function by the argument  

Parameters
----------
* `time` :  
    an interleaved array  
* `time_windows` :  
    containing a list of time windows (tw). In the array  
* `time_windows` :  
    the beginning and the end of the tws are interleaved, e.g., for two time
    windows  

         [begin1, end1, begin2, end2]  

The returned beginnings and ends refer to the index of the photons in the array
of consecutive times  

Parameters
----------
* `time.` :  

The selection of the tws can be adjusted by the parameters  

Parameters
----------
* `tw_min` :  
";

// File: _r_e_a_d_m_e_8md.xml

// File: setup_8py.xml

// File: dir_d44c64559bbebec7f509842c48db8b23.xml

// File: indexpage.xml

