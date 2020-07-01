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
#if VERBOSE
    std::clog << "-- Linking TTTR: " << std::flush;
#endif
    tttr = p2.tttr;
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
        std::shared_ptr<TTTR> tttr_data,
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
                            tttr_data.get(),
                            skip_before_first_frame_marker
                    );
                    break;
                case 1:
                    initialize_leica_sp8_ptu(tttr_data.get());
                    break;
                case 2:
                    initialize_leica_sp5_ptu(tttr_data.get());
                    break;
                default:
                    initialize_default(tttr_data.get());
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
            fill_pixels(tttr_data.get(), channels, false);
        }
    }
    if(macro_time_shift!=0) shift_line_start(macro_time_shift);
    tttr = tttr_data;
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
    std::clog << "-- Channels: "; for(auto ch: channels) std::clog << ch << " "; std::clog << std::endl;
    std::clog << "-- Clear pixel before fill: " << clear_pixel << std::endl;
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
                        if(pixel_nbr < n_pixels_in_line) line->pixels[pixel_nbr]->append(event_i);
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
        std::shared_ptr<TTTR> tttr,
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
    auto corr = Correlator(tttr.get(), correlation_method, n_bins, n_casc);
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
        auto corr = Correlator(tttr.get(), correlation_method, n_bins, n_casc);
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
        uint8_t* mask, int dmask1, int dmask2, int dmask3,
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
    if((dmask1 != n_frames) || (dmask2 != n_lines) || (dmask3 != n_pixel)){
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
                    if (mask[i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel]){
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
        std::vector<double> gs = Phasor::compute_phasor_all(
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
#if VERBOSE
    std::clog << "GET_PHASOR_IMAGE..." << std::endl;
    std::clog << "-- frequency [GHz]: " << frequency << std::endl;
    std::clog << "-- stack_frames: " << stack_frames << std::endl;
    std::clog << "-- minimum_number_of_photons: " << minimum_number_of_photons << std::endl;
#endif
    auto* t = (float *) calloc(o_frames * n_lines * n_pixel * 2, sizeof(float));
    for(int i_line=0; i_line < n_lines; i_line++){
        for(int i_pixel=0; i_pixel < n_pixel; i_pixel++){
            if(stack_frames){
                std::vector<int> idxs = {};
                size_t pixel_nbr = i_line  * (n_pixel * 2) + i_pixel * 2;
                for(int i_frame=0; i_frame < n_frames; i_frame++){
                    auto n = frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices;
                    idxs.insert(idxs.end(), n.begin(), n.end());
                }
                auto r = Phasor::compute_phasor(
                        tttr_data->micro_times,
                        idxs,
                        frequency,
                        minimum_number_of_photons,
                        g_irf, s_irf
                );
                t[pixel_nbr + 0] = r[0];
                t[pixel_nbr + 1] = r[1];
            } else{
                for(int i_frame=0; i_frame < n_frames; i_frame++){
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel * 2) + i_line  * (n_pixel * 2) + i_pixel * 2;
                    auto r = Phasor::compute_phasor(
                            tttr_data->micro_times,
                            frames[i_frame]->lines[i_line]->pixels[i_pixel]->_tttr_indices,
                            frequency,
                            minimum_number_of_photons,
                            g_irf, s_irf
                    );
                    t[pixel_nbr + 0] = r[0];
                    t[pixel_nbr + 1] = r[1];
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


void CLSMImage::get_roi(
        double** output, int* dim1, int* dim2, int* dim3,
        CLSMImage* clsm,
        std::vector<int> x_range,
        std::vector<int> y_range,
        std::string subtract_average,
        double background,
        bool clip, double clip_max, double clip_min,
        double *images, int n_frames, int n_lines, int n_pixel,
        uint8_t *mask, int dmask1, int dmask2, int dmask3,
        std::vector<int> selected_frames
) {
#if VERBOSE
    std::clog << "CREATE ROI" << std::endl;
#endif
    // determine the total number of frames, lines, and pixel in the input
    int nf, nl, np; // the number frames, lines, and pixel in the input
    TTTR* tttr_data = nullptr;
    if(clsm != nullptr){
#if VERBOSE
        std::clog << "-- Using CLSM/TTTR data" << std::endl;
#endif
        if(clsm->tttr == nullptr)
            std::cerr << "WARNING: CLSM has no TTTR data associated." << std::endl;
        tttr_data = clsm->tttr.get();
        nf = clsm->n_frames;
        nl = clsm->n_lines;
        np = clsm->n_pixel;
    } else if(
            (images != nullptr) &&
            (n_frames > 0) &&
            (n_lines > 0) &&
            (n_pixel > 0)
    ){
#if VERBOSE
        std::clog << "-- Using image array input" << std::endl;
#endif
        nf = n_frames;
        nl = n_lines;
        np = n_pixel;
    } else{
        std::cerr << "ERROR: No input data specified!" << std::endl;
    }
#if VERBOSE
    std::clog << "-- Input number of frames: " << nf << std::endl;
    std::clog << "-- Input number of lines: " << nl << std::endl;
    std::clog << "-- Input number of pixel: " << np << std::endl;
#endif
    // Determine mask
    bool use_mask =
            (dmask1 > 0) &&
            (dmask2 > 0) &&
            (dmask3 > 0) &&
            (mask != nullptr);

    // determine ROI range
    // if no stop specified (-1) use n_pixel, n_lines as stop
    int start_x = x_range[0]; int stop_x = x_range[1];
    int start_y = y_range[0]; int stop_y = y_range[1];
    stop_x = (stop_x<0)? np: stop_x % np;
    stop_y = (stop_y<0)? nl: stop_y % nl;

    // Compute the shape of the output array
    int ncol_roi = stop_x - start_x;
    int nrows_roi = stop_y - start_y;
    int nframes_roi = selected_frames.size();
    int pixel_in_roi = nrows_roi * ncol_roi;

#if VERBOSE
    std::clog << "-- ROI (x0, x1, y0, y1): " <<
              start_x << ", " << stop_x << ", " <<
              start_y << ", " << stop_y << std::endl;
    std::clog << "-- ROI size (nx, ny): " << ncol_roi << ", " << nrows_roi << std::endl;
    std::clog << "-- Number of pixel in ROI: " << pixel_in_roi << std::endl;
#endif
    if(selected_frames.empty()){
#if VERBOSE
        std::clog << "-- No frames specified, using all frames in input" << std::endl;
#endif
        selected_frames.reserve(nf);
        for(int i=0; i < nf; i++) selected_frames.emplace_back(i);
        nframes_roi = selected_frames.size();
    }
#if VERBOSE
    if(use_mask)
        std::clog << "-- Using selection mask." << std::endl;
    else
        std::clog << "-- No mask mask specified." << std::endl;
#endif
    // Check size of mask and give warning if mask size does not match ROI
    if(
            ((nf != dmask1) || (nl != dmask2) || (np != dmask3)) &&
            use_mask
    ) std::clog << "WARNING: Selection mask size and ROI size do not match!" << std::endl;
    std::vector<bool> mask_v(nf*nl*np,true);
    // copy the values from the input to the mask
    for(int f=0; f<nf; f++)
        for(int l=0; (l<nl) && (l < dmask2); l++)
            for(int p=0; (p<np) && (p < dmask3); p++){
                // in cases the number of frames in mask is smaller then the
                // number of frames in ROI use first frame in mask
                int fi = (f < dmask1)? f : 1;
                mask_v[f*nl*np + l*np + p] = mask[fi * dmask1 * dmask2 + l * dmask2 + p];
            }
#if VERBOSE
    std::clog << "-- Copying image to ROI array... " << std::endl;
    std::clog << "-- Frames in ROI: " << nframes_roi << std::endl;
#endif
    auto *img_roi = (double*) calloc(nframes_roi * pixel_in_roi, sizeof(double));
#if VERBOSE
    std::clog << "-- Copying frame: ";
#endif
    int current_pixel = 0;
    for(auto f:selected_frames){
#if VERBOSE
        std::clog << f << " ";
#endif
        for(int l=start_y;l<stop_y;l++){
            for(int p=start_x; p<stop_x;p++){
                if(mask_v[current_pixel]){
                    double value;
                    if(clsm != nullptr) {
                        auto frame = clsm->frames[f];
                        auto line = frame->lines[l];
                        value = line->pixels[p]->_tttr_indices.size();
                    } else if(images != nullptr){
                        value = images[f * (nl * np) + l * nl + p];
                    }
                    img_roi[current_pixel] = value;
                } else{
                    img_roi[current_pixel] = 0.0;
                }
                current_pixel++;
            }
        }
    }
#if VERBOSE
    std::clog << std::endl;
    std::clog << "-- Correcting ROI" << std::endl;
#endif
    if(background != 0){
#if VERBOSE
        std::clog << "-- Subtracted background per pixel: " << background << std::endl;
#endif
        for(int f=0;f<nframes_roi;f++){
            for(int p=0;p<pixel_in_roi;p++){
                img_roi[f * pixel_in_roi + p] -= background;
            }
        }
    }
    if(clip){
#if VERBOSE
        std::clog << "-- Clipping values: " << clip_max << ", " << clip_min << std::endl;
#endif
        for(int f=0;f<nframes_roi;f++){
            for(int p=0;p<pixel_in_roi;p++){
                double value = img_roi[f * pixel_in_roi + p];
                value = std::min(clip_max, value);
                value = std::max(clip_min, value);
                img_roi[f * pixel_in_roi + p] = value;
            }
        }
    }
#if VERBOSE
    std::clog << "-- Subtract average mode: " << subtract_average << std::endl;
#endif
    if(subtract_average=="stack"){
#if VERBOSE
        std::clog << "-- Subtract pixel average of all frames." << std::endl;
#endif
        auto img_mean = (double*) calloc(pixel_in_roi, sizeof(double));
        double total_count = 0.0;
        for(int f=0;f<nframes_roi;f++){
            for(int p=0;p<pixel_in_roi;p++){
                double count = img_roi[f * pixel_in_roi + p];
                total_count += count;
                img_mean[p] += count;
            }
        }
        double mean_count = total_count / (nframes_roi * pixel_in_roi);
        for(int p=0;p<pixel_in_roi;p++){
            img_mean[p] /= nframes_roi;
        }
        for(int f=0;f<nframes_roi;f++){
            for(int p=0;p<pixel_in_roi;p++){
                img_roi[f * pixel_in_roi + p] =
                        img_roi[f * pixel_in_roi + p] - img_mean[p] + mean_count;
            }
        }
        free(img_mean);
    } else if(subtract_average=="frame"){
#if VERBOSE
        std::clog << "-- Subtracting average intensity in frame." << std::endl;
#endif
        // compute the mean intensity in image and subtract the mean fro
        for(int f=0;f<nframes_roi;f++){
            double total_count = 0.0;
            for(int p=0;p<pixel_in_roi;p++){
                total_count += img_roi[f * pixel_in_roi + p];
            }
            double mean_count = total_count / (nframes_roi * pixel_in_roi);
            for(int p=0;p<pixel_in_roi;p++){
                img_roi[f * pixel_in_roi + p] = img_roi[f * pixel_in_roi + p] - mean_count;
            }
        }
    }
    *output = img_roi;
    *dim1 = nframes_roi;
    *dim2 = nrows_roi;
    *dim3 = ncol_roi;
}


void CLSMImage::compute_ics(
        double** output, int* dim1, int* dim2, int* dim3,
        std::shared_ptr<TTTR> tttr_data,
        CLSMImage* clsm,
        double *images, int input_frames, int input_lines, int input_pixel,
        std::vector<int> x_range, std::vector<int> y_range,
        std::vector<std::pair<int,int>> frames_index_pairs,
        std::string subtract_average,
        CLSMImage* clsm2,
        double *images_2, int input_frames_2, int input_lines_2, int input_pixel_2,
        uint8_t *mask, int dmask1, int dmask2, int dmask3
){
#if VERBOSE
    std::clog << "COMPUTE_ICS" << std::endl;
#endif
    // create roi
    double *roi1; int nf1, nl1, np1;
    double *roi2; int nf2, nl2, np2;
    // If pair of ICS frames empty make ACF without frame shift
    get_roi(&roi1, &nf1, &nl1, &np1,
            clsm, x_range, y_range,
            subtract_average, 0.0,
            false, 1, 1,
            images, input_frames, input_lines, input_pixel,
            mask, dmask1, dmask2, dmask3
    );
    if((clsm2!= nullptr)&&(images_2!= nullptr)){
        get_roi(&roi2, &nf2, &nl2, &np2,
                clsm, x_range, y_range,
                subtract_average, 0.0,
                false, 1, 1,
                images, input_frames, input_lines, input_pixel,
                mask, dmask1, dmask2, dmask3
        );
    } else{
        roi2 = roi1;
        nf2 = nf1; nl2 = nl1; np2 = np1;
    }
    int nf, nl, np;
    nf = std::min(nf1, nf2);
    nl = std::min(nl1, nl2);
    np = std::min(np1, np2);
    int pixel_in_roi = nl * np;

    // Define set of frame pairs (if no pairs were defined)
    if(frames_index_pairs.empty()){
#if VERBOSE
        std::clog << "-- No frame pair selection: Computing ACF " << std::endl;
#endif
        frames_index_pairs.reserve(nf);
        for(int i=0; i < nf; i++) frames_index_pairs.emplace_back(std::make_pair(i, i));
    }
    // Allocate memory for the ICS output array
    auto out_tmp = (double*) calloc(frames_index_pairs.size() * pixel_in_roi, sizeof(double));
    // Prepare FFTW plans for first and second frame, and the inverse
    fftw_complex *in, *first_out, *second_out;
    fftw_plan p_forward_first, p_forward_second, p_backward;
    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * pixel_in_roi);
    first_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * pixel_in_roi);
    second_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * pixel_in_roi);
    p_forward_first = fftw_plan_dft_2d(nl, np, in, first_out, FFTW_FORWARD, FFTW_MEASURE);
    p_forward_second = fftw_plan_dft_2d(nl, np, in, second_out, FFTW_FORWARD, FFTW_MEASURE);
    p_backward = fftw_plan_dft_2d(nl, np, in, first_out, FFTW_BACKWARD, FFTW_MEASURE);
    // Iterate through the pair of frames
#if VERBOSE
    std::clog << "-- CCF of pair: ";
#endif
    int current_frame = 0;
    for (auto frame_pair: frames_index_pairs) {
#if VERBOSE
        std::clog << "(" << frame_pair.first << ", " << frame_pair.second << ") ";
#endif
        // FFT of first frame
        double first_frame_total_intensity = 0.0;
        for (int pix = 0; pix < pixel_in_roi; pix++) {
            double count = roi1[frame_pair.first * pixel_in_roi + pix];
            first_frame_total_intensity += count;
            in[pix][0] = count;
        }
        fftw_execute(p_forward_first);
        // FFT of second frame
        double second_frame_total_intensity;
        if(frame_pair.second != frame_pair.first){
            second_frame_total_intensity = 0.0;
            for (int pix = 0; pix < pixel_in_roi; pix++) {
                double count = roi1[frame_pair.second * pixel_in_roi + pix];
                second_frame_total_intensity += count;
                in[pix][0] = count;
            }
            fftw_execute(p_forward_second);
            // make product of FFT(img1) * conj(FFT(img2))
            for(int n=0; n < pixel_in_roi; n++){
                in[n][0] = first_out[n][0] * second_out[n][0] + first_out[n][1] * second_out[n][1];
                in[n][1] = 0;
            }
        } else{
            // if the second frame equals to the first use first FFT
            second_frame_total_intensity = first_frame_total_intensity;
            // make product of FFT(img1) * conj(FFT(img1))
            for(int n=0; n < pixel_in_roi; n++){
                in[n][0] = first_out[n][0] * first_out[n][0] + first_out[n][1] * first_out[n][1];
                in[n][1] = 0;
            }
        }
        // make backward transform FFT-1(FFT(img1) * conj(FFT(img2)))
        fftw_execute(p_backward);
        // copy to results to ics output and normalize
        int frame_pos = current_frame * pixel_in_roi;
        for(int pix=0; pix < pixel_in_roi; pix++){
            // We need to normalize by the mean intensity and the number of
            // pixels. A forward and backward FFT by fftw3 introduce a factor
            // N=nx*ny. THus, it is enough to divide by total_intensity_2.
            double denom = (first_frame_total_intensity * second_frame_total_intensity);
            out_tmp[frame_pos + pix] = first_out[pix][0] / denom - 1.0;
        }
        current_frame++;
    }
#if VERBOSE
    std::clog << std::endl;
#endif
    fftw_destroy_plan(p_forward_first);
    fftw_destroy_plan(p_forward_second);
    fftw_destroy_plan(p_backward);
    fftw_free(in);
    fftw_free(first_out); fftw_free(second_out);
    free(roi1);

    *dim1 = (int) nf;
    *dim2 = (int) nl;
    *dim3 = (int) np;
    *output = out_tmp;
}