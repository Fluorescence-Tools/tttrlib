#ifndef TTTRLIB_CLSMLINE_H
#define TTTRLIB_CLSMLINE_H

#include <vector>
#include "TTTR.h" /* TTTRRange */
#include "CLSMPixel.h"
#include "TTTRRange.h"

class CLSMLine : public TTTRRange{

    friend class CLSMImage;
    friend class CLSMFrame;

private:

    std::vector<CLSMPixel> pixels;
    int pixel_duration = 1;

public:

    /// Get the number of pixels per line a frame of the CLSMImage
    size_t size() final{
        return pixels.size();
    }

    std::vector<CLSMPixel>& get_pixels(){
        return pixels;
    }

    unsigned long long get_pixel_duration(){
        return (size_t) (get_duration() / size());
    }

    CLSMLine() = default;

    CLSMLine(const CLSMLine& old_line, bool fill=true) : TTTRRange(old_line){
        // private attributes
        pixels.resize(old_line.pixels.size());
        pixels = old_line.pixels;
        if(!fill){
            for(auto &p: pixels) p.clear();
        }
    }

    explicit CLSMLine(unsigned int line_start){
        _start = line_start;
    }

    CLSMLine(int line_start,unsigned int n_pixel){
        this->_start = line_start;
        pixels.resize(n_pixel);
    }

    virtual ~CLSMLine(){
    }

    void append(CLSMPixel pixel){
        pixels.emplace_back(pixel);
        pixel_duration = (int) (get_duration() / size());
    }

    CLSMPixel* operator[](unsigned int i_pixel){
        return &pixels[i_pixel];
    }

    void crop(
            int pixel_start, int pixel_stop
    ){
        pixel_stop = std::min(pixel_stop, (int) size());
        pixel_start = std::max(0, pixel_start);

        #if VERBOSE_TTTRLIB
        std::clog << "Crop line" << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
        #endif

        pixels.erase(pixels.begin() + pixel_stop, pixels.end());
        pixels.erase(pixels.begin(), pixels.begin() + pixel_start);
    }

};

#endif //TTTRLIB_CLSMLINE_H
