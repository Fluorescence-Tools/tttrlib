//
// Created by thomas on 3/16/19.
//

#include <include/Image.h>


CLSMFrame::CLSMFrame():
TTTRRange(),
n_lines(0)
{}


CLSMFrame::~CLSMFrame(){
    for(auto line : lines){
        delete(line);
    }
}


CLSMFrame::CLSMFrame(unsigned int frame_start) :
CLSMFrame()
{
    CLSMFrame::start = frame_start;
}

void CLSMFrame::push_back(CLSMLine * line){
    lines.push_back(line);
    n_lines++;
}


std::vector<CLSMLine*> CLSMFrame::get_lines(){
    return lines;
};


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
        unsigned int marker_frame_start,
        unsigned int marker_line_start,
        unsigned int marker_line_stop,
        unsigned int marker_event_type,
        unsigned int pixel_per_line,
        unsigned int reading_routine
) : CLSMImage() {
    CLSMImage::marker_frame = marker_frame_start;
    CLSMImage::marker_line_start = marker_line_start;
    CLSMImage::marker_line_stop = marker_line_stop;
    CLSMImage::marker_event = marker_event_type;
    CLSMImage::n_pixel = pixel_per_line;
    switch (reading_routine){
        case 0:
            initialize(tttr_data);
            break;
        case 1:
            initialize_leica_sp8_ptu(tttr_data);
            break;
        default:
            initialize(tttr_data);
            break;
    }
}


CLSMImage::CLSMImage
(
        TTTR *tttr_data,
        unsigned int marker_frame_start,
        unsigned int marker_line_start,
        unsigned int marker_line_stop,
        unsigned int marker_event_type,
        unsigned int pixel_per_line
        )  : CLSMImage()
{
    CLSMImage::marker_frame = marker_frame_start;
    CLSMImage::marker_line_start = marker_line_start;
    CLSMImage::marker_line_stop = marker_line_stop;
    CLSMImage::marker_event = marker_event_type;
    CLSMImage::n_pixel = pixel_per_line;
    initialize(tttr_data);
}


void CLSMImage::push_back(CLSMFrame* frame) {
    frames.push_back(frame);
    n_frames++;
}

/*!
 *
 * @param tttr_data
 */
void CLSMImage::initialize_leica_sp8_ptu(TTTR *tttr_data){
    std::cout << "Initialize Leica SP8 PTU" << std::endl;

    // the start and stops are alternating
    unsigned int i_event = 0;
    size_t n_events = tttr_data->n_valid_events;

    // insert the first frame
    if(frames.empty()){
        push_back(new CLSMFrame(0));
    }

    for(i_event=0; i_event < n_events; i_event++){
        auto* frame = frames.back();
        if(tttr_data->routing_channels[i_event] == marker_event){
            if(tttr_data->micro_times[i_event] == marker_line_start){
                // if new start is found continue and add new line and break
                auto* line = new CLSMLine(i_event, n_pixel);
                frame->push_back(line);
                continue;
            }
            if(tttr_data->micro_times[i_event] == marker_line_stop){
                // is new stop is found the line is complete
                // fill all the line info and break search
                auto* line = frame->lines.back();
                line->stop = i_event;
                line->start_time = tttr_data->macro_times[line->start];
                line->stop_time = tttr_data->macro_times[line->stop];
                line->pixel_duration = (unsigned int) (line->get_duration() / n_pixel);
                continue;
            }
            if(tttr_data->micro_times[i_event] == marker_frame){
                // if new frame is found fill the info of the last frame
                // and create a new frame
                frame->stop = i_event;
                frame->start_time = tttr_data->macro_times[frame->start];
                frame->stop_time = tttr_data->macro_times[frame->stop];
                push_back(new CLSMFrame(i_event));
                continue;
            }
        }
    }
    n_lines = frames[0]->n_lines;
}


/*!
 * Initializes the frames, lines, and pixels of a CLSMImage.
 */
void CLSMImage::initialize(TTTR* tttr_data){

    short c;              // routing channel
    short e;              // event type

    // find the frame and line markers
    for(unsigned int i=0; i<tttr_data->n_valid_events; i++){
        e = tttr_data->event_types[i];

        // Identify events
        if(e == marker_event){
            c = tttr_data->routing_channels[i];
            // Frame marker
            if(c == marker_frame){
                if(frames.size()>1){
                    auto previous_frame = frames.back();
                    auto previous_line = previous_frame->lines.back();
                    previous_frame->stop = previous_line->stop;
                    previous_frame->stop_time = previous_line->stop_time;
                }
                auto next_frame = new CLSMFrame(i);
                next_frame->start_time = tttr_data->macro_times[i];
                push_back(next_frame);
            }

            // Line marker
            if(c == marker_line_start){
                auto frame = frames.back();
                auto line = new CLSMLine(i + 1, n_pixel);
                frame->push_back(line);

                // Find line end
                for(unsigned j=i + 1; j<tttr_data->n_valid_events; j++){
                    c = tttr_data->routing_channels[j];
                    e = tttr_data->event_types[j];
                    if( (e == marker_event) && (c == marker_line_stop) ){
                        line->stop = j;
                        break;
                    }
                }
                line->start_time = tttr_data->macro_times[line->start + 1];
                line->stop_time = tttr_data->macro_times[line->stop -1];
                line->pixel_duration = (unsigned int) (line->get_duration() / n_pixel);
            }
        }
    }
    // remove incomplete frames
    n_lines = (unsigned int) frames[0]->lines.size();

    n_frames = 0;
    size_t i_frame = 0;
    for(auto frame : frames){
        if(frame->lines.size() < n_lines){
            frames.erase(frames.begin() + i_frame);
            n_frames--;
        }
        i_frame++;
    }
}


/*!
 * Fills the tttr_indices of the pixels with the indices of the channels that are within a pixel
 *
 * @param channels
 */
void CLSMImage::fill_pixels(TTTR* tttr_data, std::vector<unsigned int> channels) {
    short c;              // routing channel
    short e;              // event type

    size_t pixel_nbr;
    for(auto frame : frames){
        for(auto line : frame->lines){
            for(unsigned int i=line->start; i < line->stop; i++){
                e = tttr_data->event_types[i];
                c = tttr_data->routing_channels[i];
                if (e == RECORD_PHOTON) {
                    for(auto ci : channels){
                        if(c == ci){
                            if(line->pixel_duration == 0){
                                break;
                            } else{
                                pixel_nbr = (tttr_data->macro_times[i] - line->start_time) / line->pixel_duration;
                                if(pixel_nbr < line->n_pixel){
                                    (line->pixels[pixel_nbr])->append(i);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    // Assign start and stops to pixels
    size_t n_tttr;
    for(auto frame : frames){
        auto lines = frame->get_lines();
        for(auto line : lines){
            auto pixels = line->get_pixels();
            for(auto pixel : pixels){
                n_tttr = pixel->tttr_indices.size();
                if(n_tttr > 0){
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


void CLSMImage::get_intensity_image(unsigned int**out, int* dim1, int* dim2, int* dim3){
    *dim1 = n_frames;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    auto* t = (unsigned int*) malloc(sizeof(unsigned int) * n_frames * n_pixel * n_lines);

    size_t i_line, i_pixel, i_frame;
    i_frame = 0;
    for(auto frame : frames){

        i_line = 0;
        for(auto line : frame->lines){

            i_pixel = 0;
            for(auto pixel : line->pixels){
                t[i_frame * (n_lines * n_pixel) +
                  i_line * (n_pixel) +
                  i_pixel
                ] = (unsigned short) pixel->tttr_indices.size();
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
        int tac_coarsening
        ){
    size_t n_tac = tttr_data->header->number_of_tac_channels / tac_coarsening;
    *dim1 = n_frames;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    *dim4 = (int) n_tac;

    auto* t = (unsigned char*) malloc(sizeof(char) * n_frames * n_lines * n_pixel * n_tac);

    unsigned int i_tac;
    size_t i_frame = 0;
    size_t i_line, i_pixel;
    for(auto frame : frames){
        i_line = 0;
        for(auto line : frame->lines){
            i_pixel = 0;
            for(auto pixel : line->pixels){
                for(auto i : pixel->tttr_indices){
                    i_tac = tttr_data->micro_times[i] / tac_coarsening;
                    if(i_tac < n_tac){
                        t[i_frame * (n_lines * n_pixel * n_tac) +
                          i_line  * (n_pixel * n_tac) +
                          i_pixel * (n_tac) +
                          i_tac
                        ] += 1;
                    }
                }
                i_pixel++;
            }
            i_line++;
        }
        i_frame++;
    }
    *out = t;
}


void CLSMImage::get_mean_tac_image(
        TTTR* tttr_data,
        double** out, int* dim1, int* dim2, int* dim3,
        int n_ph_min
){
    *dim1 = n_frames;
    *dim2 = n_lines;
    *dim3 = n_pixel;

    double mean_tac;
    auto* t = (double *) malloc(sizeof(double) * n_frames * n_lines * n_pixel);
    size_t i_frame = 0;
    size_t i_line, i_pixel;
    for(auto frame : frames){
        i_line = 0;
        for(auto line : frame->lines){
            i_pixel = 0;
            for(auto pixel : line->pixels){
                mean_tac = 0;
                for(auto i : pixel->tttr_indices){
                    mean_tac += tttr_data->micro_times[i];
                }
                if(pixel->tttr_indices.size() > n_ph_min){
                    mean_tac = mean_tac / pixel->tttr_indices.size();
                    t[i_frame * (n_lines * n_pixel) +
                      i_line  * (n_pixel) +
                      i_pixel
                    ] = mean_tac;
                }
                i_pixel++;
            }
            i_line++;
        }
        i_frame++;
    }
    *out = t;
}

