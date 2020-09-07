#ifndef TTTRLIB_IMAGE_H
#define TTTRLIB_IMAGE_H

#include <omp.h>
#include <stdlib.h>
#include <vector>
#include <iterator> // std::begin, std::end
#include <list>
#include <cstring>
#include <numeric>      // std::accumulate
#include <algorithm>
#include "fftw3.h"

#include "tttr.h"
#include "correlation.h"


class CLSMPixel : public TTTRRange{

    friend class CLSMLine;
    friend class CLSMImage;

    CLSMPixel(const CLSMPixel& p2) : TTTRRange(p2){};

    CLSMPixel(CLSMPixel* p2 = nullptr){
        if(p2 != nullptr){
            _start = p2->_start;
            _stop = p2->_stop;
            _start_time = p2->_start_time;
            _stop_time = p2->_stop_time;
            _tttr_indices = p2->_tttr_indices;
        }
    }

};


class CLSMLine : public TTTRRange{

    friend class CLSMImage;
    friend class CLSMFrame;

private:
    std::vector<CLSMPixel*> pixels;

public:
    int n_pixel = 0;
    int pixel_duration = 1;

    std::vector<CLSMPixel*> get_pixels(){
        return pixels;
    }

    unsigned long long get_pixel_duration(){
        return (size_t) (get_duration() / n_pixel);
    }

    /// Get the number of pixels per line a frame of the CLSMImage
    int get_n_pixel() const {
        return n_pixel;
    }

    CLSMLine() = default;

    CLSMLine(const CLSMLine& old_line, bool fill = false) : TTTRRange(old_line){
        // private attributes
        if(fill){
            for(auto p: old_line.pixels){
                pixels.emplace_back(new CLSMPixel(*p));
                n_pixel++;
            }
        } else{
            for(auto &p: old_line.pixels){
                pixels.emplace_back(new CLSMPixel());
                n_pixel++;
            }
        }
    }

    explicit CLSMLine(unsigned int line_start){
        _start = line_start;
    }

    CLSMLine(
            int line_start,
            int n_pixel
            ){
        this->_start = line_start;
        this->n_pixel = std::abs(n_pixel);
        for(unsigned int i=0; i<n_pixel; i++){
            auto* pixel = new CLSMPixel();
            pixels.emplace_back(pixel);
        }
    }

    ~CLSMLine(){
        for(auto p: pixels){
            delete(p);
        }
    }

    void append(CLSMPixel* pixel){
        pixels.emplace_back(pixel);
        n_pixel++;
        pixel_duration = (int) (get_duration() / n_pixel);
    }

    CLSMPixel* operator[](unsigned int i_pixel){
        return pixels[i_pixel];
    }

};


class CLSMFrame: public TTTRRange{

    friend class CLSMImage;

private:
    std::vector<CLSMLine*> lines;

public:
    unsigned int n_lines = 0;

    std::vector<CLSMLine*> get_lines(){
        return lines;
    }

    /// Get the number of lines per frame in the CLSMImage
    int get_n_lines() const{
        return (int) n_lines;
    }

    CLSMFrame();

    CLSMFrame(
            const CLSMFrame& old_frame,
            bool fill = false
    ) : TTTRRange(old_frame){
        // private attributes
        for(auto l: old_frame.lines){
            lines.emplace_back(new CLSMLine(*l, fill));
            n_lines++;
        }
    }

    ~CLSMFrame(){
        for(auto l: lines){
            delete(l);
        }
    }

    explicit CLSMFrame(size_t frame_start);

    /*!
     * Append a line to the current frame
     * @param line
     */
    void append(CLSMLine * line);

    /*!
     *
     * @param i_line the line number
     * @return a pointer to the line with requested number
     */
    CLSMLine* operator[](unsigned int i_line){
        return lines[i_line];
    }
};


class CLSMImage {

    friend class Correlator;

    friend class CLSMFrame;

    friend class CLSMLine;

    friend class CLSMPixel;

private:
    std::vector<CLSMFrame *> frames;

    void remove_incomplete_frames();

    void define_pixels_in_lines();

protected:
    /// The number of frames in an CLSMImage
    size_t n_frames = 0;

    /// The number of lines per frames
    size_t n_lines = 0;

    /// The number if pixels per line
    size_t n_pixel = 0;


protected:
    /*!
     * Initializes the frames, lines, and pixels of a CLSMImage.
     *
     * @param skip_before_first_frame_marker if set to true (the default value is true) all events
     * before the first frame marker are ignored.
     */
    void initialize_default(
            TTTR *tttr_data,
            bool skip_before_first_frame_marker = true
    );

    /*!
     * Leica SP5
     * @param tttr_data
     */
    void initialize_leica_sp5_ptu(TTTR *tttr_data);

    /*!
     * Leica SP8
     * @param tttr_data
     */
    void initialize_leica_sp8_ptu(TTTR *tttr_data);


public:

    /// Vector containing the tttr indices of the frame markers
    std::vector<int> marker_frame;

    /// Defines the marker for a line start
    int marker_line_start;

    /// Defines the marker for a line stop
    int marker_line_stop;

    /// The event type used for the marker
    int marker_event;

    std::shared_ptr<TTTR> tttr;

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
    void fill_pixels(
            TTTR *tttr_data,
            std::vector<int> channels,
            bool clear_pixel = true
    );

    /*!
     * Computes the
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

    /*!
     * Clear tttr_indices stored in the pixels
     *
     * @param channels
     */
    void clear_pixels();

    std::vector<CLSMFrame *> get_frames() {
        return frames;
    }

    void get_intensity_image(
            unsigned int **output, int *dim1, int *dim2, int *dim3
    );

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
    void get_fluorescence_decay_image(
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
    void get_average_decay_of_pixels(
            TTTR *tttr_data,
            uint8_t* mask, int dmask1, int dmaks2, int dmask3,
            unsigned int **output, int *dim1, int *dim2,
            int tac_coarsening,
            bool stack_frames
    );

    /*!
     * Calculates an image stack where the value of each pixel corresponds
     * to the mean micro time.
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
    void get_mean_micro_time_image(
            TTTR *tttr_data,
            double **output, int *dim1, int *dim2, int *dim3,
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
    void get_phasor_image(
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
    void get_mean_lifetime_image(
            TTTR *tttr_data,
            double **output, int *dim1, int *dim2, int *dim3,
            int minimum_number_of_photons = 3,
            TTTR *tttr_irf = nullptr,
            double m0_irf = 1.0, double m1_irf = 1.0,
            bool stack_frames = false
    );


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
            bool fill = false,
            std::vector<int> channels = std::vector<int>(),
            bool skip_before_first_frame_marker = false
    );

    /// Destructor
    ~CLSMImage() {
        for (auto frame : frames) {
            delete (frame);
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

};
#endif //TTTRLIB_IMAGE_H
