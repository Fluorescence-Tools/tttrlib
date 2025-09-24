#include "include/CLSMImage.h"
#include "include/Verbose.h"
#include <cstdlib>

void CLSMImage::copy(const CLSMImage &p2, bool fill) {
    bool verbose = is_verbose();
    if (verbose) {
        std::clog << "-- Copying image structure." << std::endl;
        if (fill) {
            std::clog << "-- Copying pixel information." << std::endl;
        }
    }
    // Clear existing frames (unique_ptr will delete)
    frames.clear();
    n_frames = 0;
    // private attributes
    int i_frame = 0;
    if (verbose) {
        std::clog << "-- Copying frame: " << std::flush;
    }
    frames.reserve(p2.frames.size());
    for (const auto &f : p2.frames) {
        ++i_frame;
        if (verbose) {
            std::clog << i_frame << " " << std::flush;
        }
        // deep copy each frame
        frames.emplace_back(f, fill); // copy constructor
    }
    if (verbose) {
        std::clog << std::endl;
    }
    if (verbose) {
        std::clog << "-- Linking TTTR: " << std::endl << std::flush;
    }
    this->tttr = p2.tttr;
    // public attributes
    settings = p2.settings;
    n_frames = p2.n_frames;
    n_lines = p2.n_lines;
    n_pixel = p2.n_pixel;
    if (verbose) {
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    }
}

CLSMImage::CLSMImage(const CLSMImage &p2, bool fill) {
    copy(p2, fill);
}

//void CLSMImage::shift_line_start(int macro_time_shift){
//#ifdef VERBOSE_TTTRLIB
//    std::clog << "-- Shifting line start by [macro time clocks]: " << macro_time_shift << std::endl;
//#endif
//    for(auto &frame : get_frames()){
//        for(auto &line: frame->get_lines()){
//            line->shift_start_time(macro_time_shift);
//        }
//    }
//}

void CLSMImage::determine_number_of_lines() {
    if (is_verbose()) {
        std::clog << "-- CLSMImage::determine_number_of_lines" << std::endl;
    }
    n_lines = 0;
    for (auto &f: frames) {
        if (f.get_lines().size() > n_lines)
            n_lines = f.get_lines().size();
    }
}

CLSMImage::CLSMImage(
    std::shared_ptr<TTTR> tttr_data,
    CLSMSettings settings,
    CLSMImage *source,
    bool fill,
    std::vector<int> channels,
    std::vector<std::pair<int, int> > micro_time_ranges
) {
    if (is_verbose()) {
        std::clog << "Initializing CLSM image" << std::endl;
    }

    if (source != nullptr) {
        // Copy‐constructor path remains unchanged
        if (is_verbose()) {
            std::clog << "-- Copying data from other object" << std::endl;
        }
        copy(*source, fill);
    } else {
        if (is_verbose()) {
            std::clog << "-- Initializing new CLSM image..." << std::endl;
        }

        this->settings = settings;
        this->n_pixel = settings.n_pixel_per_line;
        tttr = tttr_data;

        // Early exit if TTTR pointer missing or no records
        if (tttr.get() == nullptr) {
            std::clog << "WARNING: No TTTR object provided" << std::endl;
            return;
        }
        if (tttr_data->n_records_read == 0) {
            std::clog << "WARNING: No records in TTTR object" << std::endl;
            return;
        }

        // “No frame marker” case ===
        if (this->settings.marker_frame_start.empty()) {
            std::clog << "WARNING: No frame marker provided - creating a single full-span frame" << std::endl;

            // Determine total number of valid events
            int n_events = static_cast<int>(tttr_data->get_n_valid_events()); // or tttr_data->n_valid_events

            // Manually create one frame covering all events [0, n_events)
            auto singleFrame = CLSMFrame(0, n_events, tttr);
            singleFrame.set_tttr(tttr);
            frames.emplace_back(std::move(singleFrame));

            // We know there will be exactly one frame
            n_frames = 1;

            // Now proceed to line creation as if there were a “frame edge” pair
            create_lines();

            // Figure out how many lines exist and remove incomplete ones
            determine_number_of_lines();
            remove_incomplete_frames();

            // Finally create pixel containers within each line
            create_pixels_in_lines();
        } else {
            // “frame marker provided” path ===
            create_frames(true);
            create_lines();

            if (is_verbose()) {
                std::clog << "-- Initial number of frames: " << n_frames << std::endl;
                std::clog << "-- Lines per frame: " << n_lines << std::endl;
            }
            determine_number_of_lines();
            remove_incomplete_frames();
            create_pixels_in_lines();
        }
    }

    // If user asked to fill with photons (and specified channels), do so
    if (fill && !channels.empty()) {
        this->fill(this->tttr, channels, false, micro_time_ranges);
    }
    // (Optionally shift line start if settings.macro_time_shift != 0)
    // if (settings.macro_time_shift != 0)
    //     shift_line_start(settings.macro_time_shift);
}

void CLSMImage::create_pixels_in_lines() {
    if (is_verbose()) {
        std::clog << "-- CLSMImage::create_pixels_in_lines" << std::endl;
    }
    // by improving here, a factor of two in speed could be possible.
    for (auto &frame : frames) {
        for (auto &line : frame.get_lines()) {
            line.pixels.reserve(n_pixel);
            line.pixels.resize(n_pixel);
        }
    }
    if (is_verbose()) {
        std::clog << "-- Number of pixels per line: " << n_pixel << std::endl;
    }
}

void CLSMImage::append(CLSMFrame frame) {
    frames.emplace_back(std::move(frame));
    ++n_frames;
}

void CLSMImage::rebin(int bin_line, int bin_pixel) {
    std::vector<unsigned int> mapping;
    int n_px = static_cast<int>(n_frames * n_lines * n_pixel);
    mapping.reserve(2 * n_px);
    for (unsigned int f = 0; f < n_frames; f++) {
        for (unsigned int l = 0; l < n_lines; l++) {
            for (unsigned int p = 0; p < n_pixel; p++) {
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
    TTTR *tttr,
    int start_event,
    int stop_event,
    std::vector<int> marker_frame,
    int marker_event_type,
    int reading_routine,
    bool skip_before_first_frame_marker,
    bool skip_after_last_frame_marker) {
    int n_events = static_cast<int>(tttr->get_n_valid_events());
    std::vector<int> frame_edges;
    if (!skip_before_first_frame_marker)
        frame_edges.emplace_back(start_event);
    if (stop_event < 0) stop_event = n_events;

    if (is_verbose()) {
        std::clog << "-- GET_FRAME_EDGES" << std::endl;
        std::clog << "-- Reading routing:" << reading_routine << std::endl;
        std::clog << "-- skip_after_last_frame_marker:" << skip_after_last_frame_marker << std::endl;
        std::clog << "-- skip_before_first_frame_marker:" << skip_before_first_frame_marker << std::endl;
        std::clog << "-- n_events:" << n_events << std::endl;
        std::clog << "-- stop_event:" << stop_event << std::endl;
    }

    for (int i_event = start_event; i_event < stop_event; i_event++) {
        for (auto f: marker_frame) {
            if (reading_routine == CLSM_SP8) {
                if (tttr->routing_channels[i_event] == marker_event_type) {
                    if (f == tttr->micro_times[i_event]) {
                        frame_edges.emplace_back(i_event);
                        break;
                    }
                }
            } else if (reading_routine == CLSM_SP5) {
                if (f == tttr->routing_channels[i_event]) {
                    frame_edges.emplace_back(i_event);
                    break;
                }
            } else {
                if (tttr->event_types[i_event] == marker_event_type) {
                    if (f == tttr->routing_channels[i_event]) {
                        frame_edges.emplace_back(i_event);
                        break;
                    }
                }
            }
        }
    }
    if (!skip_after_last_frame_marker) {
        frame_edges.emplace_back(n_events);
    }
    if (is_verbose()) {
        std::clog << "-- number of frame edges:" << frame_edges.size() << std::endl;
    }
    return frame_edges;
}

std::vector<int> CLSMImage::get_line_edges(
    TTTR *tttr,
    int start_event, int stop_event,
    int marker_line_start, int marker_line_stop,
    int marker_event_type,
    int reading_routine
) {
    if (is_verbose()) {
        std::clog << "CLSMImage::get_line_edges" << std::endl;
    }
    std::vector<int> line_edges;
    if (stop_event < 0)
        stop_event = tttr->n_valid_events;

    if (reading_routine == CLSM_SP5) {
        line_edges.emplace_back(start_event);
    }

    for (int i_event = start_event; i_event < stop_event; i_event++) {
        if (reading_routine == CLSM_SP8) {
            if (tttr->routing_channels[i_event] == marker_event_type) {
                if (tttr->micro_times[i_event] == marker_line_start) {
                    line_edges.emplace_back(i_event);
                    continue;
                }
                if (tttr->micro_times[i_event] == marker_line_stop) {
                    line_edges.emplace_back(i_event);
                    continue;
                }
            }
        } else if (reading_routine == CLSM_SP5) {
            if (tttr->event_types[i_event] == marker_event_type) {
                if (tttr->routing_channels[i_event] == marker_line_start) {
                    line_edges.emplace_back(i_event);
                    continue;
                }
                if (tttr->routing_channels[i_event] == marker_line_stop) {
                    line_edges.emplace_back(i_event);
                    continue;
                }
            }
        } else {
            if (tttr->event_types[i_event] == marker_event_type) {
                if (tttr->routing_channels[i_event] == marker_line_start) {
                    line_edges.emplace_back(i_event);
                    continue;
                } else if (tttr->routing_channels[i_event] == marker_line_stop) {
                    line_edges.emplace_back(i_event);
                    continue;
                }
            }
        }
    }

    return line_edges;
}


std::vector<int> CLSMImage::get_line_edges_by_duration(
    TTTR *tttr,
    int frame_start,
    int frame_stop,
    int marker_line_start,
    int line_duration,
    int marker_event_type,
    int reading_routine
) {
    if (is_verbose()) {
        std::clog << "CLSMImage::get_line_edges_by_duration" << std::endl;
        std::clog << "-- frame_start: " << frame_start << std::endl;
        std::clog << "-- frame_stop: " << frame_stop << std::endl;
        std::clog << "-- marker_line_start: " << marker_line_start << std::endl;
        std::clog << "-- line_duration: " << line_duration << std::endl;
        std::clog << "-- marker_event_type: " << marker_event_type << std::endl;
        std::clog << "-- reading_routine: " << reading_routine << std::endl;
    }

    if (frame_stop < 0) frame_stop = tttr->n_valid_events;
    std::vector<int> line_edges;
    for (int i_event = frame_start; i_event < frame_stop; i_event++) {
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


void CLSMImage::create_frames(bool clear_first) {
    if (clear_first) frames.clear();
    auto frame_edges = get_frame_edges(
        tttr.get(),
        0, -1,
        settings.marker_frame_start,
        settings.marker_event_type,
        settings.reading_routine,
        settings.skip_before_first_frame_marker,
        settings.skip_after_last_frame_marker
    );
    if (is_verbose()) {
        std::clog << "-- CREATE_FRAMES" << std::endl;
        std::cout << "-- Creating " << frame_edges.size() << " frames: " << std::flush;
    }
    if (frame_edges.size() > 1) {
        frames.reserve(frame_edges.size() - 1);
    }
    for (size_t i = 0; i < frame_edges.size() - 1; ++i) {
        CLSMFrame frame(frame_edges[i], frame_edges[i + 1], tttr);
        frame.set_tttr(tttr);
        append(std::move(frame));
        if (is_verbose()) {
            std::cout << " " << i << std::flush;
        }
    }
    if (is_verbose()) {
        std::cout << std::endl;
        std::clog << "-- Initial number of frames: " << n_frames << std::endl;
    }
}

void CLSMImage::create_lines() {
    // create new lines in every frame
    auto verbose = is_verbose();
    if (verbose) {
        std::clog << "CLSMIMAGE::CREATE_LINES" << std::endl;
        std::clog << "-- Frame start, frame stop idx:" << std::endl;
    }
    for (auto &frame : frames) {
        if (verbose) {
            std::clog << "\t" << frame.get_start() << ", " << frame.get_stop() << std::endl;
        }
        int pixel_duration = (settings.marker_line_stop < 0) ? tttr->header->get_pixel_duration() : -1;
        if (verbose) {
            std::clog << "-- find line edges" << std::endl;
        }
        // find line edges
        std::vector<int> line_edges;
        // reserve an upper bound to avoid reallocations
        line_edges.reserve((frame.get_stop() - frame.get_start()) / 2 + 1);
        if (settings.marker_line_stop >= 0) {
            line_edges = get_line_edges(
                tttr.get(),
                frame.get_start(),
                frame.get_stop(),
                settings.marker_line_start,
                settings.marker_line_stop,
                settings.marker_event_type,
                settings.reading_routine
            );
        } else {
            long line_duration = tttr->header->get_line_duration();
            line_edges = get_line_edges_by_duration(
                tttr.get(),
                frame.get_start(),
                frame.get_stop(),
                settings.marker_line_start,
                line_duration,
                settings.marker_event_type,
                settings.reading_routine
            );
        }

        if (verbose) {
            std::clog << "-- append lines to frames" << std::endl;
        }
        frame.reserve_lines(line_edges.size() / 2);
        for (size_t i_line = 0; i_line < line_edges.size() / 2; i_line++) {
            auto line_start = line_edges[(i_line * 2) + 0];
            auto line_stop = line_edges[(i_line * 2) + 1];
            CLSMLine line;
            line._tttr_indices.insert(line_start);
            line._tttr_indices.insert(line_stop);
            line.set_tttr(tttr);
            line.set_pixel_duration(pixel_duration);
            frame.append(line);
        }
    }
}

void CLSMImage::remove_incomplete_frames() {
    if (is_verbose()) {
        std::clog << "-- Removing incomplete frames" << std::endl;
    }
    std::vector<CLSMFrame> complete_frames;
    complete_frames.reserve(frames.size());
    n_frames = frames.size();
    size_t i_frame = 0;
    for (auto &frame : frames) {
        if (frame.get_lines().size() == n_lines) {
            complete_frames.emplace_back(std::move(frame));
        }
        if (is_verbose()) {
            std::cerr << "WARNING: Frame " << i_frame + 1 << " / " << frames.size()  << " incomplete." << std::endl;
        }
        i_frame++;
    }
    frames = std::move(complete_frames);
    n_frames = frames.size();
    if (is_verbose()) {
        std::clog << "-- Final number of frames: " << n_frames << std::endl;
    }
}

void CLSMImage::clear() {
    if (is_verbose()) {
        std::clog << "Clear pixels of photons" << std::endl;
    }
    _is_filled_ = false;
    for (auto &frame : frames) {
        for (auto &line : frame.get_lines()) {
            for (auto &pixel : line.pixels) {
                pixel.clear();
            }
        }
    }
}

void CLSMImage::strip(const std::vector<int> &tttr_indices, int offset) {
    for (auto &f : get_frames()) {
        for (auto &l : f.get_lines()) {
            for (auto &p : l.get_pixels()) {
                offset = p.strip(tttr_indices, offset);
            }
        }
    }
}

void CLSMImage::fill(
    std::shared_ptr<TTTR> tttr_data,
    std::vector<int> channels,
    bool clear,
    const std::vector<std::pair<int, int> > &micro_time_ranges
) {
    if (is_verbose()) {
        std::clog << "-- Filling pixels..." << std::endl;
        std::clog << "-- Channels: ";
        for (auto ch: channels) std::clog << ch << " ";
        std::clog << std::endl;
        std::clog << "-- Clear pixel before fill: " << clear << std::endl;
        std::clog << "-- Assign photons to pixels" << std::endl;
        std::clog << "-- Micro time ranges: ";
        for (auto r: micro_time_ranges) {
            std::clog << "(" << r.first << "," << r.second << ") ";
        }
        std::clog << std::endl;
    }

    // If no TTTR data pointer was passed in, use the stored one
    if (tttr_data == nullptr) {
        tttr_data = tttr;
    }

    // If the channel list is empty, query all used routing channels from TTTR
    if (channels.empty()) {
        std::clog << "WARNING: Image filled without channel numbers. Using all channels." << std::endl;
        signed char *chs;
        int nchs;
        tttr_data->get_used_routing_channels(&chs, &nchs);
        for (int i = 0; i < nchs; i++) {
            channels.emplace_back(chs[i]);
        }
        free(chs);
    }

    // Iterate over each frame in the image
    for (auto &frame : frames) {
        // We need the line index to decide if a line is "reversed"
        for (size_t l_idx = 0; l_idx < frame.get_lines().size(); ++l_idx) {
            CLSMLine *line = const_cast<CLSMLine*>(&frame.get_lines()[l_idx]);

            // Warn if a line has no pixel vector allocated
            if (line->pixels.empty()) {
                std::clog << "WARNING: Line without pixel." << std::endl;
                continue;
            }

            // If requested, clear each pixel's stored photon indices before filling
            if (clear) {
                for (auto &p: line->pixels) {
                    p.clear();
                }
            }

            // Retrieve how long each pixel spans in macro‐time clocks
            auto pixel_duration = line->get_pixel_duration();
            size_t n_pixels_in_line = line->pixels.size();

            // Determine if this line was scanned in reverse order (odd‐indexed lines)
            bool is_reversed = settings.bidirectional_scan && ((l_idx % 2) == 1);

            // Get the event indices that bound this line in the TTTR record array
            int start_idx = line->get_start();
            int stop_idx = line->get_stop();

            // The macro‐time clock at which this line began
            unsigned long long line_start_time = line->get_start_time(tttr_data);

            // Walk through each TTTR event that falls within this line's event indices
            for (int event_i = start_idx; event_i < stop_idx; ++event_i) {
                // Only process photon events, skip all others
                if (tttr_data->event_types[event_i] != RECORD_PHOTON) {
                    continue;
                }

                // Pull the routing channel for this photon event
                auto c = tttr_data->routing_channels[event_i];

                // Check if this photon belongs to any of the selected channels
                for (auto &ci: channels) {
                    if (c == ci) {
                        // Compute the "raw" pixel index as if scanning left→right
                        int raw_pixel = static_cast<int>(
                            (tttr_data->macro_times[event_i] - line_start_time)
                            / pixel_duration
                        );

                        // If the computed raw index falls outside the line, skip
                        if (raw_pixel < 0 || raw_pixel >= static_cast<int>(n_pixels_in_line)) {
                            break;
                        }

                        // If the line was scanned right→left, flip the raw index
                        size_t pixel_nbr = static_cast<size_t>(
                            is_reversed
                                ? (static_cast<int>(n_pixels_in_line) - 1 - raw_pixel)
                                : raw_pixel
                        );

                        // Verify that the photon’s micro time falls within the specified ranges
                        bool add_ph = true;
                        auto micro_time = tttr_data->micro_times[event_i];
                        for (auto r: micro_time_ranges) {
                            add_ph &= (micro_time >= r.first);
                            add_ph &= (micro_time <= r.second);
                        }

                        // If it passes the micro‐time filter, insert the event index
                        if (add_ph) {
                            line->pixels[pixel_nbr].insert(event_i);
                        }
                    }
                }
            }
        }
    }

    // Mark the image as filled so downstream methods know pixels are populated
    _is_filled_ = true;
}

void CLSMImage::get_intensity(unsigned short **output, int *dim1, int *dim2, int *dim3) {
    *dim1 = (int) n_frames;
    *dim2 = (int) n_lines;
    *dim3 = (int) n_pixel;
    size_t n_pixel_total = n_frames * n_pixel * n_lines;
    if (is_verbose()) {
        std::clog << "Get intensity image" << std::endl;
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel <<
                std::endl;
        std::clog << "-- Total number of pixels: " << n_pixel_total << std::endl;
    }
    auto *t = (unsigned short *) calloc(n_pixel_total + 1, sizeof(unsigned short));
    size_t i_frame = 0;
    size_t t_pixel = 0;
    for (auto &frame : frames) {
        size_t i_line = 0;
        for (auto &line : frame.get_lines()) {
            size_t i_pixel = 0;
            for (auto &pixel : line.pixels) {
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) +
                                   i_line * (n_pixel) +
                                   i_pixel;
                t[pixel_nbr] = static_cast<unsigned short>(pixel._tttr_indices.size());
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
    TTTR *tttr_data,
    unsigned char **output, int *dim1, int *dim2, int *dim3, int *dim4,
    int micro_time_coarsening,
    bool stack_frames
) {
    if (is_verbose()) {
        std::clog << "Get decay image" << std::endl;
    }
    size_t nf = (stack_frames) ? 1 : n_frames;
    size_t n_tac = tttr_data->header->get_number_of_micro_time_channels() / micro_time_coarsening;
    *dim1 = static_cast<int>(nf);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    *dim4 = static_cast<int>(n_tac);

    size_t n_tac_total = nf * n_lines * n_pixel * n_tac;
    auto *t = (unsigned char *) calloc(n_tac_total, sizeof(unsigned char));
    if (is_verbose()) {
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel <<
                std::endl;
        std::clog << "-- Number of micro time channels: " << n_tac << std::endl;
        std::clog << "-- Micro time coarsening factor: " << micro_time_coarsening << std::endl;
        std::clog << "-- Final number of micro time channels: " << n_tac << std::endl;
    }
    size_t i_frame = 0;
    for (auto &frame : frames) {
        size_t i_line = 0;
        for (auto &line : frame.get_lines()) {
            size_t i_pixel = 0;
            for (size_t pixel_i = 0; pixel_i < n_pixel; pixel_i++) {
                auto pixel = line.pixels[pixel_i];
                size_t pixel_nbr = i_frame * (n_lines * n_pixel * n_tac) +
                                   i_line * (n_pixel * n_tac) +
                                   i_pixel * (n_tac);
                for (auto i: pixel._tttr_indices) {
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
    float **output, int *dim1, int *dim2, int *dim3, int *dim4,
    std::shared_ptr<TTTR> tttr,
    CLSMImage *clsm_other,
    const std::string correlation_method,
    const int n_bins, const int n_casc,
    const bool stack_frames,
    const bool normalized_correlation,
    const int min_photons
) {
    if (is_verbose()) {
        std::clog << "Get fluorescence correlation image" << std::endl;
    }
    size_t nf = (stack_frames) ? 1 : n_frames;
    auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
    size_t n_corr = corr.curve.size();
    size_t n_cor_total = nf * n_lines * n_pixel * n_corr;
    auto t = (float *) calloc(n_cor_total, sizeof(float));
    if (is_verbose()) {
        std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
        std::clog << "-- Number of correlation blocks: " << n_casc << std::endl;
        std::clog << "-- Number of correlation bins per block: " << n_bins << std::endl;
        std::clog << "-- Number of correlation channels: " << n_corr << std::endl;
        std::clog << "-- Correlating... " << n_corr << std::endl;
    }
    size_t o_frame = 0;
    //#pragma omp parallel for default(none) shared(tttr, o_frame, t, clsm_other)
    for (unsigned int i_frame = 0; i_frame < n_frames; i_frame++) {
        auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
        auto frame = frames[i_frame];
        auto other_frame = clsm_other->frames[i_frame];
        for (unsigned int i_line = 0; i_line < n_lines; i_line++) {
            auto line = frame.get_lines()[i_line];
            auto other_line = other_frame.get_lines()[i_line];
            for (unsigned int i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                auto pixel = line.pixels[i_pixel];
                auto other_pixel = other_line.pixels[i_pixel];
                if (
                    ((int) pixel._tttr_indices.size() > min_photons) &&
                    ((int) other_pixel._tttr_indices.size() > min_photons)
                ) {
                    auto v1 = pixel.get_tttr_indices();
                    auto tttr_1 = TTTR(
                        *tttr,
                        v1.data(),
                        static_cast<int>(v1.size()),
                        false
                    );
                    auto v2 = other_pixel.get_tttr_indices();
                    auto tttr_2 = TTTR(
                        *tttr,
                        v2.data(),
                        static_cast<int>(v2.size()),
                        false
                    );
                    corr.set_tttr(
                        std::make_shared<TTTR>(tttr_1),
                        std::make_shared<TTTR>(tttr_2)
                    );
                    double *correlation;
                    int temp;
                    if (!normalized_correlation) {
                        corr.get_corr(&correlation, &temp);
                    } else {
                        corr.get_corr_normalized(&correlation, &temp);
                    }
                    for (size_t i_corr = 0; i_corr < n_corr; i_corr++) {
                        t[o_frame * (n_lines * n_pixel * n_corr) +
                          i_line * (n_pixel * n_corr) +
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
    *dim1 = static_cast<int>(nf);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    *dim4 = static_cast<int>(n_corr);
}

void CLSMImage::get_decay_of_pixels(
    TTTR *tttr_data,
    uint8_t *mask, int dmask1, int dmask2, int dmask3,
    unsigned int **output, int *dim1, int *dim2,
    int tac_coarsening,
    bool stack_frames
) {
    size_t n_decays = stack_frames ? 1 : n_frames;
    size_t n_tac = tttr_data->header->get_number_of_micro_time_channels() / tac_coarsening;
    *dim1 = static_cast<int>(n_decays);
    *dim2 = static_cast<int>(n_tac);

    size_t n_tac_total = n_decays * n_tac;
    auto *t = (unsigned int *) calloc(n_tac_total, sizeof(unsigned int));
    if (is_verbose()) {
        std::clog << "Get decays:" << std::endl;
        std::clog << "-- Number of frames: " << n_frames << std::endl;
        std::clog << "-- Stack frames (true/false): " << stack_frames << std::endl;
        std::clog << "-- Number of decays: " << n_decays << std::endl;
        std::clog << "-- Number of micro time channels: " << tttr_data->header->get_number_of_micro_time_channels() <<
                std::endl;
        std::clog << "-- Micro time coarsening: " << tac_coarsening << std::endl;
        std::clog << "-- Resulting number of micro time channels: " << n_tac << std::endl;
    }
    size_t w_frame = 0;
    for (size_t i_frame = 0; i_frame < n_frames; i_frame++) {
        auto frame = frames[i_frame];
        for (size_t i_line = 0; i_line < n_lines; i_line++) {
            auto line = frame.get_lines()[i_line];
            for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                auto pixel = line.pixels[i_pixel];
                if (mask[i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel]) {
                    for (auto i: pixel._tttr_indices) {
                        size_t i_tac = tttr_data->micro_times[i] / tac_coarsening;
                        t[w_frame * n_tac + i_tac] += 1;
                    }
                }
            }
        }
        w_frame += !stack_frames;
    }
    *output = t;
}

void CLSMImage::get_mean_micro_time(
    TTTR *tttr_data,
    double **output, int *dim1, int *dim2, int *dim3,
    double microtime_resolution,
    int minimum_number_of_photons,
    bool stack_frames
) {
#ifdef VERBOSE_TTTRLIBLIBLIBLIBLIBLIBLIB
    std::clog << "Get mean micro time image" << std::endl;
    std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
    std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
    std::clog << "-- Computing stack of mean micro times " << std::endl;
#endif
    if (microtime_resolution < 0)
        microtime_resolution = tttr_data->header->get_micro_time_resolution();
    if (!stack_frames) {
        auto *t = (double *) malloc(n_frames * n_lines * n_pixel * sizeof(double));
        for (size_t i_frame = 0; i_frame < n_frames; i_frame++) {
            for (size_t i_line = 0; i_line < n_lines; i_line++) {
                for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel;
                    CLSMPixel px = frames[i_frame].get_lines()[i_line].pixels[i_pixel];
                    t[pixel_nbr] = px.get_mean_microtime(tttr_data, microtime_resolution, minimum_number_of_photons);
                }
            }
        }
        *dim1 = (int) n_frames;
        *dim2 = (int) n_lines;
        *dim3 = (int) n_pixel;
        *output = t;
    } else {
        int w_frame = 1;
        if (is_verbose()) {
            std::clog << "-- Compute photon weighted average over frames" << std::endl;
        }
        auto *r = (double *) malloc(sizeof(double) * w_frame * n_lines * n_pixel);
        for (size_t i_line = 0; i_line < n_lines; i_line++) {
            for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                size_t pixel_nbr = i_line * n_pixel + i_pixel;
                // average the arrival times over the frames
                r[pixel_nbr] = 0.0;
                std::vector<int> tr;
                for (size_t i_frame = 0; i_frame < n_frames; i_frame++) {
                    auto temp = frames[i_frame].get_lines()[i_line].pixels[i_pixel]._tttr_indices;
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
    double g_irf = 1.0, s_irf = 0.0;
    if (frequency < 0) {
        auto header = tttr_data->get_header();
        auto macro_time_res = header->get_macro_time_resolution();
        auto micro_time_res = header->get_micro_time_resolution();
        frequency = micro_time_res / macro_time_res;
    }
    if (tttr_irf != nullptr) {
        std::vector<double> gs = DecayPhasor::compute_phasor(
            tttr_irf->micro_times, tttr_irf->n_valid_events,
            frequency
        );
        g_irf = gs[0];
        s_irf = gs[1];
    }
    int o_frames = stack_frames ? 1 : n_frames;
    double factor = (2. * frequency * M_PI);
    auto *t = (float *) calloc(o_frames * n_lines * n_pixel * 2, sizeof(float));
    for (int i_line = 0; i_line < n_lines; i_line++) {
        for (int i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
            if (stack_frames) {
                std::vector<int> idxs = {};
                size_t pixel_nbr = i_line * (n_pixel * 2) + i_pixel * 2;
                for (int i_frame = 0; i_frame < n_frames; i_frame++) {
                    auto n = frames[i_frame].get_lines()[i_line].pixels[i_pixel]._tttr_indices;
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
            } else {
                for (int i_frame = 0; i_frame < n_frames; i_frame++) {
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel * 2) + i_line * (n_pixel * 2) + i_pixel * 2;
                    auto s = frames[i_frame].get_lines()[i_line].pixels[i_pixel].get_tttr_indices();
                    std::vector<int> t(s.begin(), s.end());
                    auto r = DecayPhasor::compute_phasor(
                        tttr_data->micro_times, tttr_data->n_valid_events,
                        frequency,
                        minimum_number_of_photons,
                        g_irf, s_irf,
                        &t
                    );
                    t[pixel_nbr + 0] = (float) r[0];
                    t[pixel_nbr + 1] = (float) r[1];
                }
            }
        }
    }
    if (is_verbose()) {
        std::clog << "GET_PHASOR_IMAGE..." << std::endl;
        std::clog << "-- frequency [GHz]: " << frequency << std::endl;
        std::clog << "-- stack_frames: " << stack_frames << std::endl;
        std::clog << "-- minimum_number_of_photons: " << minimum_number_of_photons << std::endl;
    }
    *dim1 = (int) o_frames;
    *dim2 = (int) n_lines;
    *dim3 = (int) n_pixel;
    *dim4 = (int) 2;
    *output = t;
}

void CLSMImage::get_mean_lifetime(
    TTTR *tttr_data,
    double **output, int *dim1, int *dim2, int *dim3,
    const int minimum_number_of_photons,
    TTTR *tttr_irf, double m0_irf, double m1_irf,
    bool stack_frames,
    std::vector<double> background,
    double m0_bg, double m1_bg,
    double background_fraction
) {
    const double dt = tttr_data->header->get_micro_time_resolution() * 1E9;
    if (is_verbose()) {
        std::clog << "Compute a mean lifetime image (Isenberg 1973)" << std::endl;
        std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
        std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
        std::clog << "-- Micro time resolution [ns]: " << dt << std::endl;
    }
    if (tttr_irf != nullptr) {
        unsigned short *micro_times_irf;
        int n_micro_times_irf;
        tttr_irf->get_micro_times(&micro_times_irf, &n_micro_times_irf);
        if (is_verbose()) {
            std::clog << "-- Computing first moments (m0, m1) of IRF using TTTR data " << std::endl;
            std::clog << "-- n_micro_times_irf:" << n_micro_times_irf << std::endl;
        }
        m0_irf = n_micro_times_irf; // number of photons
        m1_irf = 0.0;
        for (int i = 0; i < n_micro_times_irf; i++) m1_irf += (double) micro_times_irf[i];
    }
    if (is_verbose()) {
        std::clog << "-- IRF m0: " << m0_irf << std::endl;
        std::clog << "-- IRF m1: " << m1_irf << std::endl;
    }
    size_t o_frames = stack_frames ? 1 : n_frames;

    // Compute m0, m1 to minimize repeated computations for pixel
    if (!background.empty()) {
        m0_bg = 0.0;
        m1_bg = 0.0;
        for (int i = 0; i < background.size(); i++) {
            m0_bg += background[i];
            m1_bg += i * background[i];
        }
    }
    if (is_verbose()) {
        std::clog << "-- BG m0: " << m0_bg << std::endl;
        std::clog << "-- BG m1: " << m1_bg << std::endl;
    }

    auto *t = (double *) calloc(o_frames * n_lines * n_pixel, sizeof(double));
    for (int i_frame = 0; i_frame < o_frames; i_frame++) {
        for (int i_line = 0; i_line < n_lines; i_line++) {
            for (int i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel;
                if (stack_frames) {
                    std::vector<int> tttr_indices;
                    for (auto &frame : frames) {
                        auto px = frame.get_lines()[i_line].pixels[i_pixel];
                        tttr_indices.insert(tttr_indices.end(),
                                            px._tttr_indices.begin(), px._tttr_indices.end());
                    }
                    t[pixel_nbr] = TTTRRange::compute_mean_lifetime(
                        tttr_indices, tttr_data, minimum_number_of_photons,
                        nullptr, m0_irf, m1_irf, dt,
                        nullptr, m0_bg, m1_bg,
                        background_fraction
                    );
                } else {
                    auto px = this->frames[i_frame].get_lines()[i_line].pixels[i_pixel];
                    t[pixel_nbr] =
                            px.get_mean_lifetime(
                                tttr_data,
                                minimum_number_of_photons,
                                nullptr, m0_irf, m1_irf, dt,
                                nullptr, m0_bg, m1_bg,
                                background_fraction
                            );
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
    double **output, int *dim1, int *dim2, int *dim3,
    CLSMImage *clsm,
    std::vector<int> x_range,
    std::vector<int> y_range,
    std::string subtract_average,
    double background,
    bool clip, double clip_max, double clip_min,
    double *images, int n_frames, int n_lines, int n_pixel,
    uint8_t *mask, int dmask1, int dmask2, int dmask3,
    std::vector<int> selected_frames
) {
    if (is_verbose()) {
        std::clog << "CREATE ROI" << std::endl;
    }
    // determine the total number of frames, lines, and pixel in the input
    size_t nf, nl, np; // the number frames, lines, and pixel in the input
    TTTR *tttr_data = nullptr;
    if (clsm != nullptr) {
        if (is_verbose()) {
            std::clog << "-- Using CLSM/TTTR data" << std::endl;
        }
        if (clsm->tttr == nullptr)
            std::cerr << "WARNING: CLSM has no TTTR data associated." << std::endl;
        tttr_data = clsm->tttr.get();
        nf = clsm->n_frames;
        nl = clsm->n_lines;
        np = clsm->n_pixel;
    } else if (
        (images != nullptr) &&
        (n_frames > 0) &&
        (n_lines > 0) &&
        (n_pixel > 0)
    ) {
        if (is_verbose()) {
            std::clog << "-- Using image array input" << std::endl;
        }
        nf = n_frames;
        nl = n_lines;
        np = n_pixel;
    } else {
        std::cerr << "ERROR: No input data specified!" << std::endl;
    }
    if (is_verbose()) {
        std::clog << "-- Input number of frames: " << nf << std::endl;
        std::clog << "-- Input number of lines: " << nl << std::endl;
        std::clog << "-- Input number of pixel: " << np << std::endl;
    }
    // Determine mask
    bool use_mask =
            (dmask1 > 0) &&
            (dmask2 > 0) &&
            (dmask3 > 0) &&
            (mask != nullptr);

    // determine ROI range
    // if no stop specified (-1) use n_pixel, n_lines as stop
    int start_x = x_range[0];
    int stop_x = x_range[1];
    int start_y = y_range[0];
    int stop_y = y_range[1];
    stop_x = (stop_x < 0) ? np : stop_x % np;
    stop_y = (stop_y < 0) ? nl : stop_y % nl;

    // Compute the shape of the output array
    int ncol_roi = stop_x - start_x;
    int nrows_roi = stop_y - start_y;
    int nframes_roi = static_cast<int>(selected_frames.size());
    int pixel_in_roi = nrows_roi * ncol_roi;

    if (is_verbose()) {
        std::clog << "-- ROI (x0, x1, y0, y1): " <<
                start_x << ", " << stop_x << ", " <<
                start_y << ", " << stop_y << std::endl;
        std::clog << "-- ROI size (nx, ny): " << ncol_roi << ", " << nrows_roi << std::endl;
        std::clog << "-- Number of pixel in ROI: " << pixel_in_roi << std::endl;
    }
    if (selected_frames.empty()) {
        if (is_verbose()) {
            std::clog << "-- No frames specified, using all frames in input" << std::endl;
        }
        selected_frames.reserve(nf);
        for (int i = 0; i < nf; i++) selected_frames.emplace_back(i);
        nframes_roi = static_cast<int>(selected_frames.size());
    }
    if (is_verbose()) {
        if (use_mask)
            std::clog << "-- Using selection mask." << std::endl;
        else
            std::clog << "-- No mask mask specified." << std::endl;
    }
    // Check size of mask and give warning if mask size does not match ROI
    if (
        ((nf != dmask1) || (nl != dmask2) || (np != dmask3)) &&
        use_mask
    )
        std::clog << "WARNING: Selection mask size and ROI size do not match!" << std::endl;
    std::vector<bool> mask_v(nf * nl * np,true);
    // copy the values from the input to the mask
    for (int f = 0; f < nf; f++)
        for (int l = 0; (l < nl) && (l < dmask2); l++)
            for (int p = 0; (p < np) && (p < dmask3); p++) {
                // in cases the number of frames in mask is smaller than the
                // number of frames in ROI use first frame in mask
                int fi = (f < dmask1) ? f : 1;
                mask_v[f * nl * np + l * np + p] = mask[fi * dmask1 * dmask2 + l * dmask2 + p];
            }
    if (is_verbose()) {
        std::clog << "-- Copying image to ROI array... " << std::endl;
        std::clog << "-- Frames in ROI: " << nframes_roi << std::endl;
    }
    auto *img_roi = (double *) calloc(nframes_roi * pixel_in_roi, sizeof(double));
    if (is_verbose()) {
        std::clog << "-- Copying frame: ";
    }
    int current_pixel = 0;
    for (auto f: selected_frames) {
        if (is_verbose()) {
            std::clog << f << " ";
        }
        for (int l = start_y; l < stop_y; l++) {
            for (int p = start_x; p < stop_x; p++) {
                if (mask_v[current_pixel]) {
                    double value;
                    if (clsm != nullptr) {
                        auto frame = clsm->frames[f];
                        auto line = frame.get_lines()[l];
                        value = (double) line.pixels[p]._tttr_indices.size();
                    } else if (images != nullptr) {
                        value = images[f * (nl * np) + l * nl + p];
                    }
                    img_roi[current_pixel] = value;
                } else {
                    img_roi[current_pixel] = 0.0;
                }
                current_pixel++;
            }
        }
    }
    if (is_verbose()) {
        std::clog << std::endl;
        std::clog << "-- Correcting ROI" << std::endl;
    }
    if (background != 0) {
        if (is_verbose()) {
            std::clog << "-- Subtracted background per pixel: " << background << std::endl;
        }
        for (int f = 0; f < nframes_roi; f++) {
            for (int p = 0; p < pixel_in_roi; p++) {
                img_roi[f * pixel_in_roi + p] -= background;
            }
        }
    }
    if (clip) {
        if (is_verbose()) {
            std::clog << "-- Clipping values: " << clip_max << ", " << clip_min << std::endl;
        }
        for (int f = 0; f < nframes_roi; f++) {
            for (int p = 0; p < pixel_in_roi; p++) {
                double value = img_roi[f * pixel_in_roi + p];
                value = std::min(clip_max, value);
                value = std::max(clip_min, value);
                img_roi[f * pixel_in_roi + p] = value;
            }
        }
    }
    if (is_verbose()) {
        std::clog << "-- Subtract average mode: " << subtract_average << std::endl;
    }
    if (subtract_average == "stack") {
        if (is_verbose()) {
            std::clog << "-- Subtract pixel average of all frames." << std::endl;
        }
        auto img_mean = (double *) calloc(pixel_in_roi, sizeof(double));
        double total_count = 0.0;
        for (int f = 0; f < nframes_roi; f++) {
            for (int p = 0; p < pixel_in_roi; p++) {
                double count = img_roi[f * pixel_in_roi + p];
                total_count += count;
                img_mean[p] += count;
            }
        }
        double mean_count = total_count / (nframes_roi * pixel_in_roi);
        for (int p = 0; p < pixel_in_roi; p++) {
            img_mean[p] /= nframes_roi;
        }
        for (int f = 0; f < nframes_roi; f++) {
            for (int p = 0; p < pixel_in_roi; p++) {
                img_roi[f * pixel_in_roi + p] =
                        img_roi[f * pixel_in_roi + p] - img_mean[p] + mean_count;
            }
        }
        free(img_mean);
    } else if (subtract_average == "frame") {
        if (is_verbose()) {
            std::clog << "-- Subtracting average intensity in frame." << std::endl;
        }
        // compute the mean intensity in image and subtract the mean fro
        for (int f = 0; f < nframes_roi; f++) {
            double total_count = 0.0;
            for (int p = 0; p < pixel_in_roi; p++) {
                total_count += img_roi[f * pixel_in_roi + p];
            }
            double mean_count = total_count / (nframes_roi * pixel_in_roi);
            for (int p = 0; p < pixel_in_roi; p++) {
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
    double **output, int *dim1, int *dim2, int *dim3,
    std::shared_ptr<TTTR> tttr_data,
    CLSMImage *clsm,
    double *images, int input_frames, int input_lines, int input_pixel,
    std::vector<int> x_range, std::vector<int> y_range,
    std::vector<std::pair<int, int> > frames_index_pairs,
    std::string subtract_average,
    uint8_t *mask, int dmask1, int dmask2, int dmask3,
    std::vector<int> selected_frames
) {
    using T = double;
    using CT = std::complex<T>;

    // Create ROI
    T *roi = nullptr;
    int nf = 0, nl = 0, np = 0;
    // get_roi fills roi and dimensions; background=0.0, no clipping
    get_roi(&roi, &nf, &nl, &np,
            clsm,
            x_range, y_range,
            subtract_average, 0.0,
            false, 1, 1,
            images, input_frames, input_lines, input_pixel,
            mask, dmask1, dmask2, dmask3,
            selected_frames);
    int pixel_in_roi = nl * np;

    // If no frame pairs provided, default to (i,i)
    if (frames_index_pairs.empty()) {
        frames_index_pairs.reserve(nf);
        for (int i = 0; i < nf; ++i) {
            frames_index_pairs.emplace_back(i, i);
        }
    }

    // Allocate output array
    auto out_tmp = static_cast<T*>(std::calloc(frames_index_pairs.size() * pixel_in_roi, sizeof(T)));

    // FFT buffers
    std::vector<CT> in(pixel_in_roi);
    std::vector<CT> fft_roi1(pixel_in_roi);
    std::vector<CT> fft_roi2(pixel_in_roi);
    std::vector<CT> ics(pixel_in_roi);

    std::ptrdiff_t sd = sizeof(T);
    std::ptrdiff_t sc = sizeof(CT);
    pocketfft::shape_t shape{static_cast<size_t>(nl), static_cast<size_t>(np)};
    pocketfft::stride_t stride_d{sd * np, sd};
    pocketfft::stride_t stride_c{sc * np, sc};
    pocketfft::shape_t axes{0, 1};
    T norm = T(1.0 / pixel_in_roi);

    int current_pair = 0;
    for (auto &pair : frames_index_pairs) {
        // FFT of first ROI
        pocketfft::r2c<T>(shape, stride_d, stride_c, axes, pocketfft::FORWARD,
                         &roi[pair.first * pixel_in_roi], fft_roi1.data(), 1.0);
        // FFT of second ROI (or reuse first if same)
        if (pair.second != pair.first) {
            pocketfft::r2c<T>(shape, stride_d, stride_c, axes, pocketfft::FORWARD,
                             &roi[pair.second * pixel_in_roi], fft_roi2.data(), 1.0);
        } else {
            fft_roi2 = fft_roi1;
        }
        // Multiply FFT1 with conj(FFT2)
        for (size_t i = 0; i < fft_roi1.size(); ++i) {
            in[i] = fft_roi1[i] * std::conj(fft_roi2[i]);
        }
        // Inverse FFT to get ICS
        pocketfft::c2c<T>(shape, stride_c, stride_c, axes, pocketfft::BACKWARD,
                         in.data(), ics.data(), norm);
        // Store real part
        int offset = current_pair * pixel_in_roi;
        for (int i = 0; i < pixel_in_roi; ++i) {
            out_tmp[offset + i] = std::real(ics[i]);
        }
        ++current_pair;
    }
    std::free(roi);

    // Set output pointers and dimensions
    *output = out_tmp;
    *dim1 = nf;
    *dim2 = nl;
    *dim3 = np;
}

void CLSMImage::transform(unsigned int *input, int n_input) {
    CLSMImage *source = new CLSMImage(*this, true);
    CLSMImage *target = this;

    target->clear();
    for (int i = 0; i < n_input; i = i + 2) {
        // source (s)
        CLSMPixel *source_pixel = source->getPixel(input[i + 0]);
        CLSMPixel *target_pixel = target->getPixel(input[i + 1]);
        // Append tttr indices to pixel
        for (auto tr_idx: source_pixel->_tttr_indices) {
            target_pixel->_tttr_indices.insert(tr_idx);
        }
    }

    delete source;
}

void CLSMImage::distribute(
    unsigned int pixel_id,
    CLSMImage *target,
    std::vector<int> &target_pixel_ids,
    std::vector<int> &target_probabilities
) {
    std::cout << "TODO" << std::endl;
}

void CLSMImage::reshape(int new_n_frames, int new_n_lines, int new_n_pixel) {
    if (is_verbose()) {
        std::clog << "-- Reshaping image from ("
                  << n_frames << " × " << n_lines << " × " << n_pixel
                  << ") to ("
                  << new_n_frames << " × " << new_n_lines << " × " << new_n_pixel
                  << ")" << std::endl;
    }

    // 1) Check that total pixel count matches
    size_t old_total = static_cast<size_t>(n_frames) * n_lines * n_pixel;
    size_t new_total = static_cast<size_t>(new_n_frames) * new_n_lines * new_n_pixel;
    if (old_total != new_total) {
        std::cerr << "ERROR: Cannot reshape CLSMImage: total pixels ("
                  << old_total << ") ≠ new layout ("
                  << new_total << ")."
                  << std::endl;
        return;
    }

    // 2) Extract each pixel’s TTTR‐indices into a flat vector.
    //    Match the exact container type (itlib::flat_set<…>) via decltype.
    using PixelSet = decltype(std::declval<CLSMPixel>()._tttr_indices);
    std::vector<PixelSet> flat_pixel_indices;
    flat_pixel_indices.reserve(old_total);

    for (auto &frame : frames) {
        for (auto &line : frame.get_lines()) {
            for (auto &pixel : line.pixels) {
                flat_pixel_indices.push_back(pixel._tttr_indices);
            }
        }
    }

    // 3) Clear old frames (objects will be destroyed automatically)
    frames.clear();

    // 4) Update stored dimensions
    n_frames = new_n_frames;
    n_lines = new_n_lines;
    n_pixel = new_n_pixel;

    // 5) Rebuild a fresh hierarchy (CLSMFrame → CLSMLine → pixels),
    //    moving each saved PixelSet back into its new position.
    size_t idx = 0;
    for (size_t f = 0; f < n_frames; ++f) {
        CLSMFrame new_frame;
        new_frame.set_tttr(tttr);
        for (size_t l = 0; l < n_lines; ++l) {
            CLSMLine new_line;
            new_line.set_tttr(tttr);
            new_line.pixels.reserve(n_pixel);
            new_line.pixels.resize(n_pixel);
            for (size_t p = 0; p < n_pixel; ++p) {
                new_line.pixels[p]._tttr_indices = std::move(flat_pixel_indices[idx++]);
            }
            new_frame.append(new_line);
        }
        frames.emplace_back(std::move(new_frame));
    }

    if (is_verbose()) {
        std::clog << "-- Reshape complete. Now dims are ("
                  << n_frames << " × " << n_lines << " × " << n_pixel << ")." << std::endl;
    }
}

void CLSMImage::crop(
    int frame_start, int frame_stop,
    int line_start, int line_stop,
    int pixel_start, int pixel_stop
) {
    frame_stop = std::min(std::max(0, frame_stop), (int)size());
    frame_start = std::max(0, frame_start);

    if (is_verbose()) {
        std::clog << "Crop image" << std::endl;
        std::clog << "-- Frame range: " << frame_start << ", " << frame_stop << std::endl;
        std::clog << "-- Line range: " << line_start << ", " << line_stop << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
    }

    std::vector<CLSMFrame> frs;
    // Discard frames before start
    for (int i = 0; i < frame_start; ++i) {
        // nothing to delete, just skip
    }
    // Process selected frames
    for (int i = frame_start; i < frame_stop; ++i) {
        CLSMFrame &f = frames[i];
        f.crop(line_start, line_stop, pixel_start, pixel_stop);
        frs.emplace_back(std::move(f));
    }
    // Discard frames after stop (implicitly by not moving them)
    frames = std::move(frs);
    n_frames = frames.size();
    if (!frames.empty()) {
        n_lines = frames[0].get_lines().size();
        if (!frames[0].get_lines().empty()) {
            n_pixel = frames[0].get_lines()[0].pixels.size();
        }
    }
}

void CLSMImage::reserve_pixel_capacity(size_t avg_photons) {
    for (auto &frame : frames) {
        for (auto &line : frame.get_lines()) {
            for (auto &pixel : line.pixels) {
                // itlib::flat_set provides a reserve method via its underlying container
                pixel._tttr_indices.reserve(avg_photons);
            }
        }
    }
}
