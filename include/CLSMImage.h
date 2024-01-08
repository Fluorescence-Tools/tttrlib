#ifndef TTTRLIB_CLSMIMAGE_H
#define TTTRLIB_CLSMIMAGE_H

#include <iostream> /* cout, clog */
#include <vector>
#include <utility>

#include <cstring>
#include <complex>
#include <cmath>

#include "pocketfft/pocketfft_hdronly.h"

#include "TTTR.h" /* TTTR */

#include "CLSMFrame.h"
#include "CLSMLine.h"
#include "CLSMPixel.h"
#include "DecayPhasor.h"
#include "Correlator.h"


/// Different types of distances between two accessible volumes
typedef enum{
    CLSM_DEFAULT,         /// Default reading compute_icsroutine
    CLSM_SP5,             /// Leica SP5
    CLSM_SP8              /// Leica SP5
} ReadingRoutine;


template<typename TS, typename TE>
static std::pair<int, int> find_clsm_start_stop(
        int &i_event,
        int marker_start, int marker_stop, int marker_event,
        unsigned long long* macro_time_arr,
        TS start_stop_arr,
        TE event_type_arr,
        size_t n,
        long duration = -1
){
    int start = -1;
    int stop = -1;
    for(; i_event < n; i_event++)
    {
        if(event_type_arr[i_event] != marker_event) continue;
        if(start_stop_arr[i_event] == marker_start)
            // start found -> search stop and break
        {
            start = i_event;
            if(marker_stop > 0) // search for stop idx using stop event
            {
                for(;i_event < n; i_event++){
                    if(event_type_arr[i_event] != marker_event) continue;
                    if(start_stop_arr[i_event] == marker_stop){
                        stop = i_event;
                        break;
                    }
                }
            }
            else if(duration > 0) // search for stop idx using duration
            {
                unsigned long stop_time = macro_time_arr[i_event] + duration;
                for(;i_event < n; i_event++){
                    if(macro_time_arr[i_event] >= stop_time){
                        stop = i_event;
                        break;
                    }
                }
            }
            else // do not search for stop idx
            {
                stop = 0;
            }
            break;
        }
    }
    return {start, stop};
}



class CLSMSettings{

    friend class CLSMImage;

protected:

    /// To skip incomplete frames
    bool skip_before_first_frame_marker = false;
    bool skip_after_last_frame_marker = false;

    int reading_routine = CLSM_DEFAULT;

    /// Defines the marker for a line start
    int marker_line_start = 0;

    /// Defines the marker for a line stop
    int marker_line_stop = 0;

    /// Vector containing the tttr indices of the frame markers
    std::vector<int> marker_frame_start = {};

    /// The event type used for the marker
    int marker_event_type = 0;

    int n_pixel_per_line = 0;
    int n_lines = 0;

public:

    /*!
     * \brief CLSMSettings Constructor.
     *
     * Constructs a CLSMSettings object with the specified parameters.
     *
     * @param skip_before_first_frame_marker If true, skip TTTR events before the first frame marker (default is false).
     * @param skip_after_last_frame_marker   If true, skip TTTR events after the last frame marker (default is false).
     * @param reading_routine               An integer specifying the reading routine used to
     *                                      read a CLSM image out of a TTTR data stream. A CLSM image can be encoded
     *                                      in various ways in a TTTR stream.
     * @param marker_line_start             Routing channel number or micro time channel number serving as a marker
     *                                      for the start of a new line in a frame within the TTTR data stream.
     * @param marker_line_stop              Routing channel number or micro time channel number serving as a marker
     *                                      for the stop of a line in a frame within the TTTR data stream.
     * @param marker_frame_start            Routing channel numbers (default reading routine)
     *                                      or micro time channel number (SP8 reading routine) serving as a marker
     *                                      for a new frame in the TTTR data stream.
     * @param marker_event_type             Event types interpreted as markers for frames and lines.
     * @param n_pixel_per_line              Number of pixels into which each line is separated.
     *                                      If set to zero, the number of pixels per line corresponds to the number
     *                                      of lines in the first frame.
     * @param n_lines                       Number of lines (default is -1, auto-detect based on the first frame).
     */
    explicit CLSMSettings(
        bool skip_before_first_frame_marker = false,
        bool skip_after_last_frame_marker = false,
        int reading_routine = CLSM_DEFAULT,
        int marker_line_start = 3,
        int marker_line_stop = 2,
        std::vector<int> marker_frame_start = std::vector<int>({1}),
        int marker_event_type = 1,
        int n_pixel_per_line = 1,
        int n_lines = -1
        // long long macro_time_shift = 0
    ){
        this->skip_before_first_frame_marker = skip_before_first_frame_marker;
        this->skip_after_last_frame_marker = skip_after_last_frame_marker;
        this->reading_routine = reading_routine;
        this->n_pixel_per_line = n_pixel_per_line;
        this->n_lines = n_lines;
        this->marker_event_type = marker_event_type;
        this->marker_line_stop = marker_line_stop;
        this->marker_line_start = marker_line_start;
        this->marker_frame_start = marker_frame_start;
//        this->macro_time_shift = macro_time_shift;
    }

};


class CLSMImage {

    friend class Correlator;
    friend class CLSMFrame;
    friend class CLSMLine;
    friend class CLSMPixel;

private:

    CLSMSettings settings;

    /// Used to tack if the CLSMImage is in a filled state
    bool _is_filled_ = false;

    std::vector<CLSMFrame *> frames;

    void remove_incomplete_frames();

    void create_pixels_in_lines();

protected:

    /// The number of frames in an CLSMImage
    size_t n_frames = 0;

    /// The number of lines per frames
    size_t n_lines = 0;

    /// The number if pixels per line
    size_t n_pixel = 0;

    /// Pointer to tttr data that used to construct the Image
    std::shared_ptr<TTTR> tttr = nullptr;

    void create_frames(bool clear_first = true);

    void create_lines();

    void determine_number_of_lines();

public:

    std::shared_ptr<TTTR> get_tttr(){
        return tttr;
    }

    void set_tttr(std::shared_ptr<TTTR> v){
        tttr = v;
    }    

    const CLSMSettings* get_settings(){
        return &settings;
    }

    /// Get the number of frames in the CLSMImage
    size_t size(){
        return frames.size();
    }

    /// Vector containing the tttr indices of the frame markers
    std::vector<int> marker_frame;

    /// Defines the marker for a line start
    int marker_line_start;

    /// Defines the marker for a line stop
    int marker_line_stop;

    /// The event type used for the marker
    int marker_event;

    std::string reading_routine = "default";

    /// CLSM TTTR data starts can have incomplete frame. Thus skipping
    /// data can make sense
    bool skip_before_first_frame_marker = false;
    bool skip_after_last_frame_marker = false;

    /*!
     * \brief Fills the time-tagged time-resolved (TTTR) indices of the pixels with the
     *        indices of the photons that fall within each pixel.
     *
     * This function processes the TTTR data to associate routing channels with pixels based
     * on specified criteria. The TTTR indices of the channels within each pixel are stored.
     *
     * @param tttr_data           Pointer to the TTTR object containing the data to be
     *                            processed (default is nullptr).
     * @param channels            List of routing channels. Events that have routing
     *                            channels in this vector are added to pixels of corresponding
     *                            time.
     * @param clear               If set to true (default), the pixels are cleared before
     *                            being filled. If set to false, new TTTR indices are added
     *                            to the pixels.
     * @param micro_time_ranges   List of pairs representing micro-time ranges. If provided,
     *                            only events within these ranges are considered.
     */
    void fill(
        TTTR *tttr_data = nullptr,
        std::vector<int> channels = {},
        bool clear = true,
        const std::vector<std::pair<int, int>> &micro_time_ranges = {}
    );

    /*!
     * \deprecated Use the 'fill' function instead.
     * \brief Fills the time-tagged time-resolved (TTTR) indices of the pixels with the
     *        indices of the channels that fall within each pixel.
     *
     * This function is deprecated in favor of 'fill'. It processes the TTTR data to
     * associate routing channels with pixels based on specified criteria. The TTTR indices
     * of the channels within each pixel are stored.
     *
     * @param tttr_data           Pointer to the TTTR object containing the data to be
     *                            processed.
     * @param channels            List of routing channels. Events that have routing
     *                            channels in this vector are added to pixels of corresponding
     *                            time.
     * @param clear_pixel         If set to true (default), the pixels are cleared before
     *                            being filled. If set to false, new TTTR indices are added
     *                            to the pixels.
     * @param micro_time_ranges   List of pairs representing micro-time ranges. If provided,
     *                            only events within these ranges are considered.
     */
    void fill_pixels(
        TTTR *tttr_data,
        std::vector<int> channels,
        bool clear_pixel = true,
        std::vector<std::pair<int, int>> micro_time_ranges = {}
    ){
        std::clog << "WARNING: 'fill_pixels' deprecated.  Use 'fill'." << std::endl;
        fill(tttr_data, channels, clear_pixel, micro_time_ranges);
    }

    /*!
     * \brief Clears the time-tagged time-resolved (TTTR) indices stored in the pixels.
     */
    void clear();

    /*!
     * \deprecated Use the 'clear' function instead.
     * \brief Clears the time-tagged time-resolved (TTTR) indices stored in the pixels.
     *
     * This function is deprecated in favor of 'clear'. It removes the stored TTTR indices
     * from the pixels.
     */
    void clear_pixels(){
        std::clog << "WARNING: 'clear_pixels' deprecated.  Use 'clear'." << std::endl;
        clear();
    }

    /*!
     * \brief Strips time-tagged time-resolved (TTTR) indices from all pixels in the image.
     *
     * This function removes specified TTTR indices from all pixels in the image. It assumes
     * that each TTTR index is present only once in the image.
     *
     * @param tttr_indices List of TTTR indices to be removed from the pixels.
     * @param offset       Offset value added to the TTTR indices before removal
     *                     (default is 0).
     */
    void strip(const std::vector<int> &tttr_indices, int offset = 0);

    /*!
     * \brief Computes an image where pixels represent correlation curves.
     *
     * This function calculates a correlation image where each pixel corresponds to a
     * correlation curve. The resulting image is determined by the specified parameters
     * and is stored in the provided output array.
     *
     * @param output                Pointer to the array that will contain the correlation
     *                              image. The array is allocated by the function.
     * @param dim1                  Number of correlation curves in the image (frames or
     *                              time bins).
     * @param dim2                  Number of lines per frame in the correlation image.
     * @param dim3                  Number of pixels per line in the correlation image.
     * @param dim4                  Number of cascades in the correlation image.
     * @param tttr                  Shared pointer to the TTTR object containing the data.
     * @param clsm_other            Pointer to another CLSMImage object (default is nullptr).
     * @param correlation_method    Method used for correlation computation (default is
     *                              "default").
     * @param n_bins                Number of bins used for correlation computation
     *                              (default is 50).
     * @param n_casc                Number of cascades in the correlation image
     *                              (default is 1).
     * @param stack_frames          If true, correlation curves are stacked into a single
     *                              image (default is false).
     * @param normalized_correlation If true, correlation curves are normalized
     *                              (default is false).
     * @param min_photons           Minimum number of photons required for correlation
     *                              computation (default is 2).
     */
    void get_fcs_image(
        float **output, int *dim1, int *dim2, int *dim3, int *dim4,
        std::shared_ptr<TTTR> tttr,
        CLSMImage *clsm_other = nullptr,
        std::string correlation_method = "default",
        int n_bins = 50,
        int n_casc = 1,
        bool stack_frames = false,
        bool normalized_correlation = false,
        int min_photons = 2
    );

    /*!
     * \brief Retrieves the frames stored in the CLSMImage.
     *
     * @return Vector of CLSMFrame pointers representing the frames in the CLSMImage.
     */
    std::vector<CLSMFrame *> get_frames() {
        return frames;
    }

    /*!
     * \brief Computes an intensity image.
     *
     * This function calculates an intensity image and stores it in the provided output
     * array. The dimensions of the intensity image are returned in the output parameters.
     *
     * @param output Pointer to the array that will contain the intensity image. The array
     *               is allocated by the function.
     * @param dim1   Number of frames in the intensity image.
     * @param dim2   Number of lines per frame in the intensity image.
     * @param dim3   Number of pixels per line in the intensity image.
     */
    void get_intensity(unsigned short **output, int *dim1, int *dim2, int *dim3);

    /*!
     * \brief Computes an image stack where the value of each pixel corresponds to a histogram
     *        of micro times in each pixel. The micro times can be coarsened by integer
     *        numbers.
     *
     * This function calculates an image stack where each pixel represents a histogram of
     * micro times. The resulting image stack is stored in the provided output array, and
     * the dimensions of the image stack are returned in the output parameters.
     *
     * @param tttr_data           Pointer to a TTTR object.
     * @param output              Pointer to the output array of unsigned chars that will
     *                            contain the image stack. The array is allocated by the
     *                            function.
     * @param dim1                Number of frames in the image stack.
     * @param dim2                Number of lines per frame in the image stack.
     * @param dim3                Number of pixels per line in the image stack.
     * @param dim4                Number of micro time channels in the histogram.
     * @param micro_time_coarsening Constant used to coarsen the micro times. The default
     *                            value is 1, and the micro times are binned without
     *                            coarsening.
     * @param stack_frames        If true, the frames are stacked.
     */
    void get_fluorescence_decay(
        TTTR *tttr_data,
        unsigned char **output, int *dim1, int *dim2, int *dim3, int *dim4,
        int micro_time_coarsening = 1,
        bool stack_frames = false
    );


    /*!
     * \brief Computes micro time histograms for the stacks of images and a selection of
     *        pixels. Photons in pixels that are selected by the selection array contribute
     *        to the returned array of micro time histograms.
     *
     * This function calculates micro time histograms for selected pixels in the image stack,
     * based on a provided mask. The resulting micro time histograms are stored in the
     * provided output array, and the dimensions of the output array are returned in the
     * output parameters.
     *
     * @param tttr_data         Pointer to a TTTR object.
     * @param mask              A stack of images used as a mask to select pixels.
     * @param dmask1            Number of frames in the mask.
     * @param dmask2            Number of lines in the mask.
     * @param dmask3            Number of pixels per line in the mask.
     * @param output            Pointer to the output array of unsigned int that contains the
     *                          micro time histograms. The array is allocated by the function.
     * @param dim1              Dimension of the output array, i.e., the number of stacks.
     * @param dim2              Dimension of the output array, i.e., the number of micro time
     *                          channels.
     * @param tac_coarsening    Constant used to coarsen the micro times.
     * @param stack_frames      If true, the frames are stacked.
     */
    void get_decay_of_pixels(
        TTTR *tttr_data,
        uint8_t* mask, int dmask1, int dmask2, int dmask3,
        unsigned int **output, int *dim1, int *dim2,
        int tac_coarsening,
        bool stack_frames
    );


    /*!
     * \brief Calculates an image stack where the value of each pixel corresponds to the
     *        mean micro time (in units of the micro channel resolution).
     *
     * Pixels with few photons can be discriminated. Discriminated pixels will be filled
     * with zeros.
     *
     * @param tttr_data               Pointer to a TTTR object.
     * @param output                  Pointer to the output array that will contain the
     *                                image stack. The array is allocated by the function.
     * @param dim1                    Returns the number of frames.
     * @param dim2                    Returns the number of lines.
     * @param dim3                    Returns the number of pixels per line.
     * @param microtime_resolution    Micro channel resolution (default is -1.0, which
     *                                means it will be obtained from the TTTR object).
     * @param minimum_number_of_photons The minimum number of photons in a micro time
     *                                (default is 2).
     * @param stack_frames            If true, the frames are stacked (default is false).
     *                                If stack frames is set to true, the mean arrival time
     *                                is computed using the TTTR indices of all pixels
     *                                (corresponding to the photon-weighted mean arrival time).
     */
    void get_mean_micro_time(
        TTTR *tttr_data,
        double **output, int *dim1, int *dim2, int *dim3,
        double microtime_resolution = -1.0,
        int minimum_number_of_photons = 2,
        bool stack_frames = false
    );


    /*!
     * \brief Computes the phasor values for every pixel.
     *
     * Pixels with few photons can be discriminated. Discriminated pixels will be filled
     * with zeros.
     *
     * @param output                  Pointer to the output array that will contain the
     *                                image stack. The array is allocated by the function.
     * @param dim1                    Returns the number of frames.
     * @param dim2                    Returns the number of lines.
     * @param dim3                    Returns the number of pixels per line.
     * @param dim4                    Returns 2 (first is the g phasor value (cos),
     *                                second the s phasor (sin)).
     * @param tttr_data               Pointer to a TTTR object.
     * @param tttr_irf                Pointer to a TTTR object representing the Instrument
     *                                Response Function (IRF) (default is nullptr).
     * @param frequency               Modulation frequency for phasor computation
     *                                (default is -1, which means it will be obtained from
     *                                the TTTR object).
     * @param minimum_number_of_photons The minimum number of photons in a micro time
     *                                (only used if frames are not stacked, default is 2).
     * @param stack_frames            If true, the frames are stacked (default is false).
     *                                If stack frames is set to true, the mean arrival time
     *                                is computed using the TTTR indices of all pixels
     *                                (corresponding to the photon-weighted mean arrival time).
     */
    void get_phasor(
        float **output, int *dim1, int *dim2, int *dim3, int *dim4,
        TTTR *tttr_data,
        TTTR *tttr_irf = nullptr,
        double frequency = -1,
        int minimum_number_of_photons = 2,
        bool stack_frames = false
    );


    /*!
     * \brief Computes an image of average lifetimes.
     *
     * The average lifetimes are computed (not fitted) by the methods of moments
     * (Irvin Isenberg, 1973, Biophysical journal).
     *
     * Pixels with few photons can be discriminated. Discriminated pixels are filled
     * with zeros.
     *
     * By default, the fluorescence lifetimes of the pixels are computed in units of
     * nanoseconds.
     *
     * @param tttr_data               Pointer to a TTTR object.
     * @param output                  Pointer to the output array that will contain the
     *                                image stack. The array is allocated by the function.
     * @param dim1                    Returns the number of frames.
     * @param dim2                    Returns the number of lines.
     * @param dim3                    Returns the number of pixels per line.
     * @param minimum_number_of_photons The minimum number of photons in a micro time
     *                                (default is 3).
     * @param tttr_irf                Pointer to a TTTR object of the Instrument Response
     *                                Function (IRF) (default is nullptr).
     * @param m0_irf                  The zero moment of the IRF (optional, default is 1.0).
     * @param m1_irf                  The first moment of the IRF (optional, default is 1.0).
     * @param stack_frames            If true, the frames are stacked (default is false).
     * @param background              Vector of background values (optional, default is an
     *                                empty vector).
     * @param m0_bg                   The zero moment of the background (optional, default is 0.0).
     * @param m1_bg                   The first moment of the background (optional, default is 0.0).
     * @param background_fraction     Background fraction used for discrimination
     *                                (default is -1.0, which disables discrimination).
     */
    void get_mean_lifetime(
        TTTR *tttr_data,
        double **output, int *dim1, int *dim2, int *dim3,
        int minimum_number_of_photons = 3,
        TTTR *tttr_irf = nullptr, double m0_irf = 1.0, double m1_irf = 1.0,
        bool stack_frames = false,
        std::vector<double> background = {},
        double m0_bg = 0.0, double m1_bg = 0.0,
        double background_fraction = -1.0
    );

    /*!
     * \brief Convert three-dimensional coordinates (frame, line, pixel) to a one-dimensional index.
     *
     * This inline function calculates a unique one-dimensional index for a given set of
     * three-dimensional coordinates representing a position in an array.
     *
     * @param frame Index of the frame.
     * @param line  Index of the line within the frame.
     * @param pixel Index of the pixel within the line.
     * @return      One-dimensional index corresponding to the input coordinates.
     */
    inline int to1D(int frame, int line, int pixel) {
        return (frame * n_lines * n_pixel) + (line * n_pixel) + pixel;
    }


    /*!
     * \brief Convert a one-dimensional index to three-dimensional coordinates (frame, line, pixel).
     *
     * This inline function calculates the three-dimensional coordinates (frame, line, pixel)
     * corresponding to a given one-dimensional index in an array.
     *
     * @param idx   One-dimensional index to be converted.
     * @return      Vector containing frame, line, and pixel indices in that order.
     */
    inline std::vector<int> to3D(int idx) {
        int frame = idx / (n_lines * n_pixel);
        idx -= (frame * n_lines * n_pixel);
        int line = idx / n_pixel;
        int pixel = idx % n_pixel;
        return std::vector<int>{frame, line, pixel};
    }


    /*!
     * \brief Get a pointer to a CLSMPixel object based on a one-dimensional index.
     *
     * This member function calculates the three-dimensional coordinates (frame, line, pixel)
     * corresponding to a given one-dimensional index and returns a pointer to the
     * corresponding CLSMPixel object.
     *
     * @param idx   One-dimensional index to determine the CLSMPixel.
     * @return      Pointer to the CLSMPixel object.
     */
    CLSMPixel* getPixel(unsigned int idx) {
        int frame, line, pixel;

        frame = idx / (n_lines * n_pixel);
        idx -= (frame * n_lines * n_pixel);
        line = idx / n_pixel;
        pixel = idx % n_pixel;

        CLSMFrame* s_frame = frames[frame];
        CLSMLine*  s_line  = s_frame->lines[line];
        CLSMPixel* s_pixel = &(s_line->pixels[pixel]);
        return s_pixel;
    }


    /*!
     * \brief Get the number of frames in the CLSM image.
     *
     * This member function returns the number of frames in the CLSM image.
     *
     * @return The number of frames in the CLSM image.
     */
    int get_n_frames() const {
        return n_frames;
    }


    /*!
     * \brief Get the number of lines per frame in the CLSMImage.
     *
     * This member function returns the number of lines per frame in the CLSMImage.
     *
     * @return The number of lines per frame in the CLSMImage.
     */
    int get_n_lines() const {
        return n_lines;
    }


    /*!
     * \brief Get the number of pixels per line in a frame of the CLSMImage.
     *
     * This member function returns the number of pixels per line in a frame of the CLSMImage.
     *
     * @return The number of pixels per line in a frame of the CLSMImage.
     */
    int get_n_pixel() const {
        return n_pixel;
    }

    /*!
     * \brief Copy information from another CLSMImage object.
     *
     * Copies the information from another CLSMImage object, including pixel indices,
     * if the `fill` parameter is set to true (default is false).
     *
     * @param p2   The CLSMImage object from which information is copied.
     * @param fill If set to true (default is false), the time-tagged time-resolved
     *             indices of the pixels are copied.
     */
    void copy(const CLSMImage &p2, bool fill = false);

    /*!
     * \brief Append a CLSMFrame to the CLSM image.
     *
     * Appends a CLSMFrame to the CLSM image.
     *
     * @param frame The CLSMFrame to be appended.
     */
    void append(CLSMFrame *frame);

    /*!
     * \brief Move the content of the Pixels based on source and target pixel indices.
     *
     * The input is an interleaved array of source and target pixel indices.
     * A pixel index is a mapping from frames, lines, and pixel combination to an index.
     *
     * @param input [in] Pointer to an interleaved array containing source and target pixel indices.
     * @param n_input [in] Number of elements in the input array.
     */
    void transform(unsigned int* input, int n_input);

    /*!
     * \brief Rebin a CLSMImage.
     *
     * Rebinning redistributes photons and macro times in pixels.
     *
     * Note: Rebinning may alter the distribution of photons and macro times in the image.
     *
     * @param bin_line [in] Binning factor for lines.
     * @param bin_pixel [in] Binning factor for pixels within lines.
     */
    void rebin(int bin_line, int bin_pixel);

    /*!
     * \brief Distribute the photons of a pixel_id to a set of pixel ids in a target image according to provided probabilities.
     *
     * This function distributes the photons of a specified pixel_id to a set of pixel_ids
     * in a target CLSMImage based on the provided probabilities.
     *
     * @param pixel_id [in] The source pixel_id whose photons will be distributed.
     * @param target [in] Pointer to the target CLSMImage.
     * @param target_pixel_ids [in] Vector of target pixel_ids to which photons will be distributed.
     * @param target_probabilities [in] Vector of probabilities corresponding to each target pixel_id.
     */
    void distribute(
        unsigned int pixel_id,
        CLSMImage* target,
        std::vector<int>& target_pixel_ids,
        std::vector<int>& target_probabilities
    );


    /*!
     * \brief Crop the CLSMImage.
     *
     * Crop the image by specifying the range of frames, lines, and pixels to keep.
     *
     * @param frame_start [in] Starting frame index for cropping.
     * @param frame_stop [in] Stopping frame index for cropping.
     * @param line_start [in] Starting line index for cropping.
     * @param line_stop [in] Stopping line index for cropping.
     * @param pixel_start [in] Starting pixel index for cropping.
     * @param pixel_stop [in] Stopping pixel index for cropping.
     */
    void crop(
        int frame_start, int frame_stop,
        int line_start, int line_stop,
        int pixel_start, int pixel_stop
    );

    /*!
     * \brief Stack frames in the CLSMImage.
     *
     * This function stacks the frames in the CLSMImage by summing the pixel values
     * of each corresponding pixel in all frames.
     */
    void stack_frames() {
        CLSMFrame* f0 = frames[0];
        for (unsigned int i = 1; i < n_frames; i++) {
            *f0 += *frames[i];
        }
        frames.resize(1);
        n_frames = 1;
    }


    /*!
     * \brief Copy constructor for CLSMImage.
     *
     * Constructs a new CLSMImage by copying the content of another CLSMImage.
     *
     * @param p2 [in] The CLSMImage object from which the content is copied.
     * @param fill [in] If set to true (default is false), the time-tagged time-resolved
     * indices of the pixels are copied.
     */
    CLSMImage(const CLSMImage &p2, bool fill = false);

    /*!
     * \brief Constructs a CLSMImage object from TTTR data.
     *
     * Constructs a CLSMImage object from Time-Tagged Time-Resolved (TTTR) data, using
     * specified markers, settings, and optional source CLSMImage.
     *
     * @param tttr_data [in] Pointer to TTTR object containing the raw data.
     * @param settings [in] CLSMSettings object specifying parameters for image construction.
     * @param source [in] A CLSMImage object used as a template. All frames and lines are copied,
     * and empty pixels are created. If fill is set to true (default), the content of the pixels is copied.
     * @param fill [in] If set to true (default), the lines are filled with pixels containing either
     * the photons of the specified channels or the photons from the source CLSMImage instance.
     * @param channels [in] The channel number of the events used to fill the pixels.
     * @param micro_time_ranges [in] Vector of pairs specifying the micro time ranges.
     * @see CLSMSettings for more details on available settings.
     */
    explicit CLSMImage(
            std::shared_ptr<TTTR> tttr_data = nullptr,
            CLSMSettings settings = CLSMSettings(),
            CLSMImage *source = nullptr,
            bool fill = true,
            std::vector<int> channels = std::vector<int>(),
            std::vector<std::pair<int, int>> micro_time_ranges =
                    std::vector<std::pair<int, int>>()
    );

    /*!
     * \brief Destructor for CLSMImage.
     *
     * Frees memory by deleting dynamically allocated CLSMFrame objects in the frames vector.
     * It ensures proper cleanup of resources when a CLSMImage object is destroyed.
     */
    virtual ~CLSMImage() {
        for (auto frame : frames) {
            delete frame;
        }
    }


    /*!
     * \brief Accessor for CLSMFrame at the specified index in CLSMImage.
     *
     * Returns a pointer to the CLSMFrame located at the given index in the frames vector.
     *
     * @param i_frame [in] Index of the desired CLSMFrame.
     * @return Pointer to the CLSMFrame at the specified index.
     */
    CLSMFrame* operator[](unsigned int i_frame) {
        return frames[i_frame];
    }


    /*!
     * \brief Performs an image correlation spectroscopy analysis via FFTs for a set of frames.
     *
     * This function computes the image correlation for a set of frames, either specified
     * by an array or a CLSMImage object. It can compute image cross-correlation and image
     * auto-correlations based on the type of correlation specified by a vector of pairs.
     *
     * @param output [out] Array that will contain the Image Correlation Spectrscopy analysis (ICS).
     * @param dim1 [out] Number of frames in the ICS.
     * @param dim2 [out] Number of lines (line shifts) in the ICS.
     * @param dim3 [out] Number of pixels (pixel shifts) in the ICS.
     * @param tttr_data [in] Pointer to TTTR object containing the raw data (optional).
     * @param clsm [in] Optional pointer to a CLSMImage object.
     * @param images [in] Optional pointer to an image array.
     * @param input_frames [in] Number of frames in the image array.
     * @param input_lines [in] Number of lines in the image array.
     * @param input_pixel [in] Number of pixels per line in the image array.
     * @param x_range [in] Defines the region of interest (ROI) in the image (pixel).
     * @param y_range [in] Defines the ROI in y-direction (lines).
     * @param frames_index_pairs [in] Vector of integer pairs corresponding to frame numbers
     *                                 used for cross-correlation (default computes auto-correlation).
     * @param subtract_average [in] Specifies background correction: "stack" subtracts the average over all frames,
     *                              "frame" subtracts the average of each frame. Default is no correction.
     * @param mask [in] Stack of images used as a mask to select pixels (optional).
     * @param dmask1 [in] Number of frames in the mask.
     * @param dmask2 [in] Number of lines in the mask.
     * @param dmask3 [in] Number of pixels per line in the mask.
     */
    static void compute_ics(
            double **output, int *dim1, int *dim2, int *dim3,
            std::shared_ptr<TTTR> tttr_data = nullptr,
            CLSMImage* clsm = nullptr,
            double *images = nullptr, int input_frames = -1, int input_lines = -1, int input_pixel = 1,
            std::vector<int> x_range = std::vector<int>({0, -1}),
            std::vector<int> y_range = std::vector<int>({0, -1}),
            std::vector<std::pair<int, int>> frames_index_pairs = std::vector<std::pair<int, int>>(),
            std::string subtract_average = "",
            uint8_t *mask = nullptr, int dmask1 = -1, int dmask2 = -1, int dmask3 = -1
    );

    /*!
     * \brief Copies a region of interest (ROI) from the input images, performs background
     *        correction, and stores the result in a new array.
     *
     * The ROI is defined by specifying ranges for pixels and lines. The function supports
     * various correction options, such as subtracting a constant background value,
     * clipping output values, and correcting by the mean intensity of frames.
     *
     * @param output        Pointer to the array that will contain the ROI. The array is
     *                      allocated by the function.
     * @param dim1          Number of frames in the output ROI.
     * @param dim2          Number of lines per frame in the output ROI.
     * @param dim3          Number of pixels per line in the output ROI.
     * @param clsm          Pointer to a CLSMImage object (default is nullptr).
     * @param x_range       Range (selection) of pixels for the ROI.
     * @param y_range       Range (selection) of lines for the ROI.
     * @param subtract_average If set to "stack," the mean image of the ROIs computed by
     *                        averaging over all frames is subtracted from each frame, and
     *                        the mean intensity of all frames and pixels is added to the
     *                        pixels. If set to "frame," the average of each frame is
     *                        subtracted from that frame. Default is no correction.
     * @param background    Constant number subtracted from each pixel.
     * @param clip          If true, values in the ROI are clipped to the range
     *                      [clip_min, clip_max] (default is false).
     * @param clip_max      Maximum value when output ROIs are clipped.
     * @param clip_min      Minimum value when output ROIs are clipped.
     * @param images        Input array of images used to define ROIs (default is nullptr).
     * @param n_frames      Number of frames in the input array images.
     * @param n_lines       Number of lines in the input array images.
     * @param n_pixels      Number of pixels per line in the input array images.
     * @param mask          Stack of images used to select pixels (default is nullptr).
     * @param dmask1        Number of frames; if smaller than ROI, the first mask frame
     *                      is applied to all ROI frames greater than dmask1.
     * @param dmask2        Number of lines; if smaller than ROI, the outside region is
     *                      selected, and the mask is applied to all lines smaller than
     *                      dmask2.
     * @param dmask3        Number of pixels per line in the mask.
     * @param selected_frames List of frames used to define the ROIs. If empty, all frames
     *                        in the input are used.
     */
    static void get_roi(
        double** output, int* dim1, int* dim2, int* dim3,
        CLSMImage* clsm = nullptr,
        std::vector<int> x_range = {0, -1},
        std::vector<int> y_range = {0, -1},
        std::string subtract_average = "",
        double background = 0.0,
        bool clip = false, double clip_max = 1e6, double clip_min = -1e6,
        double* images = nullptr, int n_frames = -1, int n_lines = -1, int n_pixels = 1,
        uint8_t* mask = nullptr, int dmask1 = -1, int dmask2 = -1, int dmask3 = -1,
        std::vector<int> selected_frames = {}
    );


    /*!
     * \brief Retrieves the time-tagged time-resolved (TTTR) indices of frame markers for
     *        a Leica SP8 microscope.
     *
     * This function analyzes a TTTR object to extract frame marker information. The frame
     * markers are identified by specified parameters, and the corresponding TTTR indices
     * are returned as a vector.
     *
     * @param tttr                        Pointer to the TTTR object that is inspected
     *                                   (default is nullptr).
     * @param start_event                 Starting event index for analysis (default is 0).
     * @param stop_event                  Stopping event index for analysis (default is -1,
     *                                   indicating the end of the TTTR data).
     * @param marker_frame_start         Vector specifying the frame markers to consider
     *                                   (default is {4, 6}).
     * @param marker_event_type           Event type associated with frame markers
     *                                   (default is 15).
     * @param reading_routine            Type of reading routine, e.g., CLSM_SP8
     *                                   (default is CLSM_SP8).
     * @param skip_before_first_frame_marker  If true, skip events before the first frame
     *                                   marker (default is false).
     * @param skip_after_last_frame_marker   If true, skip events after the last frame
     *                                   marker (default is false).
     * @return                            Vector of TTTR indices corresponding to frame
     *                                   markers.
     */
    static std::vector<int> get_frame_edges(
        TTTR* tttr = nullptr,
        int start_event = 0,
        int stop_event = -1,
        std::vector<int> marker_frame_start = {4, 6},
        int marker_event_type = 15,
        int reading_routine = CLSM_SP8,
        bool skip_before_first_frame_marker = false,
        bool skip_after_last_frame_marker = false
    );

    /*!
     * \brief Identifies line edges by reading start and stop markers in a time-tagged
     *        time-resolved (TTTR) dataset.
     *
     * This function analyzes a TTTR object to extract line edges based on specified
     * start and stop markers. The corresponding TTTR indices are returned as a vector.
     *
     * @param tttr                   Pointer to the TTTR object that is inspected.
     * @param start_event            Starting event index for analysis.
     * @param stop_event             Stopping event index for analysis.
     * @param marker_line_start      Marker value indicating the start of a line
     *                               (default is 1).
     * @param marker_line_stop       Marker value indicating the stop of a line
     *                               (default is 2).
     * @param marker_event_type      Event type associated with line markers
     *                               (default is 15).
     * @param reading_routine       Type of reading routine, e.g., CLSM_SP8
     *                               (default is CLSM_SP8).
     * @return                       Vector of TTTR indices corresponding to line edges.
     */
    static std::vector<int> get_line_edges(
        TTTR* tttr,
        int start_event, int stop_event,
        int marker_line_start = 1, int marker_line_stop = 2,
        int marker_event_type = 15,
        int reading_routine = CLSM_SP8
    );


    /*!
     * \brief Identifies line edges by reading a start marker and using line duration as
     *        the stop criterion in a time-tagged time-resolved (TTTR) dataset.
     *
     * This function analyzes a TTTR object to extract line edges based on a specified
     * start marker and line duration. The corresponding TTTR indices are returned as a
     * vector.
     *
     * @param tttr                   Pointer to the TTTR object that is inspected.
     * @param start_event            Starting event index for analysis.
     * @param stop_event             Stopping event index for analysis.
     * @param marker_line_start      Marker value indicating the start of a line
     *                               (default is 1).
     * @param line_duration          Duration of a line, used as the stop criterion
     *                               (default is 2).
     * @param marker_event_type      Event type associated with line markers
     *                               (default is 15).
     * @param reading_routine       Type of reading routine, e.g., CLSM_SP8
     *                               (default is CLSM_SP8).
     * @return                       Vector of TTTR indices corresponding to line edges.
     */
    static std::vector<int> get_line_edges_by_duration(
        TTTR* tttr,
        int start_event, int stop_event,
        int marker_line_start = 1,
        int line_duration = 2,
        int marker_event_type = 15,
        int reading_routine = CLSM_SP8
    );


    /*!
     * \brief Obtains the duration of a line (in milliseconds) for a specified frame and line.
     *
     * This function calculates the duration of a line in milliseconds for a given frame and
     * line based on the time-tagged time-resolved (TTTR) data and header information.
     *
     * @param frame  Number of the frame in the image (default is 0).
     * @param line   Number of the line in the image (default is 0).
     * @return       Duration of the line in the selected frame in milliseconds. Returns -1.0
     *               if the TTTR object is not initialized or if frame and line indices are
     *               out of bounds.
     */
    double get_line_duration(int frame = 0, int line = 0){
        double re = -1.0;
        if(tttr != nullptr){
            auto header = tttr->get_header();
            
            auto f = frames[frame];
            auto l = f->lines[line];

            int start = l->get_start();
            int stop = l->get_stop();

            unsigned long long t_stop = tttr->macro_times[stop];
            unsigned long long t_start = tttr->macro_times[start];
            unsigned long long dt = t_stop - t_start;
            double res = header->get_macro_time_resolution() * 1000.0;
            re = dt * res;
        }
        return re;
    }

    /*!
     * \brief Obtains the duration of a pixel (in milliseconds) for a specified frame and line.
     *
     * This function calculates the duration of a pixel in milliseconds for a given frame and
     * line, based on the time-tagged time-resolved (TTTR) data, header information, and the
     * number of pixels per line.
     *
     * @param frame Selected frame number (default is 0).
     * @param line  Selected line number (default is 0).
     * @return      Duration of the pixel in the selected frame in milliseconds. Returns -1.0
     *              if the TTTR object is not initialized, or if frame and line indices are
     *              out of bounds.
     */
    double get_pixel_duration(int frame = 0, int line = 0){
        return get_line_duration(frame, line) / settings.n_pixel_per_line;
    }

};


#endif //TTTRLIB_CLSMIMAGE_H
