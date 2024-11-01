#ifndef TTTRLIB_CLSMLINE_H
#define TTTRLIB_CLSMLINE_H

#include <vector>
#include "TTTR.h" /* TTTRRange */
#include "CLSMPixel.h"
#include "TTTRSelection.h"

class CLSMLine : public TTTRSelection{

    friend class CLSMImage;
    friend class CLSMFrame;

private:

    std::vector<CLSMPixel> pixels;
    int pixel_duration = -1;

public:

    /// Get the number of pixels per line a frame of the CLSMImage
    size_t size() final{
        return pixels.size();
    }

    std::vector<CLSMPixel>& get_pixels(){
        return pixels;
    }

    void set_pixel_duration(int v){
        this->pixel_duration = v;
    }

    unsigned long long get_pixel_duration(){
        if(pixel_duration < 0){
            return (size_t) (get_duration(_tttr) / size());
        } else{
            return pixel_duration;
        }
    }

    CLSMLine() = default;

    CLSMLine(const CLSMLine& old_line, bool fill=true) : TTTRSelection(old_line){
        // private attributes
        pixels.resize(old_line.pixels.size());
        pixels = old_line.pixels;
        if(!fill){
            for(auto &p: pixels) p.clear();
        }
    }

    explicit CLSMLine(unsigned int line_start){
        _tttr_indices.insert(line_start);
    }

    CLSMLine(int line_start, unsigned int n_pixel){
        _tttr_indices.insert(line_start);
        pixels.resize(n_pixel);
    }

    virtual ~CLSMLine() = default;

    void append(CLSMPixel& pixel){
        pixels.emplace_back(pixel);
    }

    CLSMPixel* operator[](unsigned int i_pixel){
        return &pixels[i_pixel];
    }

    void crop(int pixel_start, int pixel_stop){
        pixel_stop = std::min(pixel_stop, (int) size());
        pixel_start = std::max(0, pixel_start);

        #ifdef VERBOSE_TTTRLIB
        std::clog << "Crop line" << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
        #endif

        pixels.erase(pixels.begin() + pixel_stop, pixels.end());
        pixels.erase(pixels.begin(), pixels.begin() + pixel_start);
    }

    CLSMLine& operator+=(const CLSMLine& rhs){
        for(size_t i = 0; i < pixels.size(); i++){
            pixels[i] += rhs.pixels[i];
        }
        return *this;
    }

};

#endif //TTTRLIB_CLSMLINE_H
