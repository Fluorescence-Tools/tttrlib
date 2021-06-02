//
// Created by tpeulen on 10/24/20.
//

#include "include/CLSMPixel.h"
#include "include/CLSMLine.h"
#include "include/CLSMFrame.h"
#include "TTTRRange.h"

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