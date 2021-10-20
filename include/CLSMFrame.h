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

    CLSMFrame(
            const CLSMFrame& old_frame,
            bool fill = false
    ) : TTTRRange(old_frame){
        // private attributes
        for(auto l: old_frame.lines){
            lines.emplace_back(new CLSMLine(*l));
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
};


#endif //TTTRLIB_CLSMFRAME_H
