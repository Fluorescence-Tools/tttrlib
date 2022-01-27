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

    void crop(
            int line_start, int line_stop,
            int pixel_start, int pixel_stop
    ){
        line_stop = std::min(line_stop, (int) size());
        line_start = std::max(0, line_start);

        #if VERBOSE_TTTRLIB
        std::clog << "Crop frame" << std::endl;
        std::clog << "-- Line range: " << line_start << ", " << line_stop << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
        #endif

        std::vector<CLSMLine*> lns;
        for(int i = 0; i < line_start; i++){
            delete lines[i];
        }
        for(int i = line_start; i < line_stop; i++){
            auto l = lines[i];
            l->crop(pixel_start, pixel_stop);
            lns.emplace_back(l);
        }
        for(int i = line_stop; i < size(); i++){
            delete lines[i];
        }
        lines = lns;
    }
};


#endif //TTTRLIB_CLSMFRAME_H
