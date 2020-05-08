#include <include/image.h>


CLSMFrame::CLSMFrame():
TTTRRange(),
n_lines(0)
{}


CLSMFrame::CLSMFrame(size_t frame_start) : CLSMFrame()
{
    CLSMFrame::_start = frame_start;
}


void CLSMFrame::append(CLSMLine * line){
    lines.emplace_back(line);
    n_lines++;
}

void CLSMImage::copy(const CLSMImage& p2, bool fill){
#if VERBOSE
    std::clog << "-- Copying image structure." << std::endl;
    if(fill){
        std::clog << "-- Copying pixel information." << std::endl;
    }
#endif
    // private attributes
#if VERBOSE
    std::clog << "-- Copying frame: " << std::flush;
    int i_frame = 0;
#endif
    for(auto f: p2.frames){
#if VERBOSE
        std::clog << i_frame++ << " " << std::flush;
#endif
        frames.emplace_back(new CLSMFrame(*f, fill));
    }
#if VERBOSE
    std::clog << std::endl;
#endif
    // public attributes
    marker_frame = p2.marker_frame;
    marker_line_start = p2.marker_line_start;
    marker_line_stop = p2.marker_line_stop;
    marker_event = p2.marker_event;
    n_frames = p2.n_frames;
    n_lines = p2.n_lines;
    n_pixel = p2.n_pixel;
#if VERBOSE
    std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
#endif
}

CLSMImage::CLSMImage(const CLSMImage& p2, bool fill){
    copy(p2, fill);
}


void CLSMImage::shift_line_start(
        int macro_time_shift
        ){
#if VERBOSE
    std::clog << "-- Shifting line start by [macro time clocks]: " << macro_time_shift << std::endl;
#endif
    for(auto &frame : get_frames()){
        for(auto &line: frame->get_lines()){
            line->shift_start_time(macro_time_shift);
        }
    }
}


CLSMImage::CLSMImage (
        TTTR *tttr_data,
        std::vector<int> marker_frame_start,
        int marker_line_start,
        int marker_line_stop,
        int marker_event_type,
        int pixel_per_line,
        std::string reading_routine,
        long long macro_time_shift,
        CLSMImage* source,
        bool fill,
        std::vector<int> channels,
        bool skip_before_first_frame_marker
) {
#if VERBOSE
    std::clog << "Initializing CLSM image" << std::endl;
#endif
    if(source != nullptr){
#if VERBOSE
        std::clog << "-- Copying data from other object" << std::endl;
#endif
        copy(*source, fill);
    } else{
#if VERBOSE
        std::clog << "-- Initializing new CLSM image..." << std::endl;
#endif
        this->marker_frame = marker_frame_start;
        this->marker_line_start = marker_line_start;
        this->marker_line_stop = marker_line_stop;
        this->marker_event = marker_event_type;
        this->n_pixel = pixel_per_line;

        /// map to translates string container types to int container types
        std::map<std::string, int> image_reading_routines = {
                {std::string("default"), 0},
                {std::string("SP8"), 1},
                {std::string("SP5"), 2}
        };
        if(tttr_data == nullptr){
            std::clog << "WARNING: No TTTR object provided" << std::endl;
            return;
        } else if(marker_frame_start.empty()){
            std::clog << "WARNING: No frame marker provided" << std::endl;
            return;
        } else if(tttr_data->n_records_read == 0){
            std::clog << "WARNING: No records in TTTR object" << std::endl;
            return;
        } else{
            switch (image_reading_routines[reading_routine]){
                case 0:
                    initialize_default(
                            tttr_data,
                            skip_before_first_frame_marker
                    );
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
#if VERBOSE
            std::clog << "-- Initial number of frames: " << n_frames << std::endl;
            std::clog << "-- Lines per frame: " << n_lines << std::endl;
#endif
            n_lines = (unsigned int) frames[0]->lines.size();
            n_frames = frames.size();
            remove_incomplete_frames();
            define_pixels_in_lines();
        }
    }
    if(tttr_data != nullptr){
        if(fill && !channels.empty()){
            fill_pixels(tttr_data, channels, false);
        }
    }
    if(macro_time_shift!=0) shift_line_start(macro_time_shift);
}

void CLSMImage::define_pixels_in_lines() {
    // TODO: This is the slowest step when creating a CLSM image
    // by improving here, a factor of two in speed could be possible.
    auto frame = frames.front();
    if(n_pixel == 0){
        n_pixel = frame->n_lines;
    }
    for(auto f: frames){
        for(int line_i = 0; line_i < f->n_lines; line_i++){
            auto l = f->lines[line_i];
            l->pixels.resize(n_pixel);
            l->n_pixel = n_pixel;
            for(size_t i=0; i<n_pixel; i++){
                auto pixel = new CLSMPixel();
                pixel->set_start_time(l->_start_time + i * l->pixel_duration);
                pixel->set_stop_time(l->_start_time + (i + 1) * l->pixel_duration);
                l->pixels[i] = pixel;
            }
        }
    }
#if VERBOSE
    std::clog << "-- Number of pixels per line: " << n_pixel << std::endl;
#endif
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
                    std::clog << "-- Found first frame at event: " << i_event << std::endl;
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
                auto line = frame->lines.back();
                line->_stop = i_event;
                line->update(tttr_data, false);
                continue;
            }
            else
                for(auto f: marker_frame){
                    if(f == tttr_data->micro_times[i_event])
                    {
                        frame->_stop = i_event;
                        frame->update(tttr_data, false);
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
        if(tttr_data->event_types[i_event] != marker_event) continue;
        else if(tttr_data->routing_channels[i_event] == marker_line_start){
            frames.back()->append(new CLSMLine(i_event));
        }
        else if(tttr_data->routing_channels[i_event] == marker_line_stop){
            auto line = frames.back()->lines.back();
            line->_stop = i_event;
            line->update(tttr_data, false);
        }
        else
            for (auto f: marker_frame){
                if(f == tttr_data->routing_channels[i_event]){
                    auto frame = frames.back();
                    // set values of old frame
                    frame->_stop = i_event;
                    frame->update(tttr_data, false);
                    auto new_frame = new CLSMFrame(i_event);
                    new_frame->append(new CLSMLine(i_event));
                    append(new_frame);
                    continue;
                }
            }
    }
}


void CLSMImage::initialize_default(
        TTTR* tttr_data,
        bool skip_before_first_frame_marker
){
#if VERBOSE
    std::clog << "-- Routine: default" << std::endl;
#endif
    size_t n_events = tttr_data->get_n_events();

    frames.clear();
    size_t i_event=0;
    // search first frame
    if(skip_before_first_frame_marker){
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
                    break;
                }
            }
        }
    }
    // Assume that the file starts with a "clean/complete" first frame
    append(new CLSMFrame(i_event));
    i_event++;
    for(; i_event < tttr_data->n_valid_events; i_event++){
        if(tttr_data->event_types[i_event] == marker_event){
            auto frame = frames.back();
            // Line marker
            if(tttr_data->routing_channels[i_event] == marker_line_start) {
                // take care of cases where no correct line stop was given
                // use line start as line stop
                if(!frame->lines.empty()){
                    auto line = frame->lines.back();
                    if(line->_stop<0){
                        line->_stop = i_event;
                        line->update(tttr_data, false);
                    }
                }
                // now actually add a new line
                frame->append(new CLSMLine(i_event));
            } else if(tttr_data->routing_channels[i_event] == marker_line_stop){
                auto line = frame->lines.back();
                line->_stop = i_event;
                line->update(tttr_data, false);
            } else{
                for(auto f: marker_frame){
                    if(f == tttr_data->routing_channels[i_event]) {
                        // set values of old frame
                        frame->_stop = i_event;
                        frame->update(tttr_data, false);
                        auto new_frame = new CLSMFrame(i_event);
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
#if VERBOSE
    std::clog << "-- Final number of frames: " << n_frames << std::endl;
#endif
}


void CLSMImage::clear_pixels() {
#if VERBOSE
    std::clog << "Clear pixels of photons" << std::endl;
#endif
    for(auto *frame : frames){
        for(auto line : frame->lines){
            for(auto pixel: line->pixels){
                pixel->_tttr_indices.clear();
            }
        }
    }
}


void CLSMImage::fill_pixels(
        TTTR* tttr_data,
        std::vector<int> channels,
        bool clear_pixel
        ) {
#if VERBOSE
    std::clog << "-- Filling pixels..." << std::endl;
    std::clog << "-- Channels: ";
    for(auto ch: channels){
        std::clog << ch << " ";
    }
    std::clog << std::endl;
    std::clog << "-- Assign photons to pixels" << std::endl;
#endif
    for(auto frame : frames){
        for(auto line : frame->lines){
            if(line->pixels.empty()){
                std::clog << "WARNING: Line without pixel." << std::endl;
                continue;
            }
            if(clear_pixel) for(auto &v:line->pixels) v->clear();
            auto pixel_duration = line->get_pixel_duration();
            size_t n_pixels_in_line = line->pixels.size();
            // iterate though events in the line
            for(auto event_i=line->_start; event_i < line->_stop; event_i++){
                if (tttr_data->event_types[event_i] != RECORD_PHOTON) continue;
                auto c = tttr_data->routing_channels[event_i];
                for(auto &ci : channels){
                    if(c == ci){
                        auto line_time = (tttr_data->macro_times[event_i] - line->_start_time);
                        auto pixel_nbr = line_time / pixel_duration;
                        if(pixel_nbr < n_pixels_in_line){
                            line->pixels[pixel_nbr]->append(event_i);
                        }
                        break;
                    }
                }
            }
            // update start, stop indices and times
            for(auto pixel : line->get_pixels()){
                pixel->update(tttr_data);
            }
        }
    }
}


void CLSMImage::get_intensity_image(
        unsigned int**output, int* dim1, int* dim2, int* dim3
        ){
    *dim1 = n_frames;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    size_t n_pixel_total = n_frames * n_pixel * n_lines;
#if VERBOSE
    std::clog << "Get intensity image" << std::endl;
    std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
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
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) +
                                   i_line * (n_pixel) +
                                   i_pixel;
                t[pixel_nbr] = pixel->_tttr_indices.size();
                t_pixel++;
                i_pixel++;
            }
            i_line++;
        }
        i_frame++;
    }
    *output = t;
}


void CLSMImage::get_fluorescence_decay_image(
        TTTR* tttr_data,
        unsigned char** output, int* dim1, int* dim2, int* dim3, int* dim4,
        int micro_time_coarsening,
        bool stack_frames
        ){
#if VERBOSE
    std::clog << "Get decay image" << std::endl;
#endif
    size_t nf = (stack_frames) ? 1 : n_frames;
    size_t n_tac = tttr_data->header->number_of_micro_time_channels / micro_time_coarsening;
    *dim1 = nf;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    *dim4 = (int) n_tac;

    size_t n_tac_total = nf * n_lines * n_pixel * n_tac;
    auto* t = (unsigned char*) calloc(n_tac_total, sizeof(unsigned char));
#if VERBOSE
    std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Number of micro time channels: " << n_tac << std::endl;
    std::clog << "-- Micro time coarsening factor: " << micro_time_coarsening << std::endl;
    std::clog << "-- Final number of micro time channels: " << n_tac << std::endl;
#endif
    size_t i_frame = 0;
    for(auto frame : frames){
        size_t i_line = 0;
        for(auto line : frame->lines){
            size_t i_pixel = 0;
            for(int pixel_i=0; pixel_i<n_pixel;pixel_i++){
                auto pixel = line->pixels[pixel_i];
                size_t pixel_nbr = i_frame * (n_lines * n_pixel * n_tac) +
                                   i_line  * (n_pixel * n_tac) +
                                   i_pixel * (n_tac);

                for(auto i : pixel->_tttr_indices){
                    size_t i_tac = tttr_data->micro_times[i] / micro_time_coarsening;
                    t[pixel_nbr + i_tac] += 1;
                }
                i_pixel++;
            }
            i_line++;
        }
        i_frame += !stack_frames;
    }
    *output = t;
}


void CLSMImage::get_fcs_image(
        float** output, int* dim1, int* dim2, int* dim3, int* dim4,
        TTTR* tttr,
        CLSMImage* clsm_other,
        const std::string correlation_method,
        const int n_bins, const int n_casc,
        const bool stack_frames,
        const bool normalized_correlation,
        const int min_photons
){
#if VERBOSE
    std::clog << "Get fluorescence correlation image" << std::endl;
#endif
    size_t nf = (stack_frames) ? 1 : n_frames;
    auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
    const int n_corr = corr.x_axis.size();

    size_t n_cor_total = nf * n_lines * n_pixel * n_corr;
    auto t = (float*) calloc(n_cor_total, sizeof(float));
#if VERBOSE
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Number of correlation blocks: " << n_casc << std::endl;
    std::clog << "-- Number of correlation bins per block: " << n_bins << std::endl;
    std::clog << "-- Number of correlation channels: " << n_corr << std::endl;
    std::clog << "-- Correlating... " << n_corr << std::endl;
#endif
    size_t o_frame = 0;
#pragma omp parallel for default(none) shared(tttr, o_frame, t, clsm_other)
    for(int i_frame=0; i_frame < n_frames; i_frame++){
        auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
        auto frame = frames[i_frame];
        auto other_frame = clsm_other->frames[i_frame];
        for(int i_line=0; i_line < n_lines; i_line++){
            auto line = frame->lines[i_line];
            auto other_line = other_frame->lines[i_line];
            for(int i_pixel=0; i_pixel < n_pixel; i_pixel++){
                auto pixel = line->pixels[i_pixel];
                auto other_pixel = other_line->pixels[i_pixel];
                if((pixel->_tttr_indices.size() > min_photons) && (other_pixel->_tttr_indices.size() > min_photons)){
                    auto tttr_1 = TTTR(
                            *tttr,
                            pixel->_tttr_indices.data(),
                            pixel->_tttr_indices.size(),
                            false
                    );
                    auto tttr_2 = TTTR(
                            *tttr,
                            other_pixel->_tttr_indices.data(),
                            other_pixel->_tttr_indices.size(),
                            false
                    );
                    corr.set_tttr(&tttr_1, &tttr_2);
                    double* correlation; int temp;
                    if(!normalized_correlation){
                        corr.get_corr(&correlation, &temp);
                    } else{
                        corr.get_corr_normalized(&correlation, &temp);
                    }
                    for(int i_corr = 0; i_corr < n_corr; i_corr++){
                        t[o_frame * (n_lines * n_pixel * n_corr) +
                          i_line  * (n_pixel * n_corr) +
                          i_pixel * (n_corr) +
                          i_corr
                        ] += (float) correlation[i_corr];
                    }
                }
            }
        }
        o_frame += !stack_frames;
    }
    *output = t;
    *dim1 = nf;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    *dim4 = (int) n_corr;
}


void CLSMImage::get_average_decay_of_pixels(
        TTTR* tttr_data,
        uint8_t* selection, int d_selection_1, int d_selection_2, int d_selection_3,
        unsigned int** output, int* dim1, int* dim2,
        int tac_coarsening,
        bool stack_frames
){
    size_t n_decays = stack_frames ? 1 : n_frames;
    size_t n_tac = tttr_data->header->number_of_micro_time_channels / tac_coarsening;
#ifdef VERBOSE
    std::clog << "Get decays:" << std::endl;
    std::clog << "-- Number of frames: " << n_frames << std::endl;
    std::clog << "-- Stack frames (true/false): " << stack_frames << std::endl;
    std::clog << "-- Number of decays: " << n_decays << std::endl;
    std::clog << "-- Number of micro time channels: " << tttr_data->header->number_of_micro_time_channels << std::endl;
    std::clog << "-- Micro time coarsening: " << tac_coarsening << std::endl;
    std::clog << "-- Resulting number of micro time channels: " << n_tac << std::endl;
#endif
    *dim1 = (int) n_decays;
    *dim2 = (int) n_tac;
    size_t n_tac_total = n_decays * n_tac;
    auto* t = (unsigned int*) calloc(n_tac_total, sizeof(unsigned int));
    if((d_selection_1 != n_frames) || (d_selection_2 != n_lines) || (d_selection_3 != n_pixel)){
        std::cerr << "Error: the dimensions of the selection ("
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
                        for (auto i : pixel->_tttr_indices) {
                            size_t i_tac = tttr_data->micro_times[i] / tac_coarsening;
                            t[w_frame * n_tac + i_tac] += 1;
                        }
                    }
                }
            }
            w_frame += !stack_frames;
        }
    }
    *output = t;
}


void CLSMImage::get_mean_micro_time_image(
        TTTR* tttr_data,
        double** output, int* dim1, int* dim2, int* dim3,
        int minimum_number_of_photons,
        bool stack_frames
){
    double dt = tttr_data->header->micro_time_resolution;
#if VERBOSE
    std::clog << "Get mean micro time image" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
    std::clog << "-- Micro time resolution [ns]: " << dt << std::endl;
    std::clog << "-- Computing stack of mean micro times " << std::endl;
#endif
    auto* t = (double *) malloc(n_frames * n_lines * n_pixel * sizeof(double));
    for(size_t i_frame = 0; i_frame < n_frames; i_frame++){
        for(size_t i_line = 0; i_line < n_lines; i_line++){
            for(size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++){
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line  * (n_pixel) + i_pixel;
                auto v = frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices;
                // calculate the mean arrival time iteratively
                double value = 0.0;
                if (v.size() > minimum_number_of_photons){
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
        *output = t;
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
                    auto n_photons = frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices.size();
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
        *output = r;
        free(t);
    }
}

void CLSMImage::get_phasor_image(
        float **output, int *dim1, int *dim2, int *dim3, int *dim4,
        TTTR *tttr_data,
        TTTR *tttr_irf,
        double frequency,
        int minimum_number_of_photons,
        bool stack_frames
) {
    double g_irf=1.0, s_irf=0.0;
    if(tttr_irf!= nullptr){
        std::vector<double> gs = Decay::phasor(
                tttr_irf->micro_times, tttr_irf->n_valid_events,
                frequency);
        g_irf = gs[0];
        s_irf = gs[1];
    }
    int o_frames = stack_frames? 1: n_frames;
    if(frequency<0){
        frequency = 1. / tttr_data->get_header().macro_time_resolution;
    }
    double factor = (2. * frequency * M_PI);
    std::clog << "get_phasor_image..." << std::endl;
    std::clog << "-- frequency [GHz]: " << frequency << std::endl;
    std::clog << "-- stack_frames: " << stack_frames << std::endl;
    std::clog << "-- minimum_number_of_photons: " << minimum_number_of_photons << std::endl;
    auto* t = (float *) calloc(o_frames * n_lines * n_pixel * 2, sizeof(float));
    for(int i_line=0; i_line < n_lines; i_line++){
        for(int i_pixel=0; i_pixel < n_pixel; i_pixel++){
            if(stack_frames){
                size_t pixel_nbr = i_line  * (n_pixel * 2) + i_pixel * 2;
                double sum=0.0, g_sum=0.0, s_sum = 0.0;
                for(int i_frame=0; i_frame < n_frames; i_frame++){
                    auto idxs = frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices;
                    sum += idxs.size();
                    for(auto &idx: idxs){
                        auto mt = tttr_data->micro_times[idx];
                        g_sum += std::cos(mt * factor);
                        s_sum += std::sin(mt * factor);
                    }
                }
                if(sum > minimum_number_of_photons){
                    double g_exp = g_sum / std::max(1., sum);
                    double s_exp = s_sum / std::max(1., sum);
                    t[pixel_nbr + 0] = Decay::compute_phasor_g_the(g_irf, s_irf,g_exp, s_exp);
                    t[pixel_nbr + 1] = Decay::compute_phasor_s_the(g_irf, s_irf,g_exp, s_exp);
                } else{
                    t[pixel_nbr + 0] = -1;
                    t[pixel_nbr + 1] = -1;
                }
            } else{
                for(int i_frame=0; i_frame < n_frames; i_frame++){
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel * 2) + i_line  * (n_pixel * 2) + i_pixel * 2;
                    auto idxs = frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices;
                    double sum=idxs.size(), g_sum=0.0, s_sum = 0.0;
                    for(auto &idx: idxs){
                        auto mt = tttr_data->micro_times[idx];
                        g_sum += std::cos(mt * factor);
                        s_sum += std::sin(mt * factor);
                    }
                    if(sum > minimum_number_of_photons){
                        double g_exp = g_sum / std::max(1., sum);
                        double s_exp = s_sum / std::max(1., sum);
                        t[pixel_nbr + 0] = Decay::compute_phasor_g_the(g_irf, s_irf,g_exp, s_exp);
                        t[pixel_nbr + 1] = Decay::compute_phasor_s_the(g_irf, s_irf,g_exp, s_exp);
                    } else{
                        t[pixel_nbr + 0] = -1;
                        t[pixel_nbr + 1] = -1;
                    }
                }
            }
        }
    }
    *dim1 = (int) o_frames;
    *dim2 = (int) n_lines;
    *dim3 = (int) n_pixel;
    *dim4 = (int) 2;
    *output = t;
}

void CLSMImage::get_mean_lifetime_image(
        TTTR* tttr_data,
        double** output, int* dim1, int* dim2, int* dim3,
        const int minimum_number_of_photons,
        TTTR* tttr_irf,
        double m0_irf,
        double m1_irf,
        bool stack_frames
){
    const double dt = tttr_data->header->micro_time_resolution;
#if VERBOSE
    std::clog << "Compute a mean lifetime image (Isenberg 1973)" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
    std::clog << "-- Micro time resolution [ns]: " << dt << std::endl;
    std::clog << "-- Computing stack of mean micro times " << std::endl;
#endif
    if(tttr_irf != nullptr){
        unsigned short *micro_times_irf; int n_micro_times_irf;
        tttr_irf->get_micro_time(&micro_times_irf, &n_micro_times_irf);
        m0_irf = n_micro_times_irf; m1_irf = 0;
        for(int i=0; i< n_micro_times_irf; i++) m1_irf += micro_times_irf[i];
    }
#if VERBOSE
    std::clog << "-- IRF m0: " << m0_irf << std::endl;
    std::clog << "-- IRF m1: " << m1_irf << std::endl;
#endif
    int o_frames = stack_frames? 1: n_frames;
    auto* t = (double *) calloc(o_frames * n_lines * n_pixel, sizeof(double));
#pragma omp parallel for default(none) shared(tttr_data, m0_irf, m1_irf, t, stack_frames)
    for(int i_line = 0; i_line < n_lines; i_line++){
        for(int i_pixel = 0; i_pixel < n_pixel; i_pixel++){
            if(stack_frames){
                size_t pixel_nbr = i_line  * (n_pixel) + i_pixel;
                double mu0 = 0.0; double mu1 = 0.0;
                for(auto &frame: frames){
                    mu0 += frame->lines[i_line]->pixels[i_pixel]->_tttr_indices.size();
                    for(auto &vi: frame->lines[i_line]->pixels[i_pixel]->_tttr_indices)
                        mu1 += tttr_data->micro_times[vi];
                }
                double g1 = mu0 / m0_irf;
                double g2 = (mu1 - g1 * m1_irf) / m0_irf;
                double tau1 = g2 / g1 * dt;
                t[pixel_nbr] = tau1 * (mu0 > minimum_number_of_photons);
            } else{
                for(int i_frame = 0; i_frame < n_frames; i_frame++){
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line  * (n_pixel) + i_pixel;
                    auto v = frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices;
                    if (v.size() > minimum_number_of_photons){
                        double mu0 = v.size(); double mu1 = 0.0;
                        for(auto &vi: v) mu1 += tttr_data->micro_times[vi];
                        double g1 = mu0 / m0_irf;
                        double g2 = (mu1 - g1 * m1_irf) / m0_irf;
                        double tau1 = g2 / g1 * dt;
                        t[pixel_nbr] = tau1;
                    }
                }
            }
        }
    }
    *dim1 = (int) o_frames;
    *dim2 = (int) n_lines;
    *dim3 = (int) n_pixel;
    *output = t;
}