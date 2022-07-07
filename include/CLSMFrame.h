#ifndef TTTRLIB_CLSMFRAME_H
#define TTTRLIB_CLSMFRAME_H

#include <vector>

#include "TTTR.h" /* TTTRRange */
#include "CLSMPixel.h"
#include "CLSMLine.h"
#include "TTTRRange.h"

class CLSMFrame: public TTTRRange{

    friend class CLSMImage;

private:

    std::vector<CLSMLine*> lines;

public:

    std::vector<CLSMLine*> get_lines(){
        return lines;
    }

    /// Get the number of lines in the CLSMFrame
    size_t size() final{
        return lines.size();
    }

    CLSMFrame();

    /*!
     * Copy constructor
     *
     * @param fill if set to false the content of the pixels is not copied
     */
    CLSMFrame(const CLSMFrame& old_frame, bool fill = true) : TTTRRange(old_frame){
        for(auto l: old_frame.lines){
            lines.emplace_back(new CLSMLine(*l, fill));
        }
    }

    virtual ~CLSMFrame(){
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

    CLSMFrame& operator+=(const CLSMFrame& rhs){
        int i = 0;
        for(auto l: lines){
            *l += *rhs.lines[i];
        }
        return *this;
    }

    /*!
     * Crops a frame
     */
    void crop(
            int line_start, int line_stop,
            int pixel_start, int pixel_stop
    );

};


#endif //TTTRLIB_CLSMFRAME_H
