/****************************************************************************
 * Copyright (C) 2020 by Thomas-Otavio Peulen                               *
 *                                                                          *
 * This file is part of the library tttrlib.                                *
 *                                                                          *
 *   tttrlib is free software: you can redistribute it and/or modify it     *
 *   under the terms of the MIT License.                                    *
 *                                                                          *
 *   tttrlib is distributed in the hope that it will be useful,             *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                   *
 *                                                                          *
 ****************************************************************************/

#include <include/image.h>


CLSMFrame::CLSMFrame():
TTTRRange(),
n_lines(0)
{}


CLSMFrame::CLSMFrame(size_t frame_start) :
CLSMFrame()
{
    CLSMFrame::start = frame_start;
}


void CLSMFrame::append(CLSMLine * line){
    lines.emplace_back(line);
    n_lines++;
}


CLSMImage::CLSMImage():
marker_frame(0),
marker_line_start(0),
marker_line_stop(0),
marker_event(1),
n_frames(0),
n_pixel(0),
n_lines(0)
{
}


CLSMImage::CLSMImage (
        TTTR *tttr_data,
        std::vector<unsigned int> marker_frame_start,
        unsigned int marker_line_start,
        unsigned int marker_line_stop,
        unsigned int marker_event_type,
        unsigned int pixel_per_line,
        std::string reading_routine
) : CLSMImage() {
    CLSMImage::marker_frame = marker_frame_start;
    CLSMImage::marker_line_start = marker_line_start;
    CLSMImage::marker_line_stop = marker_line_stop;
    CLSMImage::marker_event = marker_event_type;
    CLSMImage::n_pixel = pixel_per_line;

    /// map to translates string container types to int container types
    std::map<std::string, int> image_reading_routines = {
            {std::string("default"), 0},
            {std::string("SP8"), 1},
            {std::string("SP5"), 2}
    };
#if VERBOSE
    std::clog << "Initializing CLSM image" << std::endl;
#endif
    switch (image_reading_routines[reading_routine]){
        case 0:
            initialize_default(tttr_data);
            break;
        case 1:
            initialize_leica_sp8_ptu(tttr_data);
            break;
        case 2:
            initialize_leica_sp5_ptu(tttr_data);
            break;
        default:
            initialize_default(tttr_data);
            break;
    }
    n_lines = (unsigned int) frames[0]->lines.size();
#if VERBOSE
    std::clog << "-- Initial number of frames: " << n_frames << std::endl;
    std::clog << "-- Lines per frame: " << n_lines << std::endl;
#endif
    remove_incomplete_frames();
    define_pixels_in_lines();
}

void CLSMImage::define_pixels_in_lines() {
    auto frame = frames.front();
    if(n_pixel == 0){
        n_pixel = frame->n_lines;
    }
    for(auto f: frames){
        for(auto l: f->lines){
            for(size_t i=0; i<n_pixel; i++){
                auto* pixel = new CLSMPixel();
                l->pixels.emplace_back(pixel);
            }
            l->n_pixel = n_pixel;
        }
    }
}


void CLSMImage::append(CLSMFrame* frame) {
    frames.emplace_back(frame);
    n_frames++;
}

/*!
 *
 * @param tttr_data
 */
void CLSMImage::initialize_leica_sp8_ptu(
        TTTR *tttr_data
)
{
#if VERBOSE
    std::clog << "-- Routine: Leica SP8 PTU" << std::endl;
    std::clog << "-- Number of events: " << tttr_data->n_valid_events << std::endl;
#endif
    size_t n_events = tttr_data->n_valid_events;
    // find the first frame
    frames.clear();
    size_t i_event=0;
    for(; i_event < n_events; i_event++){
        bool found_frame = false;
        if(tttr_data->routing_channels[i_event] == marker_event){
            for(auto f: marker_frame){
                if(f == tttr_data->micro_times[i_event])
                {
#if VERBOSE
                    std::clog << "-- Found first frame at event: "  << i_event << std::endl;
#endif
                    found_frame = true;
                    break;
                }
            }
        }
        if (found_frame){
            append(new CLSMFrame(i_event));
            i_event++;
            break;
        }
    }

    // iterate events and append new lines / frames
    for(; i_event < n_events; i_event++){
        auto* frame = frames.back();
        if(tttr_data->routing_channels[i_event] == marker_event){
            if(tttr_data->micro_times[i_event] == marker_line_start){
                frame->append(new CLSMLine(i_event));
                continue;
            }
            else if(tttr_data->micro_times[i_event] == marker_line_stop){
                auto* line = frame->lines.back();
                line->stop = i_event;
                line->start_time = tttr_data->macro_times[line->start];
                line->stop_time = tttr_data->macro_times[line->stop];
                continue;
            }
            else
                for(auto f: marker_frame){
                    if(f == tttr_data->micro_times[i_event])
                    {
                        frame->stop = i_event;
                        frame->start_time = tttr_data->macro_times[frame->start];
                        frame->stop_time = tttr_data->macro_times[frame->stop];
                        append(new CLSMFrame(i_event));
                        continue;
                    }
                }
        }
    }
}


void CLSMImage::initialize_leica_sp5_ptu(
        TTTR *tttr_data
)
{
#if VERBOSE
    std::clog << "-- Routine: Leica SP5 PTU" << std::endl;
#endif
    size_t n_events = tttr_data->get_n_events();

    // search first frame
    frames.clear();
    size_t i_event=0;
    for(; i_event < n_events; i_event++){
        bool found_frame = false;
        for (auto f: marker_frame){
            if(f == tttr_data->routing_channels[i_event]){
#if VERBOSE
                std::clog << "-- Found first frame at event: "  << i_event << std::endl;
#endif
                found_frame = true;
                break;
            }
        }
        if (found_frame){
            append(new CLSMFrame(i_event));
            i_event++;
            break;
        }
    }

    // iterate events and append new lines / frames
    for(; i_event < n_events; i_event++){
        if(tttr_data->event_types[i_event] == marker_event){
            auto frame = frames.back();
            if(tttr_data->routing_channels[i_event] == marker_line_start){
                frame->append(new CLSMLine(i_event));
                continue;
            }
            else if(tttr_data->routing_channels[i_event] == marker_line_stop){
                auto line = frame->lines.back();
                line->start_time = tttr_data->macro_times[line->start];
                line->stop = i_event;
                line->stop_time = tttr_data->macro_times[line->stop];
                continue;
            }
            else
                for (auto f: marker_frame){
                    if(f == tttr_data->routing_channels[i_event]){
                        // set values of old frame
                        frame->stop = i_event;
                        frame->start_time = tttr_data->macro_times[frame->start];
                        frame->stop_time = tttr_data->macro_times[frame->stop];

                        auto new_frame = new CLSMFrame(i_event);
                        new_frame->append(new CLSMLine(i_event));
                        append(new_frame);
                        continue;
                    }
                }
        }
    }
}


void CLSMImage::initialize_default(TTTR* tttr_data){
#if VERBOSE
    std::clog << "-- Routine: default" << std::endl;
#endif
    size_t n_events = tttr_data->get_n_events();

    // search first frame
    frames.clear();
    size_t i_event=0;
    for(; i_event < n_events; i_event++){
        if(tttr_data->event_types[i_event] == marker_event){
            bool found_frame = false;
            for (auto f: marker_frame){
                if(f == tttr_data->routing_channels[i_event]){
#if VERBOSE
                    std::clog << "-- Found first frame at event: "  << i_event << std::endl;
#endif
                    found_frame = true;
                    break;
                }
            }
            if (found_frame){
                append(new CLSMFrame(i_event));
                i_event++;
                break;
            }
        }
    }

    for(; i_event < tttr_data->n_valid_events; i_event++){
        if(tttr_data->event_types[i_event] == marker_event){
            auto frame = frames.back();
            // Line marker
            if(tttr_data->routing_channels[i_event] == marker_line_start) {
                frame->append(new CLSMLine(i_event));
            } else if(tttr_data->routing_channels[i_event] == marker_line_stop){
                auto line = frame->lines.back();
                line->stop = i_event;
                line->start_time = tttr_data->macro_times[line->start];
                line->stop_time = tttr_data->macro_times[line->stop];
            } else{
                for(auto f: marker_frame){
                    if(f == tttr_data->routing_channels[i_event]) {
                        // set values of old frame
                        frame->stop = i_event;
                        frame->start_time = tttr_data->macro_times[frame->start];
                        frame->stop_time = tttr_data->macro_times[frame->stop];
//
                        auto new_frame = new CLSMFrame(i_event);
//                        new_frame->append(new CLSMLine(i_event));
                        append(new_frame);
                        continue;
                    }
                }
            }
        }
    }
}

void CLSMImage::remove_incomplete_frames(){
    // remove incomplete frames
#if VERBOSE
    std::clog << "-- Removing incomplete frames" << std::endl;
#endif
    n_frames = frames.size();
    size_t i_frame = 0;
    for(auto frame : frames){
        if(frame->lines.size() < n_lines){
#if VERBOSE
            std::clog << "WARNING: Incomplete frame with " << frame->lines.size() << " lines." << std::endl;
#endif
            frames.erase(frames.begin() + i_frame);
            n_frames--;
        }
        i_frame++;
    }
    frames.resize(n_frames);
    std::clog << "-- Final number of frames: " << n_frames << std::endl;
}


void CLSMImage::clear_pixels() {
#if VERBOSE
    std::clog << "Clear pixels of photons" << std::endl;
#endif
    for(auto frame : frames){
        for(auto line : frame->lines){
            for(auto pixel: line->pixels){
                pixel->tttr_indices.clear();
            }
        }
    }
}


void CLSMImage::fill_pixels(
        TTTR* tttr_data,
        std::vector<unsigned int> channels
        ) {
#if VERBOSE
    std::clog << "Fill pixels with photons" << std::endl;
    std::clog << "-- Channels: ";
#endif
    for(auto ch: channels){
        std::clog << ch << " ";
    }
    std::clog << std::endl;
#if VERBOSE
    std::clog << "-- Assign photons to pixels" << std::endl;
#endif
    for(auto frame : frames){
        for(auto line : frame->lines){
            if(line->pixels.empty()){
                std::clog << "WARNING: Line without pixel." << std::endl;
                continue;
            }
            auto pixel_duration = line->get_pixel_duration();
            // iterate though events in the line
            for(auto event_i=line->start; event_i < line->stop; event_i++){
                if (tttr_data->event_types[event_i] == RECORD_PHOTON)
                {
                    auto c = tttr_data->routing_channels[event_i];
                    for(auto ci : channels){
                        if(c == ci){
                            auto line_time = (tttr_data->macro_times[event_i] - line->start_time);
                            auto pixel_nbr = line_time / pixel_duration;
                            if(pixel_nbr < line->pixels.size()){
                                line->pixels[pixel_nbr]->append(event_i);
                            }
                            break;
                        }
                    }
                }
            }
            // Assign start and stops to pixels
            for(auto pixel : line->get_pixels()){
                if(!pixel->tttr_indices.empty()){
                    pixel->start = pixel->tttr_indices[0];
                    pixel->stop = pixel->tttr_indices[pixel->tttr_indices.size() - 1];
                    pixel->start_time = tttr_data->macro_times[pixel->start];
                    pixel->stop_time = tttr_data->macro_times[pixel->stop];
                    pixel->filled = true;
                } else{
                    pixel->filled = false;
                }
            }
        }
    }
}


void CLSMImage::get_intensity_image(
        unsigned int**out, int* dim1, int* dim2, int* dim3
        ){
    *dim1 = n_frames;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    size_t n_pixel_total = n_frames * n_pixel * n_lines;
#if VERBOSE
    std::clog << "Get intensity image" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Total number of pixels: " << n_pixel_total << std::endl;
#endif
    auto* t = (unsigned int*) calloc(n_pixel_total+1, sizeof(unsigned int));
    size_t i_frame = 0;
    size_t t_pixel = 0;
    for(auto frame : frames){
        size_t i_line = 0;
        for(auto line : frame->lines){
            size_t i_pixel = 0;
            for(auto pixel : line->pixels){
                t[i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel] = (unsigned int) pixel->tttr_indices.size();
                t_pixel++;
                i_pixel++;
            }
            i_line++;
        }
        i_frame++;
    }
    *out = t;
}


void CLSMImage::get_decay_image(
        TTTR* tttr_data,
        unsigned char** out, int* dim1, int* dim2, int* dim3, int* dim4,
        int tac_coarsening,
        bool stack_frames
        ){
#if VERBOSE
    std::clog << "Get decay image" << std::endl;
#endif
    size_t nf = (stack_frames) ? 1 : n_frames;
    size_t n_tac = tttr_data->header->number_of_tac_channels / tac_coarsening;
    *dim1 = nf;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    *dim4 = (int) n_tac;

    size_t n_tac_total = nf * n_lines * n_pixel * n_tac;
    auto* t = (unsigned char*) calloc(n_tac_total, sizeof(unsigned char));
#if VERBOSE
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Number of micro time channels: " << n_tac << std::endl;
    std::clog << "-- Micro time coarsening factor: " << tac_coarsening << std::endl;
    std::clog << "-- Final number of micro time channels: " << n_tac << std::endl;
#endif
    size_t i_frame = 0;
    for(auto frame : frames){
        size_t i_line = 0;
        for(auto line : frame->lines){
            size_t i_pixel = 0;
            for(auto pixel : line->pixels){
                for(auto i : pixel->tttr_indices){
                    size_t i_tac = tttr_data->micro_times[i] / tac_coarsening;
                    t[i_frame * (n_lines * n_pixel * n_tac) +
                      i_line  * (n_pixel * n_tac) +
                      i_pixel * (n_tac) +
                      i_tac
                    ] += 1;
                }
                i_pixel++;
            }
            i_line++;
        }
        i_frame += !stack_frames;
    }
    *out = t;
}


void CLSMImage::get_decays(
        TTTR* tttr_data,
        uint8_t* selection, int d_selection_1, int d_selection_2, int d_selection_3,
        unsigned int** out, int* dim1, int* dim2,
        int tac_coarsening,
        bool stack_frames
){
    size_t n_decays = stack_frames ? 1 : n_frames;
    size_t n_tac = tttr_data->header->number_of_tac_channels / tac_coarsening;
#ifdef DEBUG
    std::clog << "Get decays:" << std::endl;
    std::clog << "-- Number of frames: " << n_frames << std::endl;
    std::clog << "-- Stack frames (true/false): " << stack_frames << std::endl;
    std::clog << "-- Number of decays: " << n_decays << std::endl;
    std::clog << "-- Number of micro time channels: " << tttr_data->header->number_of_tac_channels << std::endl;
    std::clog << "-- Micro time coarsening: " << tac_coarsening << std::endl;
    std::clog << "-- Resulting number of micro time channels: " << n_tac << std::endl;
#endif
    *dim1 = (int) n_decays;
    *dim2 = (int) n_tac;
    size_t n_tac_total = n_decays * n_tac;
    auto* t = (unsigned int*) calloc(n_tac_total, sizeof(unsigned int));
    if((d_selection_1 != n_frames) || (d_selection_2 != n_lines) || (d_selection_3 != n_pixel)){
        std::cerr
                << "Error: the dimensions of the selection ("
                << n_frames << ", " << n_lines << ", " << n_pixel
                << ") does not match the CLSM image dimensions.";
    } else{
        size_t w_frame = 0;
        for(size_t i_frame=0; i_frame < n_frames; i_frame++) {
            auto frame = frames[i_frame];
            for (size_t i_line = 0; i_line < n_lines; i_line++) {
                auto line = frame->lines[i_line];
                for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                    auto pixel = line->pixels[i_pixel];
                    if (selection[i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel]){
                        for (auto i : pixel->tttr_indices) {
                            size_t i_tac = tttr_data->micro_times[i] / tac_coarsening;
                            t[w_frame * n_tac + i_tac] += 1;
                        }
                    }
                }
            }
            w_frame += !stack_frames;
        }
    }
    *out = t;
}


void CLSMImage::get_mean_tac_image(
        TTTR* tttr_data,
        double** out, int* dim1, int* dim2, int* dim3,
        int n_ph_min,
        bool stack_frames
){
    double dt = tttr_data->header->micro_time_resolution;
#if VERBOSE
    std::clog << "Get mean micro time image" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Minimum number of photos: " << n_ph_min << std::endl;
    std::clog << "-- Micro time resolution [ns]: " << dt << std::endl;
    std::clog << "-- Computing stack of mean micro times " << std::endl;
#endif
    auto* t = (double *) malloc(n_frames * n_lines * n_pixel * sizeof(double));
    for(size_t i_frame = 0; i_frame < n_frames; i_frame++){
        for(size_t i_line = 0; i_line < n_lines; i_line++){
            for(size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++){
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line  * (n_pixel) + i_pixel;
                auto v = frames[i_frame]->lines[i_line]->pixels[i_pixel]->tttr_indices;
                // calculate the mean arrival time iteratively
                double value = 0.0;
                if (v.size() > n_ph_min){
                    double i = 1.0;
                    for(auto event_i: v){
                        value = value + 1. / (i + 1.) * (double) (tttr_data->micro_times[event_i] - value);
                        i++;
                    }
                }
                t[pixel_nbr] = value * dt;
            }
        }
    }
    if(!stack_frames) {
        *dim1 = (int) n_frames;
        *dim2 = (int) n_lines;
        *dim3 = (int) n_pixel;
        *out = t;
    } else{
        // average over the arrival times
        int w_frame = 1;
#if VERBOSE
        std::clog << "-- Compute photon weighted average over frames" << std::endl;
#endif
        auto* r = (double *) malloc(sizeof(double) * w_frame * n_lines * n_pixel);
        for(size_t i_line = 0; i_line < n_lines; i_line++){
            for(size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++){
                size_t pixel_nbr = i_line  * n_pixel + i_pixel;
                // average the arrival times over the frames
                r[pixel_nbr] = 0.0;
                int n_photons_total = 0;
                for(size_t i_frame = 0; i_frame < n_frames; i_frame++){
                    auto n_photons = frames[i_frame]->lines[i_line]->pixels[i_pixel]->tttr_indices.size();
                    n_photons_total += n_photons;
                    r[pixel_nbr] += n_photons * t[i_frame * (n_lines * n_pixel) + i_line  * (n_pixel) + i_pixel];
                }
                r[pixel_nbr] /= std::max(1, n_photons_total);
                //if(n_photons_total > 0)
                //    r[pixel_nbr] /= n_photons_total;
            }
        }
        *dim1 = (int) w_frame;
        *dim2 = (int) n_lines;
        *dim3 = (int) n_pixel;
        *out = r;
        free(t);
    }
}

