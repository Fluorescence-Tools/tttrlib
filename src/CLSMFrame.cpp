#include "include/CLSMPixel.h"
#include "include/CLSMLine.h"
#include "include/CLSMFrame.h"
#include "TTTRRange.h"
#include "include/Verbose.h"

CLSMFrame::CLSMFrame(): TTTRSelection() {}

CLSMFrame::CLSMFrame(size_t frame_start, size_t frame_stop, std::shared_ptr<TTTR> tttr) :
    TTTRSelection(static_cast<int>(frame_start), static_cast<int>(frame_stop))
{
    set_tttr(std::move(tttr));
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

    if (is_verbose()) {
    std::clog << "Crop frame" << std::endl;
    std::clog << "-- Line range: " << line_start << ", " << line_stop << std::endl;
    std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
}

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

void CLSMFrame::get_intensity(unsigned short **output, int *dim1, int *dim2) {
    if (lines.empty()) {
        *dim1 = 0;
        *dim2 = 0;
        *output = nullptr;
        return;
    }
    
    size_t n_lines = lines.size();
    size_t n_pixels = lines[0]->pixels.size();
    
    *dim1 = static_cast<int>(n_lines);
    *dim2 = static_cast<int>(n_pixels);
    
    size_t n_total = n_lines * n_pixels;
    auto *intensity = (unsigned short *) malloc(n_total * sizeof(unsigned short));
    
    // Fill intensity array
    for (size_t i_line = 0; i_line < n_lines; i_line++) {
        auto &line = lines[i_line];
        for (size_t i_pixel = 0; i_pixel < n_pixels; i_pixel++) {
            auto &pixel = line->pixels[i_pixel];
            size_t idx = i_line * n_pixels + i_pixel;
            intensity[idx] = static_cast<unsigned short>(pixel.size());
        }
    }
    
    *output = intensity;
}

size_t CLSMFrame::get_memory_usage_bytes() const {
    size_t total = sizeof(CLSMFrame);
    
    // Lines vector overhead
    total += lines.capacity() * sizeof(CLSMLine*);
    
    // Memory for each line
    for (const auto& line : lines) {
        if (line != nullptr) {
            total += line->get_memory_usage_bytes();
        }
    }
    
    return total;
}