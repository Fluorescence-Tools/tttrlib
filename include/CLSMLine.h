#include "include/Verbose.h"
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
    std::shared_ptr<TTTR> _tttr = nullptr;  // Only lines need TTTR reference, not pixels

public:

    /// Get the number of pixels per line a frame of the CLSMImage
    size_t size() const override final{
        return pixels.size();
    }

    std::vector<CLSMPixel>& get_pixels(){
        return pixels;
    }
    
    std::shared_ptr<TTTR> get_tttr(){
        return _tttr;
    }

    void set_tttr(std::shared_ptr<TTTR> tttr){
        _tttr = std::move(tttr);
    }

    void set_pixel_duration(int v){
        this->pixel_duration = v;
    }

    unsigned long long get_pixel_duration(){
        if(pixel_duration < 0){
            if(_tttr && size() > 0){
                return (size_t) (get_duration(_tttr) / size());
            }
            return 0;  // Return 0 if no TTTR data or no pixels
        } else{
            return pixel_duration;
        }
    }

    CLSMLine(){
        set_dense(false);
    }

    CLSMLine(const CLSMLine& old_line, bool fill=true) : TTTRSelection(old_line){
        // Copy private attributes
        pixel_duration = old_line.pixel_duration;
        _tttr = old_line._tttr;
        pixels.resize(old_line.pixels.size());
        pixels = old_line.pixels;
        if(!fill){
            for(auto &p: pixels) p.clear();
        }
    }

    explicit CLSMLine(unsigned int line_start){
        set_dense(false);
        set_range(static_cast<int>(line_start), static_cast<int>(line_start));
    }

    CLSMLine(int line_start, unsigned int n_pixel){
        set_dense(false);
        set_range(line_start, line_start);
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

        if (is_verbose()) {
        std::clog << "Crop line" << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
}

        pixels.erase(pixels.begin() + pixel_stop, pixels.end());
        pixels.erase(pixels.begin(), pixels.begin() + pixel_start);
    }

    CLSMLine& operator+=(const CLSMLine& rhs){
        for(size_t i = 0; i < pixels.size(); i++){
            pixels[i] += rhs.pixels[i];
        }
        return *this;
    }

    /*!
     * \brief Get the memory usage of this line in bytes.
     *
     * @return Total memory usage in bytes.
     */
    size_t get_memory_usage_bytes() const {
        size_t total = sizeof(CLSMLine);
        total += pixels.capacity() * sizeof(CLSMPixel);
        for (const auto& p : pixels) {
            total += p.get_memory_usage_bytes() - sizeof(CLSMPixel);
        }
        return total;
    }

};

#endif //TTTRLIB_CLSMLINE_H
