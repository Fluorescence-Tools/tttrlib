//
// Created by thomas on 3/16/19.
//

#ifndef TTTRLIB_IMAGE_H
#define TTTRLIB_IMAGE_H

#include <TTTR.h>
#include <vector>
#include <list>


class CLSMPixel : public TTTRRange{

    friend class CLSMLine;
    friend class CLSMImage;

private:
    /// Stores the TTTR indices of the pixel
    std::vector<unsigned int> tttr_indices;
    bool filled;


public:

    std::vector<unsigned int> get_tttr_indices(){
        return tttr_indices;
    }

    void append(unsigned int v){
        tttr_indices.push_back(v);
    }

    CLSMPixel():
    tttr_indices(0),
    filled(false)
    {};

    ~CLSMPixel(){
    };
};


class CLSMLine : public TTTRRange{

    friend class CLSMFrame;
    friend class CLSMImage;

private:
    std::vector<CLSMPixel*> pixels;
    unsigned int n_pixel;
    unsigned int pixel_duration;

public:

    std::vector<CLSMPixel*> get_pixels(){
        return pixels;
    }

    unsigned int get_pixel_duration(){
        return pixel_duration;
    }

    CLSMLine(){}

    ~CLSMLine(){
        for(auto pixel : pixels){
            delete(pixel);
        }
    }

    CLSMLine(unsigned int line_start, unsigned int n_pixel){
        start = line_start;
        CLSMLine::n_pixel = n_pixel;
        for(unsigned int i=0; i<n_pixel; i++){
            auto* pixel = new CLSMPixel();
            pixels.push_back(pixel);
        }
    }

};


class CLSMFrame: public TTTRRange{

    friend class CLSMImage;

private:
    std::vector<CLSMLine*> lines;

public:
    unsigned int n_lines;

    std::vector<CLSMLine*> get_lines();

    CLSMFrame();

    ~CLSMFrame();

    CLSMFrame(unsigned int frame_start);

    void push_back(CLSMLine * line);
};


class CLSMImage{

private:
    std::vector<CLSMFrame*> frames;

protected:
    void initialize(TTTR* tttr_data);
    void initialize_leica_sp8_ptu(TTTR *tttr_data);

public:
    unsigned int marker_frame;
    unsigned int marker_line_start;
    unsigned int marker_line_stop;
    int marker_event;

    unsigned int n_frames;
    unsigned int n_lines;
    unsigned int n_pixel;

    void fill_pixels(TTTR* tttr_data, std::vector<unsigned int> channels);

    std::vector<CLSMFrame*> get_frames(){
        return frames;
    }

    void get_intensity_image(unsigned int** out, int* dim1, int* dim2, int* dim3);

    void get_decay_image(
            TTTR* tttr_data,
            unsigned char** out, int* dim1, int* dim2, int* dim3, int* dim4,
            int tac_coarsening
    );

    void get_mean_tac_image(
            TTTR* tttr_data,
            double** out, int* dim1, int* dim2, int* dim3,
            int n_ph_min
    );

    void push_back(CLSMFrame* frame);

    // constructor
    CLSMImage();

    CLSMImage(
            TTTR *tttr_data,
            unsigned int marker_frame_start,
            unsigned int marker_line_start,
            unsigned int marker_line_stop,
            unsigned int marker_event_type,
            unsigned int n_pixel_per_line,
            unsigned int reading_routine
    );

    CLSMImage (
            TTTR *tttr_data,
            unsigned int marker_frame_start,
            unsigned int marker_line_start,
            unsigned int marker_line_stop,
            unsigned int marker_event_type,
            unsigned int pixel_per_line
    );

    ~CLSMImage(){
        for(auto frame : frames){
            delete(frame);
        }
    }
};


#endif //TTTRLIB_IMAGE_H
