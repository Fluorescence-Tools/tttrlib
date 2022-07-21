#ifndef TTTRLIB_CLSMIMAGE_H
#define TTTRLIB_CLSMIMAGE_H

#include <vector>
#include <cstring>
#include "fftw3.h" /* FFT for ICS*/

#include "TTTR.h" /* TTTR */
#include "Correlator.h"
#include "DecayPhasor.h"

#include "CLSMFrame.h"
#include "CLSMLine.h"
#include "CLSMPixel.h"


class CLSMImage {

    friend class Correlator;
    friend class CLSMFrame;
    friend class CLSMLine;
    friend class CLSMPixel;

private:

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

    /// Pointer to tttr data that was used to construct the Image
    std::shared_ptr<TTTR> tttr = nullptr;

    void create_frames(bool clear_first = true);

    void create_lines();

    void determine_number_of_lines();

public:

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
     * Fill the tttr_indices of the pixels with the indices of the channels
     * that are within a pixel
     *
     * @param channels[in] list of routing channels. Events that have routing
     * channels in this vector are added to pixels of corresponding time.
     * @param clear_pixel[in] if set to true (default) the pixels are cleared
     * before they are filled. If set to false new tttr indices are added to
     * the pixels
     */
    void fill(
            TTTR *tttr_data = nullptr,
            std::vector<int> channels = std::vector<int>(),
            bool clear = true,
            const std::vector<std::pair<int,int>> &micro_time_ranges = std::vector<std::pair<int,int>>()
    );
    void fill_pixels(
            TTTR *tttr_data,
            std::vector<int> channels,
            bool clear_pixel = true,
            std::vector<std::pair<int,int>> micro_time_ranges = std::vector<std::pair<int,int>>()
    ){
        std::clog << "WARNING: 'fill_pixels' deprecated.  Use 'fill'." << std::endl;
        fill(tttr_data, channels, clear_pixel, micro_time_ranges);
    }

    /*!
     * Clear tttr_indices stored in the pixels
     */
    void clear();
    void clear_pixels(){
        std::clog << "WARNING: 'clear_pixels' deprecated.  Use 'clear'." << std::endl;
        clear();
    }

    /*!
     * Strips tttr_indices from all pixels in Image
     * assumes that each tttr index is only once in an image
     */
    void strip(const std::vector<int> &tttr_indices);

    /// Get tttr indices of photons in
    std::vector<int>  get_tttr_indices(){
        auto idx = std::vector<int>();
        for(auto &f: get_frames()){
        }
        return idx;
    }

    /*!
     * Computes the an image where pixels are correlation curves
     *
     * @param output[out]
     * @param dim1[out]
     * @param dim2[out]
     * @param dim3[out]
     * @param dim4[out]
     * @param tttr_self
     * @param tac_coarsening
     * @param stack_frames
     */
    void get_fcs_image(
            float **output, int *dim1, int *dim2, int *dim3, int *dim4,
            std::shared_ptr<TTTR> tttr,
            CLSMImage *clsm_other,
            std::string correlation_method = "default",
            int n_bins = 50,
            int n_casc = 1,
            bool stack_frames = false,
            bool normalized_correlation = false,
            int min_photons = 2
    );

    /// Get the frames in the CLSMImage
    std::vector<CLSMFrame *> get_frames() {
        return frames;
    }

    /// Intensity image
    void get_intensity(unsigned int **output, int *dim1, int *dim2, int *dim3);

    /*!
     * Computes an image stack where the value of each pixel corresponds to
     * a histogram of micro times in each pixel. The micro times can be coarsened
     * by integer numbers.
     *
     * @param tttr_data pointer to a TTTR object
     * @param out pointer to output array of unsigned chars that will contain
     * the image stack
     * @param dim1 number of frames
     * @param dim2 number of lines
     * @param dim3 number of pixels
     * @param dim4 number of micro time channels in the histogram
     * @param micro_time_coarsening constant used to coarsen the micro times. The default
     * value is 1 and the micro times are binned without coarsening.
     * @param stack_frames if True the frames are stacked.
     */
    void get_fluorescence_decay(
            TTTR *tttr_data,
            unsigned char **output, int *dim1, int *dim2, int *dim3, int *dim4,
            int micro_time_coarsening = 1,
            bool stack_frames = false
    );

    /*!
     * Computes micro time histograms for the stacks of images and a selection
     * of pixels. Photons in pixels that are selected by the selection array
     * contribute to the returned array of micro time histograms.
     *
     * @param tttr_data pointer to a TTTR object
     * @param mask a stack of images used as a mask to select pixels
     * @param dmask1  number of frames
     * @param dmask2  number of lines
     * @param dmask3  number of pixels per line
     * @param out pointer to output array of unsigned int contains the micro time histograms
     * @param dim1 dimension of the output array, i.e., the number of stacks
     * @param dim1 dimension the number of micro time channels
     * @param tac_coarsening constant used to coarsen the micro times
     * @param stack_frames if True the frames are stacked.
     */
    void get_decay_of_pixels(
            TTTR *tttr_data,
            uint8_t* mask, int dmask1, int dmask2, int dmask3,
            unsigned int **output, int *dim1, int *dim2,
            int tac_coarsening,
            bool stack_frames
    );

    /*!
     * Calculates an image stack where the value of each pixel corresponds
     * to the mean micro time (in units of the micro channel resolution).
     *
     * Pixels with few photons can be discriminated. Discriminated pixels will
     * be filled with zeros.
     *
     * @param tttr_data[in] pointer to a TTTR object
     * @param out[out] pointer to output array that will contain the image stack
     * @param dim1[out] returns the number of frames
     * @param dim2[out] returns the number of lines
     * @param dim3[out] returns the number of pixels per line
     * @param minimum_number_of_photons[in] the minimum number of photons in a micro time
     * @param stack_frames[in] if true the frames are stacked (default value is
     * false). If stack frames is set to true the mean arrival time is computed
     * using the tttr indices of all pixels (this corresponds to the photon weighted
     * mean arrival time).
     */
    void get_mean_micro_time(
            TTTR *tttr_data,
            double **output, int *dim1, int *dim2, int *dim3,
            double microtime_resolution = -1.0,
            int minimum_number_of_photons = 2,
            bool stack_frames = false
    );

    /*!
     * Computes the phasor values for every pixel
     *
     * Pixels with few photons can be discriminated. Discriminated pixels will
     * be filled with zeros.
     *
     * @param tttr_data[in] pointer to a TTTR object
     * @param out[out] pointer to output array that will contain the image stack
     * @param dim1[out] returns the number of frames
     * @param dim2[out] returns the number of lines
     * @param dim3[out] returns the number of pixels per line
     * @param dim4[out] returns 2 (first is the g phasor value (cos), second the
     * s phasor (sin)
     * @param minimum_number_of_photons[in] the minimum number of photons in a micro time
     * (only used if frames are not stacked)
     * @param stack_frames[in] if true the frames are stacked (default value is
     * false). If stack frames is set to true the mean arrival time is computed
     * using the tttr indices of all pixels (this corresponds to the photon weighted
     * mean arrival time).
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
     *  Computes an image of average lifetimes
     *
     * The average lifetimes are computed (not fitted) by the methods of
     * moments (Irvin Isenberg, 1973, Biophysical journal). This approach
     * does not consider scattered light.
     *
     * Pixels with few photons can be discriminated. Discriminated pixels are
     * filled with zeros.
     *
     * By default the fluorescence lifetimes of the pixels are computed in
     * units of nanoseconds.
     *
     * @param tttr_data[in] pointer to a TTTR object
     * @param tttr_irf[in] pointer to a TTTR object of the IRF
     * @param out[out] pointer to output array that will contain the image stack
     * @param dim1[out] returns the number of frames
     * @param dim2[out] returns the number of lines
     * @param dim3[out] returns the number of pixels per line
     * @param minimum_number_of_photons[in] the minimum number of photons in a micro time
     * @param m0_irf is the zero moment of the IRF (optional, default=1)
     * @param m1_irf is the first moment of the IRF (optional, default=1)
     */
    void get_mean_lifetime(
            TTTR *tttr_data,
            double **output, int *dim1, int *dim2, int *dim3,
            int minimum_number_of_photons = 3,
            TTTR *tttr_irf = nullptr, double m0_irf = 1.0, double m1_irf = 1.0,
            bool stack_frames = false
    );

    /// Convert frame, line, and pixel to 1D index
    inline int to1D(int frame, int line, int pixel) {
        return (frame * n_lines * n_pixel) + (line * n_lines) + pixel;
    }

    /// Convert 1D index to frame, line, and pixel
    inline std::vector<int> to3D(int idx) {
        int frame = idx / (n_lines * n_pixel);
        idx -= (frame * n_lines * n_pixel);
        int line = idx / n_lines;
        int pixel = idx % n_pixel;
        return std::vector<int>{frame, line, pixel};
    }

    CLSMPixel* getPixel(unsigned int idx){
        int frame, line, pixel;

        frame = idx / (n_lines * n_pixel);
        idx -= (frame * n_lines * n_pixel);
        line = idx / n_lines;
        pixel = idx % n_pixel;

        CLSMFrame* s_frame = frames[frame];
        CLSMLine*  s_line  = s_frame->lines[line];
        CLSMPixel* s_pixel = &(s_line->pixels[pixel]);
        return s_pixel;
    }

    /// Get the number of frames in the CLSM image
    int get_n_frames() const {
        return n_frames;
    }

    /// Get the number of lines per frame in the CLSMImage
    int get_n_lines() const {
        return n_lines;
    }

    /// Get the number of pixels per line a frame of the CLSMImage
    int get_n_pixel() const {
        return n_pixel;
    }

    /*!
     * Copy the information from another CLSMImage object
     *
     * @param p2 The information from this object is copied.
     * @param fill If this is set to true (default is false) the tttr indices
     * of the pixels are copied.
     * @return
     */
    void copy(const CLSMImage &p2, bool fill = false);

    /*!
     * Append a frame to the CLSM image.
     *
     * @param frame
     */
    void append(CLSMFrame *frame);

    /*!
     * Moves the content of the Pixels
     *
     * The input is an interleaved array or source and target
     * pixel indices. A pixel index is a mapping from a frames,
     * lines, and pixel combination to an index.
     *
     * @param index
     * @param n_index
     */
    void transform(unsigned int* input, int n_input);

    /*!
     * Rebin a CLSMImage
     *
     * Note, rebinning redistributes photons and thus
     * the macro times in pixels.
     *
     * @param bin_line binning factor for lines
     * @param bin_pixel binning factor for pixel in lines
     */
    void rebin(int bin_line, int bin_pixel);

    /*!
     * Distribute the photons of a pixel_id to a set of
     * pixel ids in a target image according to provided probabilities
     *
     */
    void distribute(
            unsigned int pixel_id,
            CLSMImage* target,
            std::vector<int> &target_pixel_ids,
            std::vector<int> &target_probabilities
    );

    /*!
     * Crop the image
     * @param frame_start
     * @param frame_stop
     * @param line_start
     * @param line_stop
     * @param pixel_start
     * @param pixel_stop
     */
    void crop(
            int frame_start, int frame_stop,
            int line_start, int line_stop,
            int pixel_start, int pixel_stop
    );

    void stack_frames(){
        CLSMFrame* f0 = frames[0];
        for(int i = 1; i < n_frames; i++){
            *f0 += *frames[i];
        }
        frames.resize(1);
        n_frames = 1;
    }

    /// Copy constructor
    CLSMImage(const CLSMImage &p2, bool fill = false);

    /*!
     *
     * @param tttr_data pointer to TTTR object
     * @param marker_frame_start routing channel numbers (default reading routine)
     * or micro time channel number (SP8 reading routine) that serves as a marker
     * informing on a new frame in the TTTR data stream.
     * @param marker_line_start routing channel number (default reading routine)
     * or micro time channel number (SP8 reading routine) that serves as a marker
     * informing on the start of a new line in a frame within the TTTR data stream
     * @param marker_line_stop routing channel number (default reading routine)
     * or micro time channel number (SP8 reading routine) that serves as a marker
     * informing on the stop of a new line in a frame within the TTTR data stream
     * @param marker_event_type event types that are interpreted as markers for
     * frames and lines.
     * @param n_pixel_per_line number of pixels into which each line is separated.
     * If the number of pixels per line is set to zero. The number of pixels per
     * line will correspond to the number of lines in the first frame.
     * @param reading_routine an integer that specifies the reading routine used to
     * read a CLSM image out of a TTTR data stream. A CLSM image can be encoded
     * by several ways in a TTTR stream. Leica encodes frame and line markers in
     * micro time channel numbers. PicoQuant and others use a more 'traditional'
     * encoding for frame and line markers marking TTTR events as marker events and
     * using the channel number to differentiate the different marker types.
     * @param macro_time_shift Number of macro time counts a line start is shifted
     * relative to the line start marker in the TTTR object (default 0)
     * @param source A CLSMImage object that is used as a template for the created
     * object. All frames and lines are copied and empty pixels are created. If
     * the parameter fill is set to true moreover the content of the pixels is copied.
     * @param fill if set to true (default) is false the lines are filled with pixels
     * that will contain either the photons of the specified channels or the photons
     * from the source CLSMImage instance.
     * @param channels The channel number of the events that will be used to fill
     * the pixels.
     * @param stack_frames If set to true (default is false) the frames in the CLSM
     * image are stacked and the resulting CLSMImage will hava a single frame.
     */
    explicit CLSMImage(
            std::shared_ptr<TTTR> tttr_data = nullptr,
            std::vector<int> marker_frame_start = std::vector<int>(),
            int marker_line_start = 0,
            int marker_line_stop = 0,
            int marker_event_type = 0,
            int n_pixel_per_line = 0,
            std::string reading_routine = "default",
            long long macro_time_shift = 0,
            CLSMImage *source = nullptr,
            bool fill = true,
            std::vector<int> channels = std::vector<int>(),
            bool skip_before_first_frame_marker = false,
            bool skip_after_last_frame_marker = false,
            std::vector<std::pair<int,int>> micro_time_ranges = std::vector<std::pair<int,int>>()
    );

    /// Destructor
    virtual ~CLSMImage() {
        for (auto frame : frames) {
            delete frame;
        }
    }

    /*!
     * Shift the line starts at least by a specified number of macro time clock
     * counts
     *
     * @param tttr_data pointer [in] to the TTTR object used to construct the CLSM
     * object that is shifted
     * @param macro_time_shift  [in] the number of macro time counts that which
     * which the lines are at least shifted.
     */
    void shift_line_start(
            int macro_time_shift
    );

    CLSMFrame *operator[](unsigned int i_frame) {
        return frames[i_frame];
    };

    /*!
     * Computes an image correlation via FFTs for a set of frames
     *
     * This function computes the image correlation for a set of frames. The
     * frames can be either specified by an array or by a CLSMImage object. This
     * function can compute image cross-correlation and image auto-correlations.
     * The type of the correlation is specified by a set of pairs that are cross-
     * correlated.
     *
     * @param output the array that will contain the ICS
     * @param dim1 number of frames in the ICS
     * @param dim2 number of lines (line shifts) in the ICS
     * @param dim3 number of pixel (pixel shifts) in the ICS
     * @param tttr_data
     * @param clsm an optional pointer to a CLSMImage object
     * @param images an optional pointer to an image array
     * @param input_frames number of frames in the image array
     * @param input_lines number of lines in the image array
     * @param input_pixel number of pixel in the image array
     * @param x_range defines the region of interest (ROI) in the image (pixel).
     * This parameter is optional. The default value is [0,-1]. This means that
     * the entire input pixel range is used
     * @param y_range region defines the ROI in y-direction (lines). The default
     * value is [0,-1]. By default all lines in the image are used.
     * @param frames_index_pairs A vector of integer pairs. The pairs correspond
     * to the frame numbers in the input that will be cross-correlated. If no
     * vector of frame pairs is specified the image auto-correlation will be
     * computed
     * @param subtract_average the input image can be corrected for the background,
     * i.e., a constant background can be subtracted from the frames. If this
     * parameter is set to "stack" the average over all frames is computed and
     * subtracted pixel-wise from each frame. If this parameter is set to "frame"
     * the average of each frame is computed and subtracted from the each frame.
     * By default no correction is applied.
     * @param mask a stack of images used as a to select pixels
     * @param dmask1  number of frames
     * @param dmask2  number of lines
     * @param dmask3  number of pixels per line
     */
    static void compute_ics(
            double **output, int *dim1, int *dim2, int *dim3,
            std::shared_ptr<TTTR> tttr_data = nullptr,
            CLSMImage* clsm = nullptr,
            double *images = nullptr, int input_frames=-1, int input_lines=-1, int input_pixel=1,
            std::vector<int> x_range=std::vector<int>({0,-1}),
            std::vector<int> y_range=std::vector<int>({0,-1}),
            std::vector<std::pair<int,int>> frames_index_pairs=std::vector<std::pair<int,int>>(),
            std::string subtract_average="",
            CLSMImage* clsm2=nullptr,
            double *images_2=nullptr, int input_frames_2=-1, int input_lines_2=-1, int input_pixel_2=-1,
            uint8_t *mask=nullptr, int dmask1=-1, int dmask2=-1, int dmask3=-1
    );

     /*!
      * Copies a region of interest (ROI) into a new image and does some background
      * correction.
      *
      * The ROI is defined by defining a range for the pixels and lines. The ROI
      * can be corrected by a constant background value, clipped to limit the range
      * of the output values, and corrected by the mean intensity of the frames.
      *
      * @param output the array that will contain the ROI. The array is allocated
      * by the function
      * @param dim1 the number of frames in the output ROI
      * @param dim2 the number of lines per frame in the output
      * @param dim3 the number of pixels per line in the output ROI
      * @param clsm a pointer to a CLSMImage object
      * @param x_range the range (selection) of the pixels
      * @param y_range the range (selection) of the lines
      * @param subtract_average If this parameter is set to "stack" the mean image
      * of the ROIs that is computed by the average over all frames is subtracted
      * from each frame and the mean intensity of all frames and pixels is added
      * to the pixels. If this parameter is set to "frame" the average of each
      * frame is subtracted from each frame. The default behaviour is to do nothing.
      * @param background A constant number that is subtracted from each pixel.
      * @param clip If set to true (the default value is false) the values in the
      * ROI are clipped to the range [clip_min, clip_max]
      * @param clip_max the maximum value when output ROIs are clipped
      * @param clip_min the minimum value when output ROIs are clipped
      * @param images Input array of images that are used to defined ROIs. If no
      * CLSMImage object is specified. This array is used as an input.
      * @param n_frames The number of frames in the input array images
      * @param n_lines The number of lines in the input array images
      * @param n_pixel The number of pixel in the input array images
      * @param selected_frames A list of frames that is used to define the ROIs.
      * If no frames are defined by this list, all frames in the input are used.
      * @param mask a stack of images used as a to select pixels
      * @param dmask1  number of frames if the number of frames in the mask is
      * smaller then the ROI the first mask frame will be applied to all ROI
      * frames that are greater than dmask1
      * @param dmask2  number of lines if smaller then ROI the outside region
      * will be selected and the mask will be applied to all lines smaller than
      * dmask2
      * @param dmask3 number of pixels per line in the mask.
      */
    static void get_roi(
             double** output, int* dim1, int* dim2, int* dim3,
             CLSMImage* clsm = nullptr,
             std::vector<int> x_range=std::vector<int>({0,-1}),
             std::vector<int> y_range=std::vector<int>({0,-1}),
             std::string subtract_average = "",
             double background = 0.0,
             bool clip=false, double clip_max=1e6, double clip_min=-1e6,
             double *images = nullptr, int input_frames=-1, int input_lines=-1, int input_pixel=1,
             uint8_t *mask = nullptr, int dmask1 = -1, int dmask2 = -1, int dmask3 = -1,
             std::vector<int> selected_frames = std::vector<int>()
    );

    /*!
    * Get the tttr indices of frame markers for a SP8
    *
    * @param tttr pointer to the TTTR object that is inspected
    * @param marker_frame vector of
    * @param marker_event
    * @param start_event
    * @param stop_event
    * @return
    */
    static void get_frame_edges(
            int** output, int* n_output,
            TTTR* tttr = nullptr,
            int start_event = 0,
            int stop_event = -1,
            std::vector<int> marker_frame = std::vector<int>({4, 6}),
            int marker_event = 15,
            std::string reading_routine = "SP8",
            bool skip_before_first_frame_marker = false,
            bool skip_after_last_frame_marker = false
    );

    static void get_line_edges(
            unsigned int** output, int* n_output,
            TTTR* tttr,
            int start_event,
            int stop_event,
            int marker_line_start = 1,
            int marker_line_stop = 2,
            int marker_event = 15,
            std::string reading_routine = "SP8"
    );

};


#endif //TTTRLIB_CLSMIMAGE_H
