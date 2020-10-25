//
// Created by tpeulen on 10/24/20.
//

#include "clsm/CLSMPixel.h"
#include "clsm/CLSMLine.h"
#include "clsm/CLSMFrame.h"

CLSMFrame::CLSMFrame():
TTTRRange()
{}

CLSMFrame::CLSMFrame(size_t frame_start) : CLSMFrame()
{
    _start = frame_start;
}

void CLSMFrame::append(CLSMLine * line){
    lines.emplace_back(line);
}