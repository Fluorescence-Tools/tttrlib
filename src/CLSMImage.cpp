#include "include/CLSMImage.h"
#include "info.h"

void CLSMImage::copy(const CLSMImage& p2, bool fill){
#if VERBOSE_TTTRLIB
    std::clog << "-- Copying image structure." << std::endl;
    if(fill){
        std::clog << "-- Copying pixel information." << std::endl;
    }
#endif
    // private attributes
#if VERBOSE_TTTRLIB
    std::clog << "-- Copying frame: " << std::flush;
    int i_frame = 0;
#endif
    for(auto f: p2.frames){
#if VERBOSE_TTTRLIB
        std::clog << i_frame++ << " " << std::flush;
#endif
        frames.emplace_back(new CLSMFrame(*f, fill));
    }
#if VERBOSE_TTTRLIB
    std::clog << std::endl;
#endif
#if VERBOSE_TTTRLIB
    std::clog << "-- Linking TTTR: " << std::endl << std::flush;
#endif
    this->tttr = p2.tttr;
    // public attributes
    settings = p2.settings;
    n_frames = p2.n_frames;
    n_lines = p2.n_lines;
    n_pixel = p2.n_pixel;
#if VERBOSE_TTTRLIB
    std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
#endif
}

CLSMImage::CLSMImage(const CLSMImage& p2, bool fill){
    copy(p2, fill);
}

void CLSMImage::shift_line_start(int macro_time_shift){
#if VERBOSE_TTTRLIB
    std::clog << "-- Shifting line start by [macro time clocks]: " << macro_time_shift << std::endl;
#endif
    for(auto &frame : get_frames()){
        for(auto &line: frame->get_lines()){
            line->shift_start_time(macro_time_shift);
        }
    }
}

void CLSMImage::determine_number_of_lines(){
    n_lines = 0;
    for(auto &f: frames){
        if(f->lines.size() > n_lines)
            n_lines = f->lines.size();
    }
}

CLSMImage::CLSMImage (
        std::shared_ptr<TTTR> tttr_data,
        CLSMSettings settings,
        CLSMImage* source,
        bool fill,
        std::vector<int> channels,
        std::vector<std::pair<int,int>> micro_time_ranges
) {
#if VERBOSE_TTTRLIB
    std::clog << "Initializing CLSM image" << std::endl;
#endif
    if(source != nullptr){
#if VERBOSE_TTTRLIB
        std::clog << "-- Copying data from other object" << std::endl;
#endif
        copy(*source, fill);
    } else{
#if VERBOSE_TTTRLIB
        std::clog << "-- Initializing new CLSM image..." << std::endl;
#endif
        this->settings = settings;
        this->n_pixel = settings.n_pixel_per_line;
        tttr = tttr_data;

        // early exist if sth is wrong
        if(tttr_data.get() == nullptr){
            std::clog << "WARNING: No TTTR object provided" << std::endl;
            return;
        }
        if(this->settings.marker_frame_start.empty()){
            std::clog << "WARNING: No frame marker provided" << std::endl;
            return;
        }
        if(tttr_data->n_records_read == 0){
            std::clog << "WARNING: No records in TTTR object" << std::endl;
            return;
        }

        create_frames(true);
        create_lines();
#if VERBOSE_TTTRLIB
        std::clog << "-- Initial number of frames: " << n_frames << std::endl;
        std::clog << "-- Lines per frame: " << n_lines << std::endl;
#endif
        determine_number_of_lines();
        remove_incomplete_frames();
        create_pixels_in_lines();
    }
    // fill pixel
    if(fill && !channels.empty())
        this->fill(this->tttr.get(), channels, false, micro_time_ranges);
    if(settings.macro_time_shift!=0)
        shift_line_start(settings.macro_time_shift);
}

void CLSMImage::create_pixels_in_lines() {
    // by improving here, a factor of two in speed could be possible.
    for(auto &f: frames){
        for(auto &l: f->lines){
            l->pixels.resize(n_pixel);
            for(auto &p: l->pixels){
                p._tttr = tttr.get();
            }
        }
    }
#if VERBOSE_TTTRLIB
    std::clog << "-- Number of pixels per line: " << n_pixel << std::endl;
#endif
}

void CLSMImage::append(CLSMFrame* frame) {
    frames.emplace_back(frame);
    n_frames++;
}

void CLSMImage::rebin(int bin_line, int bin_pixel){
    std::vector<unsigned int> mapping;
    int n_px = n_frames * n_lines * n_pixel;
    mapping.reserve(2 * n_px);
    for(unsigned int f = 0; f < n_frames; f++){
        for(unsigned int l = 0; l < n_lines; l++) {
            for(unsigned int p = 0; p < n_pixel; p++) {
                auto source_idx = to1D(f, l, p);
                auto target_idx = to1D(f, l / bin_line, p / bin_pixel);
                mapping.emplace_back(source_idx);
                mapping.emplace_back(target_idx);
            }
        }
    }
    transform(&mapping[0], 2 * n_px);
}

std::vector<int> CLSMImage::get_frame_edges(
        TTTR* tttr,
        int start_event,
        int stop_event,
        std::vector<int> marker_frame,
        int marker_event_type,
        int reading_routine,
        bool skip_before_first_frame_marker,
        bool skip_after_last_frame_marker)
{
    int n_events = tttr->get_n_valid_events();
    std::vector<int> frame_edges;
    if (!skip_before_first_frame_marker)
        frame_edges.emplace_back(start_event);
    if (stop_event < 0) stop_event = n_events;

#if VERBOSE_TTTRLIB
    std::clog << "-- GET_FRAME_EDGES" << std::endl;
    std::clog << "-- Reading routing:" << reading_routine << std::endl;
    std::clog << "-- skip_after_last_frame_marker:" << skip_after_last_frame_marker << std::endl;
    std::clog << "-- skip_before_first_frame_marker:" << skip_before_first_frame_marker << std::endl;
    std::clog << "-- n_events:" << n_events << std::endl;
    std::clog << "-- stop_event:" << stop_event << std::endl;
#endif

    for (int i_event = start_event; i_event < stop_event; i_event++) {
        for (auto f: marker_frame) {
            if (reading_routine == CLSM_SP8) {
                if (tttr->routing_channels[i_event] == marker_event_type){
                    if (f == tttr->micro_times[i_event]) {
                        frame_edges.emplace_back(i_event);
                        break;
                    }
                }
            } else if (reading_routine == CLSM_SP5) {
                if(f == tttr->routing_channels[i_event]){
                    frame_edges.emplace_back(i_event);
                    break;
                }
            } else {
                if(tttr->event_types[i_event] == marker_event_type){
                    if(f == tttr->routing_channels[i_event]){
                        frame_edges.emplace_back(i_event);
                        break;
                    }
                }
            }
        }
    }
    if(!skip_after_last_frame_marker){
        frame_edges.emplace_back(n_events);
    }
#if VERBOSE_TTTRLIB
    std::clog << "-- number of frame edges:" << frame_edges.size() << std::endl;
#endif
    return frame_edges;
}

std::vector<int> CLSMImage::get_line_edges(
        TTTR* tttr,
        int start_event, int stop_event,
        int marker_line_start, int marker_line_stop,
        int marker_event_type,
        int reading_routine
) {
    std::vector<int> line_edges;
    if (stop_event < 0) stop_event = tttr->n_valid_events;

    if (reading_routine == CLSM_SP5){
        line_edges.emplace_back(start_event);
    }
    for (int i_event = start_event; i_event < stop_event; i_event++) {
        if (reading_routine == CLSM_SP8) {
            if(tttr->routing_channels[i_event] == marker_event_type){
                if(tttr->micro_times[i_event] == marker_line_start){
                    line_edges.emplace_back(i_event);
                    continue;
                }
                if(tttr->micro_times[i_event] == marker_line_stop){
                    line_edges.emplace_back(i_event);
                    continue;
                }
            }
        } else if (reading_routine == CLSM_SP5) {
            if(tttr->event_types[i_event] == marker_event_type){
                if(tttr->routing_channels[i_event] == marker_line_start){
                    line_edges.emplace_back(i_event);
                    continue;
                }
                if(tttr->routing_channels[i_event] == marker_line_stop){
                    line_edges.emplace_back(i_event);
                    continue;
                }
            }
        } else {
            if(tttr->event_types[i_event] == marker_event_type){
                if(tttr->routing_channels[i_event] == marker_line_start) {
                    line_edges.emplace_back(i_event);
                    continue;
                } else if(tttr->routing_channels[i_event] == marker_line_stop){
                    line_edges.emplace_back(i_event);
                    continue;
                }
            }
        }
    }
    return line_edges;
}


std::vector<int> CLSMImage::get_line_edges_dur(
        TTTR* tttr,
        int frame_start,
        int frame_stop,
        int marker_line_start,
        int line_duration,
        int marker_event_type,
        int reading_routine
){

#if VERBOSE_TTTRLIB
    std::clog << "GET_LINE_EDGES_DUR" << std::endl;
    std::clog << "-- frame_start: " << frame_start << std::endl;
    std::clog << "-- frame_stop: " << frame_stop << std::endl;
    std::clog << "-- marker_line_start: " << marker_line_start << std::endl;
    std::clog << "-- line_duration: " << line_duration << std::endl;
    std::clog << "-- marker_event_type: " << marker_event_type << std::endl;
    std::clog << "-- reading_routine: " << reading_routine << std::endl;
#endif

    if (frame_stop < 0) frame_stop = tttr->n_valid_events;
    std::vector<int> line_edges;
    for(int i_event = frame_start; i_event < frame_stop; i_event++) {
        std::pair<int, int> start_stop;
        if (reading_routine == CLSM_SP8) {
            start_stop = find_clsm_start_stop(
                    i_event,
                    marker_line_start, -1, marker_event_type,
                    tttr->macro_times,
                    tttr->routing_channels,
                    tttr->micro_times,
                    frame_stop,
                    line_duration
            );
        } else {
            start_stop = find_clsm_start_stop(
                    i_event,
                    marker_line_start, -1, marker_event_type,
                    tttr->macro_times,
                    tttr->routing_channels,
                    tttr->event_types,
                    frame_stop,
                    line_duration
            );
        }
        if ((start_stop.first > 0) && (start_stop.second > 0)) {
            line_edges.emplace_back(start_stop.first);
            line_edges.emplace_back(start_stop.second);
        }
    }
    return line_edges;
}


void CLSMImage::create_frames(bool clear_first){
    if(clear_first) frames.clear();
    // get frame edges and create new frames
    auto frame_edges = get_frame_edges(
            tttr.get(),
            0, -1,
            settings.marker_frame_start,
            settings.marker_event_type,
            settings.reading_routine,
            settings.skip_before_first_frame_marker,
            settings.skip_after_last_frame_marker
    );
#if VERBOSE_TTTRLIB
    std::clog << "-- CREATE_FRAMES" << std::endl;
    std::cout << "-- Creating " << frame_edges.size() << " frames: " << std::flush;
#endif
    for(size_t i=0; i < frame_edges.size() - 1; i++){
        auto frame = new CLSMFrame(
                frame_edges[i], frame_edges[i + 1], tttr.get());
        append(frame);
#if VERBOSE_TTTRLIB
        std::cout << " " << i  << std::flush;
#endif
    }
#if VERBOSE_TTTRLIB
    std::cout << std::endl;
#endif
#if VERBOSE_TTTRLIB
    std::clog << "-- Initial number of frames: " << n_frames << std::endl;
#endif
}

void CLSMImage::create_lines(){
    // create new lines in every frame
#if VERBOSE_TTTRLIB
    std::clog << "CLSMIMAGE::CREATE_LINES" << std::endl;
    std::clog << "-- Frame start, frame stop idx:" << std::endl;
#endif
    for(auto &frame:frames){
#if VERBOSE_TTTRLIB
        std::clog << "\t" << frame->get_start() << ", " << frame->get_stop() << std::endl;
#endif
        int pixel_duration = (settings.marker_line_stop < 0) ?
                             tttr->header->get_pixel_duration() : -1;
        std::vector<int> line_edges;
        if(settings.marker_line_stop >= 0){
            line_edges = get_line_edges(
                    tttr.get(),
                    frame->get_start(),
                    frame->get_stop(),
                    settings.marker_line_start,
                    settings.marker_line_stop,
                    settings.marker_event_type,
                    settings.reading_routine
            );
        } else{
            long line_duration = tttr->header->get_line_duration();
            line_edges = get_line_edges_dur(
                    tttr.get(),
                    frame->get_start(),
                    frame->get_stop(),
                    settings.marker_line_start,
                    line_duration,
                    settings.marker_event_type,
                    settings.reading_routine
            );
        }
        for(size_t i_line = 0; i_line < line_edges.size() / 2; i_line++){
            auto line_start = line_edges[(i_line * 2) + 0];
            auto line_stop = line_edges[(i_line * 2) + 1];
            auto line = new CLSMLine(line_start);
            line->_stop = (int) line_stop;
            line->set_pixel_duration(pixel_duration);
            line->update(tttr.get(), false);
            frame->append(line);
        }
    }
}

void CLSMImage::remove_incomplete_frames(){
    // remove incomplete frames
#if VERBOSE_TTTRLIB
    std::clog << "-- Removing incomplete frames" << std::endl;
#endif
    std::vector<CLSMFrame*> complete_frames;
    n_frames = frames.size();
    size_t i_frame = 0;
    for(auto frame : frames){
        if(frame->lines.size() == n_lines){
            complete_frames.push_back(frame);
        } else{
#if VERBOSE_TTTRLIB
            std::cerr << "WARNING: Frame " << i_frame + 1 << " / " << frames.size() <<
            " incomplete only "<< frame->lines.size() << " / " << n_lines << " lines." << std::endl;
#endif
            delete(frame);
        }
        i_frame++;
    }
    frames = complete_frames;
    n_frames = complete_frames.size();
#if VERBOSE_TTTRLIB
    std::clog << "-- Final number of frames: " << n_frames << std::endl;
#endif
}


void CLSMImage::clear() {
#if VERBOSE_TTTRLIB
    std::clog << "Clear pixels of photons" << std::endl;
#endif
    _is_filled_ = false;
    for(auto *frame : frames){
        for(auto &line : frame->lines){
            for(auto &pixel: line->pixels){
                pixel.clear();
            }
        }
    }
}

void CLSMImage::fill(
        TTTR* tttr_data,
        std::vector<int> channels,
        bool clear,
        const std::vector<std::pair<int,int>> &micro_time_ranges
) {
#if VERBOSE_TTTRLIB
    std::clog << "-- Filling pixels..." << std::endl;
    std::clog << "-- Channels: "; for(auto ch: channels) std::clog << ch << " "; std::clog << std::endl;
    std::clog << "-- Clear pixel before fill: " << clear << std::endl;
    std::clog << "-- Assign photons to pixels" << std::endl;
    std::clog << "-- Micro time ranges:";
    for(auto r : micro_time_ranges){
        std::clog << "(" << r.first << "," << r.second << ") ";
    }
    std::clog << std::endl;
#endif
    if(tttr_data == nullptr){
        tttr_data = tttr.get();
    }
    if(clear) this->clear();
    if(channels.empty()){
        std::clog << "WARNING: Image filled without channel numbers. Using all channels." << std::endl;
        signed char *chs; int nchs;
        tttr_data->get_used_routing_channels(&chs, &nchs);
        for(int i = 0; i < nchs; i++){
            channels.emplace_back(chs[i]);
        }
        free(chs);
    }
    for(auto frame : frames){
        for(auto line : frame->lines){
            if(line->pixels.empty()){
                std::clog << "WARNING: Line without pixel." << std::endl;
                continue;
            }
            auto pixel_duration = line->get_pixel_duration();
            size_t n_pixels_in_line = line->pixels.size();

            // iterate though events in the line
            int start_idx = line->get_start();
            int stop_idx = line->get_stop();
            for(auto event_i=start_idx; event_i < stop_idx; event_i++){
                if (tttr_data->event_types[event_i] != RECORD_PHOTON) continue;
                auto c = tttr_data->routing_channels[event_i];
                for(auto &ci : channels){
                    if(c == ci){
                        auto line_time = (tttr_data->macro_times[event_i] - line->get_start_time());
                        auto pixel_nbr = line_time / pixel_duration;

                        // skip if photon is outside of line
                        if(pixel_nbr >=  n_pixels_in_line) break;

                        // Check if micro time is in micro time range
                        bool add_ph = true;
                        auto micro_time = tttr_data->micro_times[event_i];
                        for(auto r: micro_time_ranges){
                            add_ph &= micro_time >= r.first;
                            add_ph &= micro_time <= r.second;
                        }

                        // If photon was in micro time range add it
                        if(add_ph){
                            line->pixels[pixel_nbr].append(event_i);
                        }
                    }
                }
            }

            // update start, stop indices and times
            for(auto pixel : line->get_pixels()){
                pixel.update(tttr_data);
            }

        }
    }
    _is_filled_ = true;
}

void CLSMImage::strip(const std::vector<int> &tttr_indices){
    int offset = 0;
//    for(auto &p: get_pixel()){
//        offset = p->strip(tttr_indices, offset);
//    }
    for(auto &f: get_frames()){
        for(auto &l: f->get_lines()){
            for(auto &p: l->get_pixels()){
                offset = p.strip(tttr_indices, offset);
            }
        }
    }
}

void CLSMImage::get_intensity(unsigned int**output, int* dim1, int* dim2, int* dim3){
    *dim1 = (int) n_frames;
    *dim2 = (int) n_lines;
    *dim3 = (int) n_pixel;
    size_t n_pixel_total = n_frames * n_pixel * n_lines;
#if VERBOSE_TTTRLIB
    std::clog << "Get intensity image" << std::endl;
    std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Total number of pixels: " << n_pixel_total << std::endl;
#endif
    auto* t = (unsigned int*) calloc(n_pixel_total+1, sizeof(unsigned int));
    size_t i_frame = 0;
    size_t t_pixel = 0;
    for(auto &frame : frames){
        size_t i_line = 0;
        for(auto &line : frame->lines){
            size_t i_pixel = 0;
            for(auto &pixel : line->pixels){
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) +
                                   i_line * (n_pixel) +
                                   i_pixel;
                t[pixel_nbr] = pixel._tttr_indices.size();
                t_pixel++;
                i_pixel++;
            }
            i_line++;
        }
        i_frame++;
    }
    *output = t;
}

void CLSMImage::get_fluorescence_decay(
        TTTR* tttr_data,
        unsigned char** output, int* dim1, int* dim2, int* dim3, int* dim4,
        int micro_time_coarsening,
        bool stack_frames
        ){
#if VERBOSE_TTTRLIB
    std::clog << "Get decay image" << std::endl;
#endif
    size_t nf = (stack_frames) ? 1 : n_frames;
    size_t n_tac = tttr_data->header->get_number_of_micro_time_channels() / micro_time_coarsening;
    *dim1 = nf;
    *dim2 = n_lines;
    *dim3 = n_pixel;
    *dim4 = (int) n_tac;

    size_t n_tac_total = nf * n_lines * n_pixel * n_tac;
    auto* t = (unsigned char*) calloc(n_tac_total, sizeof(unsigned char));
#if VERBOSE_TTTRLIB
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
            for(size_t pixel_i=0; pixel_i < n_pixel; pixel_i++){
                auto pixel = line->pixels[pixel_i];
                size_t pixel_nbr = i_frame * (n_lines * n_pixel * n_tac) +
                                   i_line  * (n_pixel * n_tac) +
                                   i_pixel * (n_tac);
                for(auto i : pixel._tttr_indices){
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
#if VERBOSE_TTTRLIB
    std::clog << "Get fluorescence correlation image" << std::endl;
#endif
    size_t nf = (stack_frames) ? 1 : n_frames;
    auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
    size_t n_corr = corr.curve.size();
    size_t n_cor_total = nf * n_lines * n_pixel * n_corr;
    auto t = (float*) calloc(n_cor_total, sizeof(float));
#if VERBOSE_TTTRLIB
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Number of correlation blocks: " << n_casc << std::endl;
    std::clog << "-- Number of correlation bins per block: " << n_bins << std::endl;
    std::clog << "-- Number of correlation channels: " << n_corr << std::endl;
    std::clog << "-- Correlating... " << n_corr << std::endl;
#endif
    size_t o_frame = 0;
//#pragma omp parallel for default(none) shared(tttr, o_frame, t, clsm_other)
    for(unsigned int i_frame=0; i_frame < n_frames; i_frame++){
        auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
        auto frame = frames[i_frame];
        auto other_frame = clsm_other->frames[i_frame];
        for(unsigned int i_line=0; i_line < n_lines; i_line++){
            auto line = frame->lines[i_line];
            auto other_line = other_frame->lines[i_line];
            for(unsigned int i_pixel=0; i_pixel < n_pixel; i_pixel++){
                auto pixel = line->pixels[i_pixel];
                auto other_pixel = other_line->pixels[i_pixel];
                if(
                    ((int) pixel._tttr_indices.size() > min_photons) &&
                    ((int) other_pixel._tttr_indices.size() > min_photons)
                ){
                    auto tttr_1 = TTTR(
                            *tttr,
                            pixel._tttr_indices.data(),
                            pixel._tttr_indices.size(),
                            false
                    );
                    auto tttr_2 = TTTR(
                            *tttr,
                            other_pixel._tttr_indices.data(),
                            other_pixel._tttr_indices.size(),
                            false
                    );
                    corr.set_tttr(
                            std::make_shared<TTTR>(tttr_1),
                            std::make_shared<TTTR>(tttr_2)
                            );
                    double* correlation; int temp;
                    if(!normalized_correlation){
                        corr.get_corr(&correlation, &temp);
                    } else{
                        corr.get_corr_normalized(&correlation, &temp);
                    }
                    for(size_t i_corr = 0; i_corr < n_corr; i_corr++){
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

void CLSMImage::get_decay_of_pixels(
        TTTR* tttr_data,
        uint8_t* mask, int dmask1, int dmask2, int dmask3,
        unsigned int** output, int* dim1, int* dim2,
        int tac_coarsening,
        bool stack_frames
){
    size_t n_decays = stack_frames ? 1 : n_frames;
    size_t n_tac = tttr_data->header->get_number_of_micro_time_channels() / tac_coarsening;
#if VERBOSE_TTTRLIB
    std::clog << "Get decays:" << std::endl;
    std::clog << "-- Number of frames: " << n_frames << std::endl;
    std::clog << "-- Stack frames (true/false): " << stack_frames << std::endl;
    std::clog << "-- Number of decays: " << n_decays << std::endl;
    std::clog << "-- Number of micro time channels: " << tttr_data->header->get_number_of_micro_time_channels() << std::endl;
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
                        for (auto i : pixel._tttr_indices) {
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

void CLSMImage::get_mean_micro_time(
        TTTR* tttr_data,
        double** output, int* dim1, int* dim2, int* dim3,
        double microtime_resolution,
        int minimum_number_of_photons,
        bool stack_frames
){
#if VERBOSE_TTTRLIB
    std::clog << "Get mean micro time image" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
    std::clog << "-- Computing stack of mean micro times " << std::endl;
#endif
    if(microtime_resolution < 0)
        microtime_resolution = tttr_data->header->get_micro_time_resolution();
    if(!stack_frames) {
        auto* t = (double *) malloc(n_frames * n_lines * n_pixel * sizeof(double));
        for(size_t i_frame = 0; i_frame < n_frames; i_frame++){
            for(size_t i_line = 0; i_line < n_lines; i_line++){
                for(size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++){
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line  * (n_pixel) + i_pixel;
                    CLSMPixel px = frames[i_frame]->lines[i_line]->pixels[i_pixel];
                    t[pixel_nbr] = px.get_mean_microtime(tttr_data, microtime_resolution, minimum_number_of_photons);
                }
            }
        }
        *dim1 = (int) n_frames;
        *dim2 = (int) n_lines;
        *dim3 = (int) n_pixel;
        *output = t;
    } else{
        int w_frame = 1;
#if VERBOSE_TTTRLIB
        std::clog << "-- Compute photon weighted average over frames" << std::endl;
#endif
        auto* r = (double *) malloc(sizeof(double) * w_frame * n_lines * n_pixel);
        for(size_t i_line = 0; i_line < n_lines; i_line++){
            for(size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++){
                size_t pixel_nbr = i_line  * n_pixel + i_pixel;
                // average the arrival times over the frames
                r[pixel_nbr] = 0.0;
                std::vector<int> tr;
                for(size_t i_frame = 0; i_frame < n_frames; i_frame++){
                    auto temp = frames[i_frame]->lines[i_line]->pixels[i_pixel]._tttr_indices;
                    tr.insert(tr.end(), temp.begin(), temp.end());
                }
                r[pixel_nbr] = tttr_data->get_mean_microtime(&tr, microtime_resolution, minimum_number_of_photons);
            }
        }
        *dim1 = (int) w_frame;
        *dim2 = (int) n_lines;
        *dim3 = (int) n_pixel;
        *output = r;
    }
}

void CLSMImage::get_phasor(
        float **output, int *dim1, int *dim2, int *dim3, int *dim4,
        TTTR *tttr_data,
        TTTR *tttr_irf,
        double frequency,
        int minimum_number_of_photons,
        bool stack_frames
) {
    double g_irf=1.0, s_irf=0.0;
    if(frequency<0){
        auto header = tttr_data->get_header();
        auto macro_time_res = header->get_macro_time_resolution();
        auto micro_time_res = header->get_micro_time_resolution();
        frequency = micro_time_res / macro_time_res;
    }
    if(tttr_irf!= nullptr){
        std::vector<double> gs = DecayPhasor::compute_phasor(
                tttr_irf->micro_times, tttr_irf->n_valid_events,
                frequency
                );
        g_irf = gs[0];
        s_irf = gs[1];
    }
    int o_frames = stack_frames? 1: n_frames;
    double factor = (2. * frequency * M_PI);
    auto* t = (float *) calloc(o_frames * n_lines * n_pixel * 2, sizeof(float));
    for(int i_line=0; i_line < n_lines; i_line++){
        for(int i_pixel=0; i_pixel < n_pixel; i_pixel++){
            if(stack_frames){
                std::vector<int> idxs = {};
                size_t pixel_nbr = i_line  * (n_pixel * 2) + i_pixel * 2;
                for(int i_frame=0; i_frame < n_frames; i_frame++){
                    auto n = frames[i_frame]->lines[i_line]->pixels[i_pixel]._tttr_indices;
                    idxs.insert(idxs.end(), n.begin(), n.end());
                }
                auto r = DecayPhasor::compute_phasor(
                        tttr_data->micro_times, tttr_data->n_valid_events,
                        frequency,
                        minimum_number_of_photons,
                        g_irf, s_irf,
                        &idxs
                );
                t[pixel_nbr + 0] = (float) r[0];
                t[pixel_nbr + 1] = (float) r[1];
            } else{
                for(int i_frame=0; i_frame < n_frames; i_frame++){
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel * 2) + i_line  * (n_pixel * 2) + i_pixel * 2;
                    auto r = DecayPhasor::compute_phasor(
                            tttr_data->micro_times, tttr_data->n_valid_events,
                            frequency,
                            minimum_number_of_photons,
                            g_irf, s_irf,
                            &frames[i_frame]->lines[i_line]->pixels[i_pixel]._tttr_indices
                    );
                    t[pixel_nbr + 0] = (float) r[0];
                    t[pixel_nbr + 1] = (float) r[1];
                }
            }
        }
    }
#if VERBOSE_TTTRLIB
    std::clog << "GET_PHASOR_IMAGE..." << std::endl;
    std::clog << "-- frequency [GHz]: " << frequency << std::endl;
    std::clog << "-- stack_frames: " << stack_frames << std::endl;
    std::clog << "-- minimum_number_of_photons: " << minimum_number_of_photons << std::endl;
#endif
    *dim1 = (int) o_frames;
    *dim2 = (int) n_lines;
    *dim3 = (int) n_pixel;
    *dim4 = (int) 2;
    *output = t;
}

void CLSMImage::get_mean_lifetime(
        TTTR* tttr_data,
        double** output, int* dim1, int* dim2, int* dim3,
        const int minimum_number_of_photons,
        TTTR* tttr_irf, double m0_irf, double m1_irf,
        bool stack_frames
){
    const double dt = tttr_data->header->get_micro_time_resolution() * 1E9;
#if VERBOSE_TTTRLIB
    std::clog << "Compute a mean lifetime image (Isenberg 1973)" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
    std::clog << "-- Micro time resolution [ns]: " << dt << std::endl;
#endif
    if(tttr_irf != nullptr){
        unsigned short *micro_times_irf; int n_micro_times_irf;
        tttr_irf->get_micro_times(&micro_times_irf, &n_micro_times_irf);
#if VERBOSE_TTTRLIB
        std::clog << "-- Computing first moments (m0, m1) of IRF using TTTR data " << std::endl;
        std::clog << "-- n_micro_times_irf:" << n_micro_times_irf << std::endl;
#endif
        m0_irf = n_micro_times_irf; // number of photons
        m1_irf = 0.0;
        for(int i=0; i<n_micro_times_irf;i++) m1_irf += (double) micro_times_irf[i];
        //m1_irf = std::accumulate(micro_times_irf, micro_times_irf + n_micro_times_irf,0.0); // sum of photon arrival times
    }
#if VERBOSE_TTTRLIB
    std::clog << "-- IRF m0: " << m0_irf << std::endl;
    std::clog << "-- IRF m1: " << m1_irf << std::endl;
#endif
    size_t o_frames = stack_frames? 1: n_frames;
    auto* t = (double *) calloc(o_frames * n_lines * n_pixel, sizeof(double));
    for(int i_frame = 0; i_frame < o_frames; i_frame++) {
        for(int i_line = 0; i_line < n_lines; i_line++){
            for(int i_pixel = 0; i_pixel < n_pixel; i_pixel++){
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line  * (n_pixel) + i_pixel;
                if(stack_frames){
                    std::vector<int> tttr_indices;
                    for (auto &frame: frames) {
                        auto px = frame->lines[i_line]->pixels[i_pixel];
                        tttr_indices.insert(tttr_indices.end(), px._tttr_indices.begin(), px._tttr_indices.end());
                    }
                    t[pixel_nbr] = TTTRRange::compute_mean_lifetime(
                            tttr_indices, tttr_data, minimum_number_of_photons,
                            nullptr, m0_irf, m1_irf, dt); // m0_irf, m1_irf precomputed => tttr_irf=nullptr
                } else {
                    auto px = this->frames[i_frame]->lines[i_line]->pixels[i_pixel];
                    t[pixel_nbr] =
                            px.get_mean_lifetime(tttr_data, minimum_number_of_photons,nullptr, m0_irf, m1_irf, dt);
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
#if VERBOSE_TTTRLIB
    std::clog << "CREATE ROI" << std::endl;
#endif
    // determine the total number of frames, lines, and pixel in the input
    size_t nf, nl, np; // the number frames, lines, and pixel in the input
    TTTR* tttr_data = nullptr;
    if(clsm != nullptr){
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
        std::clog << "-- Using image array input" << std::endl;
#endif
        nf = n_frames;
        nl = n_lines;
        np = n_pixel;
    } else{
        std::cerr << "ERROR: No input data specified!" << std::endl;
    }
#if VERBOSE_TTTRLIB
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

#if VERBOSE_TTTRLIB
    std::clog << "-- ROI (x0, x1, y0, y1): " <<
              start_x << ", " << stop_x << ", " <<
              start_y << ", " << stop_y << std::endl;
    std::clog << "-- ROI size (nx, ny): " << ncol_roi << ", " << nrows_roi << std::endl;
    std::clog << "-- Number of pixel in ROI: " << pixel_in_roi << std::endl;
#endif
    if(selected_frames.empty()){
#if VERBOSE_TTTRLIB
        std::clog << "-- No frames specified, using all frames in input" << std::endl;
#endif
        selected_frames.reserve(nf);
        for(int i=0; i < nf; i++) selected_frames.emplace_back(i);
        nframes_roi = selected_frames.size();
    }
#if VERBOSE_TTTRLIB
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
                // in cases the number of frames in mask is smaller than the
                // number of frames in ROI use first frame in mask
                int fi = (f < dmask1)? f : 1;
                mask_v[f*nl*np + l*np + p] = mask[fi * dmask1 * dmask2 + l * dmask2 + p];
            }
#if VERBOSE_TTTRLIB
    std::clog << "-- Copying image to ROI array... " << std::endl;
    std::clog << "-- Frames in ROI: " << nframes_roi << std::endl;
#endif
    auto *img_roi = (double*) calloc(nframes_roi * pixel_in_roi, sizeof(double));
#if VERBOSE_TTTRLIB
    std::clog << "-- Copying frame: ";
#endif
    int current_pixel = 0;
    for(auto f:selected_frames){
#if VERBOSE_TTTRLIB
        std::clog << f << " ";
#endif
        for(int l=start_y;l<stop_y;l++){
            for(int p=start_x; p<stop_x;p++){
                if(mask_v[current_pixel]){
                    double value;
                    if(clsm != nullptr) {
                        auto frame = clsm->frames[f];
                        auto line = frame->lines[l];
                        value = (double) line->pixels[p]._tttr_indices.size();
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
#if VERBOSE_TTTRLIB
    std::clog << std::endl;
    std::clog << "-- Correcting ROI" << std::endl;
#endif
    if(background != 0){
#if VERBOSE_TTTRLIB
        std::clog << "-- Subtracted background per pixel: " << background << std::endl;
#endif
        for(int f=0;f<nframes_roi;f++){
            for(int p=0;p<pixel_in_roi;p++){
                img_roi[f * pixel_in_roi + p] -= background;
            }
        }
    }
    if(clip){
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
    std::clog << "-- Subtract average mode: " << subtract_average << std::endl;
#endif
    if(subtract_average=="stack"){
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
    std::clog << "-- CCF of pair: ";
#endif
    int current_frame = 0;
    for (auto frame_pair: frames_index_pairs) {
#if VERBOSE_TTTRLIB
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
            // N=nx*ny. Thus, it is enough to divide by total_intensity_2.
            double denom = (first_frame_total_intensity * second_frame_total_intensity);
            out_tmp[frame_pos + pix] = first_out[pix][0] / denom - 1.0;
        }
        current_frame++;
    }
#if VERBOSE_TTTRLIB
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


void CLSMImage::transform(unsigned int* input, int n_input){
    CLSMImage* source = new CLSMImage(*this, true);
    CLSMImage* target = this;

    target->clear();
    for(int i=0; i<n_input; i = i + 2){
        // source (s)
        CLSMPixel* source_pixel = source->getPixel(input[i + 0]);
        CLSMPixel* target_pixel = target->getPixel(input[i + 1]);
        // Append tttr indices to pixel
        target_pixel->_tttr_indices.insert(
                std::end(target_pixel->_tttr_indices),
                std::begin(source_pixel->_tttr_indices),
                std::end(source_pixel->_tttr_indices)
        );
    }

    delete source;
}


void CLSMImage::distribute(
        unsigned int pixel_id,
        CLSMImage* target,
        std::vector<int> &target_pixel_ids,
        std::vector<int> &target_probabilities
){
    std::cout << "TODO" << std::endl;
}


void CLSMImage::crop(
        int frame_start, int frame_stop,
        int line_start, int line_stop,
        int pixel_start, int pixel_stop
){
    frame_stop = std::min(std::max(0, frame_stop), (int) size());
    frame_start = std::max(0, frame_start);

    #if VERBOSE_TTTRLIB
    std::clog << "Crop image" << std::endl;
    std::clog << "-- Frame range: " << frame_start << ", " << frame_stop << std::endl;
    std::clog << "-- Line range: " << line_start << ", " << line_stop << std::endl;
    std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
    #endif

    std::vector<CLSMFrame*> frs;
    for(int i = 0; i < frame_start; i++){
        delete frames[i];
    }
    for(int i = frame_start; i < frame_stop; i++){
        auto f = frames[i];
        f->crop(line_start, line_stop, pixel_start, pixel_stop);
        frs.emplace_back(f);
    }
    for(unsigned long i = frame_stop; i < n_frames; i++){
        delete frames[i];
    }
    frames = frs;

    n_frames = frs.size();
    n_lines = frs[0]->lines.size();
    n_pixel = frs[0]->lines[0]->pixels.size();
}
