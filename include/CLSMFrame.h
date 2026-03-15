#ifndef TTTRLIB_CLSMFRAME_H
#define TTTRLIB_CLSMFRAME_H

#include <vector>

#include "TTTR.h" /* TTTRRange */
#include "CLSMPixel.h"
#include "CLSMLine.h"
#include "TTTRSelection.h"


class CLSMFrame: public TTTRSelection{

    friend class CLSMImage;

private:

    std::vector<CLSMLine*> lines;
    TTTR* _tttr = nullptr;
    std::shared_ptr<TTTR> _tttr_shared = nullptr;  // For compatibility with set_tttr

public:

    /*!
     * \brief Get a vector containing pointers to the CLSMLines in the CLSMFrame.
     *
     * This function returns a vector containing pointers to the CLSMLines stored
     * in the CLSMFrame.
     *
     * @return A vector of CLSMLine pointers.
     */
    std::vector<CLSMLine*> get_lines() {
        return lines;
    }
    
    std::shared_ptr<TTTR> get_tttr(){
        return _tttr_shared;
    }

    void set_tttr(std::shared_ptr<TTTR> tttr){
        _tttr_shared = tttr;
        _tttr = tttr.get();
        // Propagate to all lines
        for (auto& line : lines) {
            line->set_tttr(tttr);
        }
    }


    /*!
     * \brief Get the number of lines in the CLSMFrame.
     *
     * @return The number of lines in the CLSMFrame.
     */
    size_t size() const override final{
        return lines.size();
    }

    /*!
     * \brief Default constructor for CLSMFrame.
     *
     * This constructor creates an empty CLSMFrame object.
     */
    CLSMFrame();

    /*!
     * \brief Copy constructor for CLSMFrame.
     *
     * @param old_frame [in] Reference to the existing CLSMFrame object to be copied.
     * @param fill [in] If set to false, the content of the pixels is not copied.
     */
    CLSMFrame(const CLSMFrame& old_frame, bool fill = true) : TTTRSelection(old_frame) {
        _tttr_shared = old_frame._tttr_shared;
        _tttr = old_frame._tttr;
        for (auto& l : old_frame.lines) {
            lines.emplace_back(new CLSMLine(*l, fill));
        }
    }

    CLSMFrame& operator=(const CLSMFrame& other) {
        if (this != &other) {
            // Free existing resources
            for (auto& l : lines) {
                delete l;
            }
            lines.clear();

            // Copy new data
            _tttr_shared = other._tttr_shared;
            _tttr = other._tttr;
            for (auto& l : other.lines) {
                lines.emplace_back(new CLSMLine(*l, true));
            }
            TTTRSelection::operator=(other);
        }
        return *this;
    }

    /*!
     * \brief Destructor for CLSMFrame.
     *
     * Deletes dynamically allocated CLSMLine objects in the lines vector.
     */
    virtual ~CLSMFrame() {
        for (auto& l : lines) {
            delete l;
        }
    }

    /*!
     * \brief Constructor for CLSMFrame with specified frame indices and a TTTR object.
     *
     * This constructor initializes a CLSMFrame object with the specified frame indices
     * (frame_start to frame_stop) using a TTTR (Time-Tagged Time-Resolved) data object.
     *
     * @param frame_start [in] The starting frame index for the CLSMFrame.
     * @param frame_stop [in] The stopping frame index for the CLSMFrame.
     * @param tttr [in] Pointer to a TTTR object containing time-resolved data.
     */
    explicit CLSMFrame(size_t frame_start, size_t frame_stop, std::shared_ptr<TTTR> tttr);

    /*!
     * \brief Append a CLSMLine to the current CLSMFrame.
     *
     * @param line [in] Pointer to the CLSMLine to be appended.
     */
    void append(CLSMLine* line);

    /*!
     * \brief Retrieve a pointer to the CLSMLine with the specified line number.
     *
     * @param i_line [in] The line number.
     * @return A pointer to the CLSMLine with the requested number.
     */
    CLSMLine* operator[](unsigned int i_line) {
        return lines[i_line];
    }

    /*!
     * \brief Add the corresponding CLSMLines of another CLSMFrame to the current frame.
     *
     * This operator performs element-wise addition, adding each CLSMLine of the
     * right-hand side (rhs) CLSMFrame to the corresponding CLSMLine of the current frame.
     *
     * @param rhs [in] The right-hand side CLSMFrame to be added.
     * @return A reference to the modified current CLSMFrame.
     */
    CLSMFrame& operator+=(const CLSMFrame& rhs) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            *lines[i] += *rhs.lines[i];
        }
        return *this;
    }

    /*!
     * \brief Crop the CLSMFrame by selecting a region of lines and pixels.
     *
     * This function crops the CLSMFrame by selecting a specific range of lines
     * (from line_start to line_stop) and pixels (from pixel_start to pixel_stop).
     *
     * @param line_start [in] The starting line index for cropping.
     * @param line_stop [in] The stopping line index for cropping.
     * @param pixel_start [in] The starting pixel index for cropping.
     * @param pixel_stop [in] The stopping pixel index for cropping.
     */
    void crop(
        int line_start, int line_stop,
        int pixel_start, int pixel_stop
    );

    /*!
     * \brief Get the intensity array for this frame.
     *
     * This function computes the intensity (photon count) for each pixel in the frame
     * and returns it as a 2D array (lines × pixels).
     *
     * @param output [out] Pointer to the output array (will be allocated).
     * @param dim1 [out] Number of lines.
     * @param dim2 [out] Number of pixels per line.
     */
    void get_intensity(unsigned short **output, int *dim1, int *dim2);

    /*!
     * \brief Get the memory usage of this frame in bytes.
     *
     * @return Total memory usage in bytes.
     */
    size_t get_memory_usage_bytes() const;

};


#endif //TTTRLIB_CLSMFRAME_H
