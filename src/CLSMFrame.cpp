#include "include/CLSMPixel.h"
#include "include/CLSMLine.h"
#include "include/CLSMFrame.h"
#include "TTTRRange.h"

CLSMFrame::CLSMFrame(): TTTRRange() {}

CLSMFrame::CLSMFrame(size_t frame_start, size_t frame_stop, TTTR* tttr) :
CLSMFrame()
{
    _stop = frame_stop;
    _start = frame_start;
}

void CLSMFrame::append(CLSMLine * line){
    lines.emplace_back(line);
}

void CLSMFrame::crop(
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
    for(size_t i = 0; i < line_start; i++){
        delete lines[i];
    }
    for(size_t i = line_start; i < line_stop; i++){
        auto l = lines[i];
        l->crop(pixel_start, pixel_stop);
        lns.emplace_back(l);
    }
    for(size_t i = line_stop; i < size(); i++){
        delete lines[i];
    }
    lines = lns;
}