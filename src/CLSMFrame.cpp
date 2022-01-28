//
// Created by tpeulen on 10/24/20.
//

#include "include/CLSMPixel.h"
#include "include/CLSMLine.h"
#include "include/CLSMFrame.h"
#include "TTTRRange.h"

CLSMFrame::CLSMFrame(): TTTRRange() {}

CLSMFrame::CLSMFrame(size_t frame_start) : CLSMFrame()
{
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