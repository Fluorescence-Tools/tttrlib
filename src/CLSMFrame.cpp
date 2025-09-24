#include "include/CLSMPixel.h"
#include "include/CLSMLine.h"
#include "include/CLSMFrame.h"
#include "TTTRRange.h"
#include "include/Verbose.h"
#include <algorithm>
#include <vector>

CLSMFrame::CLSMFrame(): TTTRSelection() {}

CLSMFrame::CLSMFrame(size_t frame_start, size_t frame_stop, std::shared_ptr<TTTR> tttr) :
    TTTRSelection(static_cast<int>(frame_start), static_cast<int>(frame_stop), tttr)
{}

void CLSMFrame::crop(
        int line_start, int line_stop,
        int pixel_start, int pixel_stop
){
    line_stop = std::min(line_stop, (int) size());
    line_start = std::max(0, line_start);

    if (is_verbose()) {
        std::clog << "Crop frame" << std::endl;
        std::clog << "-- Line range: " << line_start << ", " << line_stop << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
    }

    // Crop each selected line's pixels
    for (int i = line_start; i < line_stop; ++i) {
        lines[i].crop(pixel_start, pixel_stop);
    }
    // Retain only the selected lines
    std::vector<CLSMLine> new_lines;
    new_lines.reserve(line_stop - line_start);
    for (int i = line_start; i < line_stop; ++i) {
        new_lines.emplace_back(std::move(lines[i]));
    }
    lines = std::move(new_lines);
}