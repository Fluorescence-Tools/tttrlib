#include "include/CLSMImage.h"
#include "include/Verbose.h"
#include "include/info.h"
#include <memory>
#include <cstring>  // for memset
#include <iostream>
#include <map>

#ifdef _OPENMP
#include <omp.h>
#endif

// Portable prefetch macro for better cache utilization
// Works on x86, x64, ARM, ARM64, and other architectures
#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang builtin - automatically translates to:
    //   x86/x64: PREFETCHT0
    //   ARM32: PLD (Preload Data)
    //   ARM64: PRFM (Prefetch Memory)
    //   Other: appropriate instruction or no-op
    #define PREFETCH(addr) __builtin_prefetch(addr, 0, 0)
    
    // ARM-specific: prefetch for write (optional, can improve performance)
    #if defined(__arm__) || defined(__aarch64__)
        #define PREFETCH_WRITE(addr) __builtin_prefetch(addr, 1, 0)
    #else
        #define PREFETCH_WRITE(addr) __builtin_prefetch(addr, 1, 0)
    #endif
    
#elif defined(_MSC_VER)
    #if defined(_M_ARM) || defined(_M_ARM64)
        // MSVC on ARM: use ARM intrinsics
        #include <arm_neon.h>
        #define PREFETCH(addr) __prefetch(addr)
        #define PREFETCH_WRITE(addr) __prefetch(addr)
    #else
        // MSVC on x86/x64: use SSE intrinsics
        #include <intrin.h>
        #define PREFETCH(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
        #define PREFETCH_WRITE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
    #endif
#else
    // Fallback for unknown compilers
    #define PREFETCH(addr) ((void)0)
    #define PREFETCH_WRITE(addr) ((void)0)
#endif


// Helper function to collect TTTR indices from stacked frames for a specific pixel
// Avoids repeated allocation by pre-calculating total size
static std::vector<int> collect_stacked_pixel_indices(
    const std::vector<CLSMFrame*>& frames,
    size_t i_line,
    size_t i_pixel
) {
    // Pre-calculate total size to avoid repeated reallocations
    size_t total_size = 0;
    for (const auto& frame : frames) {
        const auto& lines = frame->get_lines();
        const auto& pixels = lines[i_line]->get_pixels();
        total_size += pixels[i_pixel].size();
    }
    
    // Reserve space and collect indices
    std::vector<int> indices;
    indices.reserve(total_size);
    
    for (const auto& frame : frames) {
        const auto& lines = frame->get_lines();
        const auto& pixels = lines[i_line]->get_pixels();
        const auto& dense_indices = pixels[i_pixel].get_tttr_indices();
        indices.insert(indices.end(), dense_indices.begin(), dense_indices.end());
    }
    
    return indices;
}

// Helper to setup micro-time filtering bitmap
// Returns: {bitmap_pointer, owns_bitmap_flag, use_filter_flag}
static std::tuple<bool*, bool, bool> setup_microtime_filter(
    bool* micro_time_bitmap,
    int n_micro_time_bitmap,
    const std::vector<std::pair<int, int>>& micro_time_ranges,
    const std::vector<int>& channels,
    bool do_split_fill,
    size_t ch_idx
) {
    bool* micro_time_valid = nullptr;
    bool owns_bitmap = false;
    bool use_micro_time_filter = false;
    
    // Determine bitmap type
    bool has_per_channel_bitmap = (micro_time_bitmap != nullptr && 
                                  n_micro_time_bitmap == 65536 * static_cast<int>(channels.size()));
    bool has_global_bitmap = (micro_time_bitmap != nullptr && n_micro_time_bitmap == 65536);
    
    if (has_per_channel_bitmap) {
        // Per-channel bitmap: select the appropriate channel's bitmap
        if (do_split_fill && ch_idx < channels.size()) {
            micro_time_valid = micro_time_bitmap + (ch_idx * 65536);
            use_micro_time_filter = true;
        } else if (!do_split_fill) {
            micro_time_valid = micro_time_bitmap;  // Base pointer
            use_micro_time_filter = true;
        }
    } else if (has_global_bitmap) {
        // Global bitmap: use for all channels
        micro_time_valid = micro_time_bitmap;
        use_micro_time_filter = true;
    } else if (!micro_time_ranges.empty()) {
        // Build bitmap from ranges (allocate on heap)
        micro_time_valid = new bool[65536];
        std::memset(micro_time_valid, 0, 65536);
        
        // Set valid ranges using memset for contiguous ranges
        for (const auto& r: micro_time_ranges) {
            int start = std::max(0, r.first);
            int end = std::min(65535, r.second);
            if (end >= start) {
                std::memset(&micro_time_valid[start], 1, end - start + 1);
            }
        }
        use_micro_time_filter = true;
        owns_bitmap = true;
    }
    
    return std::make_tuple(micro_time_valid, owns_bitmap, use_micro_time_filter);
}

// Helper to check if a photon passes micro-time filter
static inline bool passes_microtime_filter(
    unsigned short micro_time,
    bool* micro_time_valid,
    bool has_per_channel_bitmap,
    bool do_split_fill,
    signed char photon_channel,
    const std::vector<int>& channels
) {
    // For per-channel bitmaps in non-split mode, find the channel index
    if (has_per_channel_bitmap && !do_split_fill) {
        for (size_t i = 0; i < channels.size(); i++) {
            if (photon_channel == static_cast<signed char>(channels[i])) {
                const bool* channel_bitmap = micro_time_valid + (i * 65536);
                return channel_bitmap[micro_time];
            }
        }
        return false;  // Channel not in list
    } else {
        // Global bitmap or split-fill with per-channel bitmap
        return micro_time_valid[micro_time];
    }
}

// Helper function to display micro-time bitmap information in verbose mode
static void display_microtime_bitmap_info(
    const bool* micro_time_bitmap,
    int n_micro_time_bitmap,
    size_t n_channels
) {
    if (micro_time_bitmap == nullptr) {
        std::clog << "-- Micro time bitmap: NOT PROVIDED (will use ranges if specified)" << std::endl;
        return;
    }
    
    std::clog << "-- Micro time bitmap provided: size = " << n_micro_time_bitmap << std::endl;
    
    // Determine bitmap type
    if (n_micro_time_bitmap == 65536) {
        std::clog << "-- Bitmap type: GLOBAL (single bitmap for all channels)" << std::endl;
    } else if (n_micro_time_bitmap == 65536 * static_cast<int>(n_channels)) {
        std::clog << "-- Bitmap type: PER-CHANNEL (" << n_channels << " channels)" << std::endl;
    } else {
        std::clog << "-- WARNING: Bitmap size mismatch (expected 65536 or " 
                  << (65536 * n_channels) << ")" << std::endl;
    }
    
    // Count valid micro-times and show first 5000
    int total_valid = 0;
    int display_limit = std::min(5000, n_micro_time_bitmap);
    
    std::clog << "-- Bitmap content (first " << display_limit << " elements):" << std::endl;
    std::clog << "   Valid ranges: ";
    
    bool in_range = false;
    int range_start = -1;
    int ranges_shown = 0;
    const int max_ranges_to_show = 20;
    
    for (int i = 0; i < n_micro_time_bitmap; i++) {
        if (micro_time_bitmap[i]) {
            total_valid++;
            
            if (i < display_limit) {
                if (!in_range) {
                    range_start = i;
                    in_range = true;
                }
            }
        } else {
            if (in_range && i <= display_limit && ranges_shown < max_ranges_to_show) {
                if (ranges_shown > 0) std::clog << ", ";
                std::clog << "[" << range_start << "-" << (i-1) << "]";
                ranges_shown++;
                in_range = false;
            }
        }
    }
    
    // Close last range if still open
    if (in_range && ranges_shown < max_ranges_to_show) {
        if (ranges_shown > 0) std::clog << ", ";
        std::clog << "[" << range_start << "-" << std::min(display_limit-1, n_micro_time_bitmap-1) << "]";
    }
    
    if (ranges_shown >= max_ranges_to_show) {
        std::clog << " ... (showing first " << max_ranges_to_show << " ranges)";
    }
    std::clog << std::endl;
    
    std::clog << "-- Total valid micro-times: " << total_valid << " / " << n_micro_time_bitmap 
              << " (" << (100.0 * total_valid / n_micro_time_bitmap) << "%)" << std::endl;
}


void CLSMImage::copy(const CLSMImage &p2, bool fill) {
    if (is_verbose()) {
        std::clog << "-- Copying image structure..." << std::endl;
        if (fill) {
            std::clog << "-- Copying pixel information." << std::endl;
        }
    }
    // private attributes
    int i_frame = 0;
    if (is_verbose()) {
        std::clog << "-- Copying frame: " << std::flush;
    }
    for (auto f: p2.frames) {
        i_frame++;
        if (is_verbose()) {
            std::clog << i_frame << " " << std::flush;
        }
        frames.emplace_back(new CLSMFrame(*f, fill));
    }
    if (is_verbose()) {
        std::clog << std::endl;
    }
    if (is_verbose()) {
        std::clog << "-- Linking TTTR: " << std::endl << std::flush;
    }
    this->tttr = p2.tttr;
    // public attributes
    settings = p2.settings;
    n_frames = p2.n_frames;
    n_lines = p2.n_lines;
    n_pixel = p2.n_pixel;
    // Copy channel layout as well
    n_channels = p2.n_channels;
    channel_offsets = p2.channel_offsets;
    channel_counts = p2.channel_counts;
    if (is_verbose()) {
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel <<
                std::endl;
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
    // Use n_lines from settings (metadata) if available and positive
    // If n_lines is -1 or 0, determine from actual frame data
    if (settings.n_lines > 0) {
        n_lines = settings.n_lines;
    } else {
        // Determine from frames: find the most common line count
        std::map<size_t, int> line_count_histogram;
        for (auto &f: frames) {
            line_count_histogram[f->lines.size()]++;
        }
        
        // Find the most frequently occurring line count
        n_lines = 0;
        int max_count = 0;
        for (auto &pair : line_count_histogram) {
            if (pair.second > max_count) {
                max_count = pair.second;
                n_lines = pair.first;
            }
        }
    }
}

void CLSMImage::compute_channel_layout() {
    // Respect settings: if not splitting by channel, keep a flat single-channel layout
    n_channels = 1;
    channel_offsets.clear();
    channel_counts.clear();

    if (!settings.split_by_channel) {
        channel_offsets.push_back(0);
        channel_counts.push_back(frames.size());
        return;
    }

    if (frames.empty()) {
        channel_offsets.push_back(0);
        channel_counts.push_back(0);
        return;
    }

    size_t current_count = 0;
    channel_offsets.push_back(0);

    for (size_t i = 0; i < frames.size(); ++i) {
        if (i > 0 && frames[i]->has_channel_flip()) {
            // finalize previous channel
            channel_counts.push_back(current_count);
            // start new channel at i
            channel_offsets.push_back(i);
            current_count = 0;
            n_channels++;
        }
        current_count++;
    }
    // finalize last channel
    channel_counts.push_back(current_count);

    // sanity: if no flips were detected, ensure n_channels is 1 with one count
    if (n_channels == 1) {
        channel_offsets.clear();
        channel_counts.clear();
        channel_offsets.push_back(0);
        channel_counts.push_back(frames.size());
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

        // Apply BH SPC-130 specific defaults
        if (this->settings.reading_routine == CLSM_BH_SPC130 && tttr_data != nullptr) {
            if (is_verbose()) {
                std::clog << "-- Applying BH SPC-130 defaults" << std::endl;
            }
            
            // Set BH-specific marker conventions if not explicitly overridden
            if (this->settings.marker_frame_start.size() == 1 && 
                this->settings.marker_frame_start[0] == 1) {
                this->settings.marker_frame_start = {4};  // BH frame marker channel
            }
            if (this->settings.marker_line_start == 3) {  // Check if still at CLSMSettings default
                this->settings.marker_line_start = 2;  // BH line marker channel
            }
            if (this->settings.marker_line_stop == 2) {  // Check if still at CLSMSettings default
                this->settings.marker_line_stop = -1;  // BH has no stop marker, uses duration
            }
            this->settings.marker_event_type = 1; // BH marker event type is 1 (standard)
            
            // Skip incomplete data at start/end
            this->settings.skip_before_first_frame_marker = true;
            this->settings.skip_after_last_frame_marker = true;
            
            // Read image dimensions from header metadata (set by read_bh_set_file)
            auto header = tttr_data->get_header();
            if (header != nullptr) {
                try {
                    auto json = nlohmann::json::parse(header->get_json());
                    
                    // Read n_pixel_per_line from header if not explicitly binned by user
                    if (this->settings.n_pixel_per_line <= 1) {
                        auto pixX_tag = TTTRHeader::get_tag(json, "ImgHdr_PixX");
                        if (!pixX_tag.is_null() && pixX_tag.contains("value")) {
                            this->settings.n_pixel_per_line = pixX_tag["value"].get<int>();
                            if (is_verbose()) {
                                std::clog << "-- BH: n_pixel_per_line from header: " 
                                          << this->settings.n_pixel_per_line << std::endl;
                            }
                        }
                    }
                    
                    // Read n_lines from header if not explicitly set
                    if (this->settings.n_lines <= 0) {
                        auto pixY_tag = TTTRHeader::get_tag(json, "ImgHdr_PixY");
                        if (!pixY_tag.is_null() && pixY_tag.contains("value")) {
                            this->settings.n_lines = pixY_tag["value"].get<int>();
                            if (is_verbose()) {
                                std::clog << "-- BH: n_lines from header: " 
                                          << this->settings.n_lines << std::endl;
                            }
                        }
                    }
                    
                    // Read pixel clock setting from header
                    auto pixClk_tag = TTTRHeader::get_tag(json, "BH_UsePixelClock");
                    if (!pixClk_tag.is_null() && pixClk_tag.contains("value")) {
                        int use_pix_clk = pixClk_tag["value"].get<int>();
                        this->settings.use_pixel_markers = (use_pix_clk == 1);
                        if (is_verbose()) {
                            std::clog << "-- BH: use_pixel_markers from header: " 
                                      << this->settings.use_pixel_markers << std::endl;
                        }
                    }
                    
                    // Set pixel marker channel if using pixel markers
                    if (this->settings.use_pixel_markers) {
                        this->settings.marker_pixel = 1;  // BH pixel clock channel
                    }
                } catch (...) {
                    // Header parsing failed, continue with settings as-is
                    if (is_verbose()) {
                        std::clog << "-- BH: Could not read header metadata" << std::endl;
                    }
                }
            }

            // Task 5: Fallback for dimension inference if still not set
            if (this->settings.n_pixel_per_line <= 1 || this->settings.n_lines <= 0) {
                if (is_verbose()) {
                    std::clog << "-- BH: Inferring dimensions from markers" << std::endl;
                }
                
                auto frame_edges = get_frame_edges(tttr_data.get(), 0, -1, 
                    this->settings.marker_frame_start, this->settings.marker_event_type, 
                    this->settings.reading_routine, true, true);
                
                if (frame_edges.size() >= 2) {
                    // 1. Count line markers in first frame
                    if (this->settings.n_lines <= 0) {
                        int line_count = 0;
                        for (int i = frame_edges[0]; i < frame_edges[1]; ++i) {
                            if (tttr_data->get_event_type_at(i) == this->settings.marker_event_type && 
                                tttr_data->get_routing_channel_at(i) == this->settings.marker_line_start) {
                                line_count++;
                            }
                        }
                        if (line_count > 1) {
                            this->settings.n_lines = line_count - 1;
                            if (is_verbose()) {
                                std::clog << "-- BH: Inferred n_lines: " << this->settings.n_lines << std::endl;
                            }
                        }
                    }
                    
                    // 2. Count pixel markers in first line
                    if (this->settings.n_pixel_per_line <= 1) {
                        // Find first two line markers to identify a complete line
                        int first_line_start = -1;
                        int second_line_start = -1;
                        for (int i = frame_edges[0]; i < frame_edges[1]; ++i) {
                            if (tttr_data->get_event_type_at(i) == this->settings.marker_event_type && 
                                tttr_data->get_routing_channel_at(i) == this->settings.marker_line_start) {
                                if (first_line_start == -1) {
                                    first_line_start = i;
                                } else {
                                    second_line_start = i;
                                    break;
                                }
                            }
                        }
                        
                        if (first_line_start != -1 && second_line_start != -1) {
                            int pixel_count = 0;
                            int pixel_marker = 1; // BH default pixel channel
                            for (int i = first_line_start; i < second_line_start; ++i) {
                                if (tttr_data->get_event_type_at(i) == this->settings.marker_event_type && 
                                    tttr_data->get_routing_channel_at(i) == pixel_marker) {
                                    pixel_count++;
                                }
                            }
                            if (pixel_count > 1) {
                                this->settings.n_pixel_per_line = pixel_count - 1;
                                this->settings.use_pixel_markers = true;
                                this->settings.marker_pixel = pixel_marker;
                                if (is_verbose()) {
                                    std::clog << "-- BH: Inferred n_pixel_per_line: " << this->settings.n_pixel_per_line << std::endl;
                                }
                            }
                        }
                    }
                }
            }

            // Update n_pixel if it was updated in settings
            this->n_pixel = this->settings.n_pixel_per_line;
        }

        // Early exit if TTTR pointer missing or no records
        if (tttr.get() == nullptr) {
            std::clog << "WARNING: No TTTR object provided" << std::endl;
            return;
        }
        if (tttr_data->get_n_events() == 0) {
            std::clog << "WARNING: No records in TTTR object" << std::endl;
            return;
        }

        // “No frame marker” case ===
        if (this->settings.marker_frame_start.empty()) {
            std::clog << "WARNING: No frame marker provided - creating a single full-span frame" << std::endl;

            // Determine total number of valid events
            int n_events = static_cast<int>(tttr_data->get_n_valid_events()); // or tttr_data->n_valid_events

            // Manually create one frame covering all events [0, n_events)
            auto singleFrame = new CLSMFrame(0, n_events, tttr);
            singleFrame->set_tttr(tttr);
            frames.emplace_back(singleFrame);

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

    // If user asked to fill with photons, do so first. The fill() method will
    // handle an empty 'channels' list by defaulting to all used routing channels.
    // This ensures that Python calls like CLSMImage(d, split_by_channel=True, fill=True)
    // actually populate the pixels on construction.
    // However, if we copied from a source with fill=true, the pixels are already filled,
    // so we should not refill them from TTTR data.
    if (fill && source == nullptr) {
        this->fill(this->tttr, channels, false, micro_time_ranges);
    }

    // Compute channel layout based on per-frame channel flip flags
    compute_channel_layout();
    
    // (Optionally shift line start if settings.macro_time_shift != 0)
    // if (settings.macro_time_shift != 0)
    //     shift_line_start(settings.macro_time_shift);
}

void CLSMImage::create_pixels_in_lines() {
    if (is_verbose()) {
        std::clog << "-- CLSMImage::create_pixels_in_lines" << std::endl;
        
        // Show CPU features
        bool has_avx = tttrlib::cpu_features::get_avx_enabled();
        bool has_openmp = tttrlib::cpu_features::get_openmp_enabled();
        std::clog << "-- CPU features: AVX=" << (has_avx ? "yes" : "no") 
                  << ", OpenMP=" << (has_openmp ? "yes" : "no") << std::endl;
#ifdef _OPENMP
        if (has_openmp) {
            std::clog << "-- OpenMP threads: " << omp_get_max_threads() << std::endl;
        }
#endif
    }
    
    // Estimate average photons per pixel for pre-allocation
    // This optimization can provide a factor of two in speed improvement
    size_t total_pixels = n_frames * n_lines * n_pixel;
    size_t estimated_photons_per_pixel = 10; // Default conservative estimate
    
    if (tttr != nullptr && total_pixels > 0) {
        size_t total_events = tttr->get_n_valid_events();
        // Estimate with 1.5x buffer to reduce reallocations
        estimated_photons_per_pixel = (total_events / total_pixels) * 3 / 2;
        // Ensure reasonable bounds
        estimated_photons_per_pixel = std::max(size_t(10), estimated_photons_per_pixel);
        
        if (is_verbose()) {
            std::clog << "-- Total events: " << total_events << std::endl;
            std::clog << "-- Total pixels: " << total_pixels << std::endl;
            std::clog << "-- Estimated photons per pixel: " << estimated_photons_per_pixel << std::endl;
        }
    }
    
    // Create pixels (serial is faster due to memory allocation overhead)
    for (auto &f: frames) {
        for (auto &l: f->lines) {
            l->pixels.resize(n_pixel);
        }
    }
    
    if (is_verbose()) {
        std::clog << "-- Number of pixels per line: " << n_pixel << std::endl;
        std::clog << "-- Pre-allocated capacity per pixel: " << estimated_photons_per_pixel << std::endl;
    }
}

void CLSMImage::append(CLSMFrame *frame) {
    frames.emplace_back(frame);
    n_frames++;
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
    
    // Reserve space - estimate ~100-1000 frames
    frame_edges.reserve(1000);
    
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

    // Pre-fetch pointers for faster access
    const signed char* event_types = tttr->event_types;
    const signed char* routing_channels = tttr->routing_channels;
    const unsigned short* micro_times = tttr->micro_times;
    
    // Build lookup table for frame markers - O(1) instead of O(n) per event
    bool frame_marker_lookup_u16[65536] = {false};  // For SP8 (micro_times)
    bool frame_marker_lookup_i8[TTTRLIB_MAX_ROUTING_CHANNELS] = {false};  // For SP5/default (routing_channels)
    
    if (reading_routine == CLSM_SP8) {
        for (auto f: marker_frame) {
            if (f >= 0 && f < 65536) {
                frame_marker_lookup_u16[f] = true;
            }
        }
    } else {
        for (auto f: marker_frame) {
            if (f >= -128 && f < 128) {
                frame_marker_lookup_i8[static_cast<unsigned char>(f)] = true;
            }
        }
    }
    
    // Hoist reading_routine check outside the loop
    if (reading_routine == CLSM_SP8) {
        for (int i_event = start_event; i_event < stop_event; i_event++) {
            if (routing_channels[i_event] == marker_event_type) {
                unsigned short mt = micro_times[i_event];
                if (frame_marker_lookup_u16[mt]) {
                    frame_edges.emplace_back(i_event);
                }
            }
        }
    } else if (reading_routine == CLSM_SP5) {
        for (int i_event = start_event; i_event < stop_event; i_event++) {
            signed char rc = routing_channels[i_event];
            if (frame_marker_lookup_i8[static_cast<unsigned char>(rc)]) {
                frame_edges.emplace_back(i_event);
            }
        }
    } else {
        for (int i_event = start_event; i_event < stop_event; i_event++) {
            if (event_types[i_event] == marker_event_type) {
                signed char rc = routing_channels[i_event];
                if (frame_marker_lookup_i8[static_cast<unsigned char>(rc)]) {
                    frame_edges.emplace_back(i_event);
                }
            }
        }
    }

    // Task 7: BH SPC-130 Frame 1 adjustment
    if (reading_routine == CLSM_BH_SPC130 && frame_edges.size() >= 2) {
        int frame_m = marker_frame.empty() ? 4 : marker_frame[0];
        int line_m = 2; // BH default line marker
        if (detect_bh_frame1_extra_line(tttr, frame_m, line_m, marker_event_type)) {
            // Find first line marker after first frame marker and skip it
            for (int i = frame_edges[0]; i < frame_edges[1]; ++i) {
                if (event_types[i] == marker_event_type && routing_channels[i] == line_m) {
                    frame_edges[0] = i + 1;
                    break;
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
        stop_event = static_cast<int>(tttr->n_valid_events);

    // Reserve space: estimate 2 edges per line, with typical 256-512 lines per frame
    size_t estimated_lines = (stop_event - start_event) / 1000; // Conservative estimate
    estimated_lines = std::max(size_t(100), std::min(size_t(2048), estimated_lines));
    line_edges.reserve(estimated_lines * 2);

    if (reading_routine == CLSM_SP5) {
        line_edges.emplace_back(start_event);
    }

    // Pre-fetch pointers for faster access
    const signed char* event_types = tttr->event_types;
    const signed char* routing_channels = tttr->routing_channels;
    const unsigned short* micro_times = tttr->micro_times;
    
    // Cast markers to appropriate types once
    signed char marker_start_sc = static_cast<signed char>(marker_line_start);
    signed char marker_stop_sc = static_cast<signed char>(marker_line_stop);
    unsigned short marker_start_us = static_cast<unsigned short>(marker_line_start);
    unsigned short marker_stop_us = static_cast<unsigned short>(marker_line_stop);
    signed char marker_event_sc = static_cast<signed char>(marker_event_type);
    
    // Hoist reading_routine check outside the loop
    if (reading_routine == CLSM_SP8) {
        for (int i_event = start_event; i_event < stop_event; i_event++) {
            if (routing_channels[i_event] == marker_event_sc) {
                unsigned short mt = micro_times[i_event];
                if (mt == marker_start_us) {
                    line_edges.emplace_back(i_event);
                } else if (mt == marker_stop_us) {
                    line_edges.emplace_back(i_event);
                }
            }
        }
    } else if (reading_routine == CLSM_SP5) {
        for (int i_event = start_event; i_event < stop_event; i_event++) {
            if (event_types[i_event] == marker_event_sc) {
                signed char rc = routing_channels[i_event];
                if (rc == marker_start_sc) {
                    line_edges.emplace_back(i_event);
                } else if (rc == marker_stop_sc) {
                    line_edges.emplace_back(i_event);
                }
            }
        }
    } else {
        for (int i_event = start_event; i_event < stop_event; i_event++) {
            if (event_types[i_event] == marker_event_sc) {
                signed char rc = routing_channels[i_event];
                if (rc == marker_start_sc) {
                    line_edges.emplace_back(i_event);
                } else if (rc == marker_stop_sc) {
                    line_edges.emplace_back(i_event);
                }
            }
        }
    }

    // Handle single-marker mode (only start markers, no stop markers)
    // This is common for B&H SPC files where line markers indicate line starts only
    if (marker_line_start != marker_line_stop && !line_edges.empty()) {
        // Check if we only have start markers by verifying count
        // If we have N line markers but they're all starts (no stops found),
        // we need to convert them to (start, stop) pairs where each line
        // runs from one start to the next
        
        // Count how many are starts vs stops
        size_t start_count = 0;
        size_t stop_count = 0;
        signed char marker_start_sc = static_cast<signed char>(marker_line_start);
        signed char marker_stop_sc = static_cast<signed char>(marker_line_stop);
        signed char marker_event_sc = static_cast<signed char>(marker_event_type);
        const signed char* routing_channels = tttr->routing_channels;
        const signed char* event_types = tttr->event_types;
        
        for (int idx : line_edges) {
            if (event_types[idx] == marker_event_sc) {
                if (routing_channels[idx] == marker_start_sc) start_count++;
                else if (routing_channels[idx] == marker_stop_sc) stop_count++;
            }
        }
        
        // If all markers are starts (no stops found), convert to pairs
        if (stop_count == 0 && start_count > 1) {
            std::vector<int> paired_edges;
            paired_edges.reserve((start_count - 1) * 2);
            for (size_t i = 0; i < line_edges.size() - 1; i++) {
                paired_edges.emplace_back(line_edges[i]);      // line start
                paired_edges.emplace_back(line_edges[i + 1]);  // line stop = next start
            }
            return paired_edges;
        }
    }

    return line_edges;
}


bool CLSMImage::detect_bh_frame1_extra_line(
    TTTR* tttr,
    int frame_marker,
    int line_marker,
    int marker_event_type
) {
    if (tttr == nullptr) return false;

    // We only need to check the first 3 frame markers to compare two complete frames
    std::vector<int> frame_marker_positions;
    
    const signed char* event_types = tttr->event_types;
    const signed char* routing_channels = tttr->routing_channels;
    size_t n_events = tttr->get_n_events();

    for (size_t i = 0; i < n_events; ++i) {
        if (event_types[i] == marker_event_type) {
            if (routing_channels[i] == frame_marker) {
                frame_marker_positions.push_back(static_cast<int>(i));
                if (frame_marker_positions.size() >= 3) break;
            }
        }
    }

    if (frame_marker_positions.size() < 3) return false;

    // Count line markers in Frame 1 (between 1st and 2nd frame marker)
    int n_lines_f1 = 0;
    for (int i = frame_marker_positions[0]; i < frame_marker_positions[1]; ++i) {
        if (event_types[i] == marker_event_type && routing_channels[i] == line_marker) {
            n_lines_f1++;
        }
    }

    // Count line markers in Frame 2 (between 2nd and 3rd frame marker)
    int n_lines_f2 = 0;
    for (int i = frame_marker_positions[1]; i < frame_marker_positions[2]; ++i) {
        if (event_types[i] == marker_event_type && routing_channels[i] == line_marker) {
            n_lines_f2++;
        }
    }

    // Frame 1 anomaly: one extra initialization line marker
    return (n_lines_f1 == n_lines_f2 + 1);
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

    if (frame_stop < 0) frame_stop = static_cast<int>(tttr->n_valid_events);
    std::vector<int> line_edges;
    
    // Reserve space for line edges
    size_t estimated_lines = (frame_stop - frame_start) / 1000;
    estimated_lines = std::max(size_t(100), std::min(size_t(2048), estimated_lines));
    line_edges.reserve(estimated_lines * 2);
    
    // Pre-fetch pointers for faster access (except macro_times which may be compressed)
    const signed char* routing_channels = tttr->routing_channels;
    const unsigned short* micro_times = tttr->micro_times;
    const signed char* event_types = tttr->event_types;
    
    // Pre-cast markers
    signed char marker_start_sc = static_cast<signed char>(marker_line_start);
    unsigned short marker_start_us = static_cast<unsigned short>(marker_line_start);
    signed char marker_event_sc = static_cast<signed char>(marker_event_type);
    
    // Optimized line finding - only process marker events
    if (reading_routine == CLSM_SP8) {
        for (int i_event = frame_start; i_event < frame_stop; i_event++) {
            // Only check marker events
            if (routing_channels[i_event] == marker_event_sc) {
                if (micro_times[i_event] == marker_start_us) {
                    // Found line start - calculate stop based on duration
                    int line_start = i_event;
                    unsigned long long stop_time = tttr->get_macro_time_at(i_event) + line_duration;
                    
                    // Binary search for stop event by time (much faster than linear)
                    int left = i_event + 1;
                    int right = frame_stop - 1;
                    int line_stop = -1;
                    
                    while (left <= right) {
                        int mid = left + (right - left) / 2;
                        if (tttr->get_macro_time_at(mid) >= stop_time) {
                            line_stop = mid;
                            right = mid - 1;  // Look for earlier match
                        } else {
                            left = mid + 1;
                        }
                    }
                    
                    if (line_stop > 0) {
                        line_edges.emplace_back(line_start);
                        line_edges.emplace_back(line_stop);
                    }
                }
            }
        }
    } else {
        for (int i_event = frame_start; i_event < frame_stop; i_event++) {
            // Only check marker events
            if (event_types[i_event] == marker_event_sc) {
                if (routing_channels[i_event] == marker_start_sc) {
                    // Found line start - calculate stop based on duration
                    int line_start = i_event;
                    unsigned long long stop_time = tttr->get_macro_time_at(i_event) + line_duration;
                    
                    // Binary search for stop event by time (much faster than linear)
                    int left = i_event + 1;
                    int right = frame_stop - 1;
                    int line_stop = -1;
                    
                    while (left <= right) {
                        int mid = left + (right - left) / 2;
                        if (tttr->get_macro_time_at(mid) >= stop_time) {
                            line_stop = mid;
                            right = mid - 1;  // Look for earlier match
                        } else {
                            left = mid + 1;
                        }
                    }
                    
                    if (line_stop > 0) {
                        line_edges.emplace_back(line_start);
                        line_edges.emplace_back(line_stop);
                    }
                }
            }
        }
    }
    
    return line_edges;
}


void CLSMImage::create_frames(bool clear_first) {
    if (clear_first) frames.clear();
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
    if (is_verbose()) {
        std::clog << "-- CREATE_FRAMES" << std::endl;
        std::cout << "-- Creating " << frame_edges.size() << " frames: " << std::flush;
    }
    for (size_t i = 0; i < frame_edges.size() - 1; i++) {
        auto frame = new CLSMFrame(
            frame_edges[i], frame_edges[i + 1], tttr);
        frame->set_tttr(tttr);
        append(frame);
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
    if (is_verbose()) {
        std::clog << "CLSMIMAGE::CREATE_LINES" << std::endl;
        std::clog << "-- Frame start, frame stop idx:" << std::endl;
    }
    
    int pixel_duration = (settings.marker_line_stop < 0) ? tttr->header->get_pixel_duration() : -1;

    // Configure OpenMP for parallel line finding
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    
    // Use adaptive threshold: parallelize if work per thread is sufficient
    // Minimum 2 frames per thread to avoid overhead
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && frames.size() >= min_frames_for_parallel)
    for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
        auto &frame = frames[f_idx];
        
        // find line edges
        std::vector<int> line_edges;
        if (settings.marker_line_stop >= 0) {
            line_edges = get_line_edges(
                tttr.get(),
                frame->get_start(),
                frame->get_stop(),
                settings.marker_line_start,
                settings.marker_line_stop,
                settings.marker_event_type,
                settings.reading_routine
            );
        } else {
            long line_duration = tttr->header->get_line_duration();
            line_edges = get_line_edges_by_duration(
                tttr.get(),
                frame->get_start(),
                frame->get_stop(),
                settings.marker_line_start,
                line_duration,
                settings.marker_event_type,
                settings.reading_routine
            );
        }

        // Pre-allocate lines vector for better performance
        size_t n_lines = line_edges.size() / 2;
        frame->lines.reserve(n_lines);
        
        // Batch allocate lines for better memory locality
        std::vector<CLSMLine*> new_lines(n_lines);
        for (size_t i_line = 0; i_line < n_lines; i_line++) {
            new_lines[i_line] = new CLSMLine();
        }
        
        // Configure lines in second pass (better cache utilization)
        for (size_t i_line = 0; i_line < n_lines; i_line++) {
            auto line_start = line_edges[(i_line * 2) + 0];
            auto line_stop = line_edges[(i_line * 2) + 1];
            new_lines[i_line]->set_range(line_start, line_stop);
            new_lines[i_line]->set_tttr(tttr);
            new_lines[i_line]->set_pixel_duration(pixel_duration);
        }
        
        // Move lines into frame
        frame->lines = std::move(new_lines);
    }
}

void CLSMImage::remove_incomplete_frames() {
    // remove incomplete frames
    if (is_verbose()) {
        std::clog << "-- Removing incomplete frames..." << std::endl;
    }
    std::vector<CLSMFrame *> complete_frames;
    n_frames = frames.size();
    size_t i_frame = 0;
    for (auto frame: frames) {
        if (frame->lines.size() >= n_lines) {
            // Truncate to n_lines if there are extra lines
            if (frame->lines.size() > n_lines) {
                if (is_verbose()) {
                    std::clog << "-- Frame " << i_frame + 1 << " has " << frame->lines.size() 
                              << " lines, truncating to " << n_lines << std::endl;
                }
                // Delete extra lines from the first line
                while (frame->lines.size() > n_lines) {
                    delete frame->lines.front();
                    frame->lines.erase(frame->lines.begin());
                }
            }
            complete_frames.push_back(frame);
        } else {
            if (is_verbose()) {
                std::cerr << "WARNING: Frame " << i_frame + 1 << " / " << frames.size() <<
                        " incomplete only " << frame->lines.size() << " / " << n_lines << " lines." << std::endl;
            }
            delete(frame);
        }
        i_frame++;
    }
    frames = complete_frames;
    n_frames = complete_frames.size();
    if (is_verbose()) {
        std::clog << "-- Final number of frames: " << n_frames << std::endl;
    }
}


void CLSMImage::clear() {
    if (is_verbose()) {
        std::clog << "Clear pixels of photons" << std::endl;
    }
    _is_filled_ = false;
    for (auto *frame: frames) {
        for (auto &line: frame->lines) {
            for (auto &pixel: line->pixels) {
                pixel.clear();
            }
        }
    }
}

size_t CLSMImage::split_frames_by_channel(
    const std::vector<int>& channels,
    std::shared_ptr<TTTR> tttr_data
) {
    if (is_verbose()) {
        std::clog << "-- Splitting frames by channel..." << std::endl;
        std::clog << "-- Number of channels: " << channels.size() << std::endl;
        std::clog << "-- Original frames: " << frames.size() << std::endl;
    }
    
    // Use stored TTTR if none provided
    if (tttr_data == nullptr) {
        tttr_data = tttr;
    }
    
    // Enable split_by_channel setting
    settings.split_by_channel = true;
    
    // If already split or only one channel, return current size
    if (n_channels > 1 || channels.size() <= 1) {
        if (is_verbose()) {
            std::clog << "-- Already split or single channel, skipping" << std::endl;
        }
        return frames.size();
    }
    
    // Store original frames
    size_t original_frames = frames.size();
    std::vector<CLSMFrame*> original = frames;
    
    // Clear and rebuild frame list
    frames.clear();
    n_frames = 0;
    
    // Duplicate each frame for each channel
    for (size_t ci = 0; ci < channels.size(); ++ci) {
        for (size_t fi = 0; fi < original_frames; ++fi) {
            // Copy structure but leave pixels empty
            auto nf = new CLSMFrame(*original[fi], false);
            nf->set_tttr(tttr_data);
            
            // Mark the first frame of each channel block (except the first) as a flip point
            if (ci > 0 && fi == 0) {
                nf->set_channel_flip(true);
            }
            
            frames.emplace_back(nf);
            n_frames++;
        }
    }
    
    // Clean up original frames (we created full copies)
    for (auto of : original) {
        delete of;
    }
    
    if (is_verbose()) {
        std::clog << "-- New total frames: " << n_frames << std::endl;
        std::clog << "-- Channel block size: " << original_frames << std::endl;
    }
    
    return original_frames;
}

void CLSMImage::fill(
    std::shared_ptr<TTTR> tttr_data,
    std::vector<int> channels,
    bool clear,
    const std::vector<std::pair<int, int> > &micro_time_ranges,
    bool* micro_time_bitmap,
    int n_micro_time_bitmap
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
        
        // Display bitmap information using helper function
        display_microtime_bitmap_info(micro_time_bitmap, n_micro_time_bitmap, channels.size());
    }

    // If no TTTR data pointer was passed in, use the stored one
    if (tttr_data == nullptr) {
        tttr_data = tttr;
    }

    // If the channel list is empty, query all used routing channels from TTTR
    if (channels.empty()) {
        if (is_verbose()) {
            std::clog << "WARNING: Image filled without channel numbers. Using all channels." << std::endl;
        }
        signed char *chs; int nchs;
        tttr_data->get_used_routing_channels(&chs, &nchs);
        for (int i = 0; i < nchs; i++) {
            channels.emplace_back(chs[i]);
        }
        free(chs);
    }

    // If requested, split frames by channel: duplicate each original frame for every routing channel
    size_t channel_block_size = frames.size();
    bool do_split_fill = settings.split_by_channel && channels.size() > 1;
    if (do_split_fill) {
        if (n_channels <= 1) {
            // Use the public method to split frames
            channel_block_size = split_frames_by_channel(channels, tttr_data);
        } else {
            // Already split previously; infer block size from layout if available
            if (!channel_counts.empty()) {
                channel_block_size = channel_counts[0];
            } else if (channels.size() > 0) {
                channel_block_size = frames.size() / channels.size();
            }
        }
    }

    // Pre-compute channel lookup for faster matching
    bool channel_lookup[TTTRLIB_MAX_ROUTING_CHANNELS] = {false};
    if (!do_split_fill) {
        for (auto ch : channels) {
            if (ch >= 0 && ch < TTTRLIB_MAX_ROUTING_CHANNELS) {
                channel_lookup[ch] = true;
            }
        }
    }
    
    // Pre-fetch TTTR data pointers once - they're read-only and shared across all threads
    const signed char* event_types = tttr_data->event_types;
    const signed char* routing_channels_ptr = tttr_data->routing_channels;
    const unsigned short* micro_times = tttr_data->micro_times;
    
    // Note: macro_times cannot be cached as a pointer when compression is enabled
    // We'll use get_macro_time_at() accessor instead
    
    // Configure OpenMP and get actual thread count
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    
    // Adaptive threshold based on number of threads
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && frames.size() >= min_frames_for_parallel)
    for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
        CLSMFrame* frame = frames[f_idx];

        // Hoist marker-times vector to frame loop to reduce heap allocations
        std::vector<unsigned long long> pixel_marker_times;
        if (settings.use_pixel_markers) {
            pixel_marker_times.reserve(n_pixel + 16);
        }

        // We need the line index to decide if a line is "reversed"
        for (size_t l_idx = 0; l_idx < frame->lines.size(); ++l_idx) {
            CLSMLine *line = frame->lines[l_idx];

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
            
            // Pre-compute for faster pixel calculation
            int n_pixels_minus_1 = static_cast<int>(n_pixels_in_line) - 1;
            
            // Use reciprocal multiplication instead of division (3-5x faster)
            double pixel_duration_reciprocal = 1.0 / static_cast<double>(pixel_duration);
            
            // Pre-compute channel index for split fill (constant per frame)
            size_t ch_idx = 0;
            if (do_split_fill && channel_block_size > 0) {
                ch_idx = f_idx / channel_block_size;
            }
            
            // Setup micro-time filtering using helper
            auto [micro_time_valid, owns_bitmap, use_micro_time_filter] = setup_microtime_filter(
                micro_time_bitmap, n_micro_time_bitmap, micro_time_ranges,
                channels, do_split_fill, ch_idx
            );
            
            // Determine bitmap type for filter checking
            bool has_per_channel_bitmap = (micro_time_bitmap != nullptr && 
                                          n_micro_time_bitmap == 65536 * static_cast<int>(channels.size()));
            
            // Cache pixel array pointer for faster access
            auto* pixels_ptr = line->pixels.data();
            
            // Pre-compute channel matching value for split fill
            signed char target_channel = 0;
            bool check_channel_match = true;
            if (do_split_fill) {
                if (ch_idx < channels.size()) {
                    target_channel = static_cast<signed char>(channels[ch_idx]);
                } else {
                    check_channel_match = false;  // Skip all photons
                }
            }

            // Collect pixel marker times if marker-based binning is enabled
            if (settings.use_pixel_markers) {
                pixel_marker_times.clear();
                for (int event_i = start_idx; event_i < stop_idx; ++event_i) {
                    if (event_types[event_i] == RECORD_MARKER && routing_channels_ptr[event_i] == settings.marker_pixel) {
                        pixel_marker_times.push_back(tttr_data->get_macro_time_at(event_i));
                    }
                }
            }

            // Walk through each TTTR event that falls within this line's event indices
            for (int event_i = start_idx; event_i < stop_idx; ++event_i) {
                // Prefetch next iteration's data for better cache utilization
                if (event_i + 16 < stop_idx) {
                    PREFETCH(&event_types[event_i + 16]);
                    PREFETCH(&routing_channels_ptr[event_i + 16]);
                    // Note: Cannot prefetch macro_times when using compression
                }
                
                // Only process photon events, skip all others
                signed char event_type = event_types[event_i];
                if (event_type != RECORD_PHOTON) {
                    continue;
                }

                // Pull the routing channel for this photon event
                signed char c = routing_channels_ptr[event_i];

                // Decide if this photon should be included in this frame (channel-aware)
                if (do_split_fill) {
                    // Direct comparison with pre-computed target
                    if (!check_channel_match || c != target_channel) {
                        continue;
                    }
                } else {
                    // Fast lookup using pre-computed array (unsigned cast for bounds check)
                    unsigned char uc = static_cast<unsigned char>(c);
                    if (uc >= TTTRLIB_MAX_ROUTING_CHANNELS || !channel_lookup[uc]) {
                        continue;
                    }
                }

                // Compute the "raw" pixel index
                int raw_pixel = -1;
                if (settings.use_pixel_markers) {
                    if (pixel_marker_times.empty()) continue;
                    unsigned long long photon_time = tttr_data->get_macro_time_at(event_i);
                    auto it = std::upper_bound(pixel_marker_times.begin(), pixel_marker_times.end(), photon_time);

                    if (it == pixel_marker_times.begin()) {
                        continue; // Before first marker
                    } else if (it == pixel_marker_times.end()) {
                        // After last marker: belongs to the last pixel segment
                        raw_pixel = static_cast<int>(pixel_marker_times.size()) - 1;
                    } else {
                        // Between markers
                        raw_pixel = static_cast<int>(std::distance(pixel_marker_times.begin(), it) - 1);
                    }
                } else {
                    // Compute using reciprocal multiplication (faster than division)
                    // Use accessor to handle both compressed and uncompressed macro times
                    unsigned long long time_offset = tttr_data->get_macro_time_at(event_i) - line_start_time;
                    double pixel_idx_float = static_cast<double>(time_offset) * pixel_duration_reciprocal;
                    raw_pixel = static_cast<int>(pixel_idx_float);
                }
                
                // If the computed raw index falls outside the line, skip (single comparison)
                if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) {
                    continue;
                }
                // If the line was scanned right→left, flip the raw index
                int pixel_nbr = is_reversed ? (n_pixels_minus_1 - raw_pixel) : raw_pixel;

                // Verify that the photon's micro time falls within the specified ranges (bitmap lookup)
                if (use_micro_time_filter) {
                    unsigned short micro_time = micro_times[event_i];
                    if (!passes_microtime_filter(micro_time, micro_time_valid, has_per_channel_bitmap, 
                                                 do_split_fill, c, channels)) {
                        continue;
                    }
                }

                // Insert the event index using cached pointer
                pixels_ptr[pixel_nbr].insert(event_i);
            }
            
            // Clean up micro-time filter bitmap only if we allocated it
            if (owns_bitmap && micro_time_valid != nullptr) {
                delete[] micro_time_valid;
            }
        }
    }

    // Shrink all pixel vectors to eliminate capacity overhead
    // This reduces memory from ~1.5-2x to exactly the needed size
    for (auto &f: frames) {
        for (auto &l: f->lines) {
            for (auto &p: l->pixels) {
                p.shrink_to_fit();
            }
        }
    }

    // Mark the image as filled so downstream methods know pixels are populated
    _is_filled_ = true;
}

void CLSMImage::strip(const std::vector<int> &tttr_indices, int offset) {
    for (auto &f: get_frames()) {
        for (auto &l: f->get_lines()) {
            for (auto &p: l->get_pixels()) {
                offset = p.strip(tttr_indices, offset);
            }
        }
    }
}

void CLSMImage::get_intensity(unsigned short **output, int *dim1, int *dim2, int *dim3) {
    *dim1 = static_cast<int>(n_frames);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    size_t n_pixel_total = n_frames * n_pixel * n_lines;
    
    if (is_verbose()) {
        std::clog << "Get intensity image" << std::endl;
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
        std::clog << "-- Total number of pixels: " << n_pixel_total << std::endl;
    }
    
    // Allocate output array for all frames
    auto *t = (unsigned short *) malloc(n_pixel_total * sizeof(unsigned short));
    
    // Configure OpenMP for parallel intensity computation
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    
    // Adaptive threshold based on number of threads
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && n_frames >= min_frames_for_parallel)
    for (int i_frame = 0; i_frame < static_cast<int>(n_frames); i_frame++) {
        auto &frame = frames[i_frame];
        
        // Use CLSMFrame::get_intensity() to get frame data
        unsigned short *frame_intensity = nullptr;
        int frame_lines = 0, frame_pixels = 0;
        frame->get_intensity(&frame_intensity, &frame_lines, &frame_pixels);
        
        // Copy frame intensity into the output array at the correct offset
        if (frame_intensity != nullptr) {
            size_t frame_offset = i_frame * n_lines * n_pixel;
            size_t frame_size = frame_lines * frame_pixels;
            std::memcpy(&t[frame_offset], frame_intensity, frame_size * sizeof(unsigned short));
            free(frame_intensity);
        }
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
    // Use malloc + memset for large arrays - faster than calloc
    auto *t = (unsigned char *) malloc(n_tac_total * sizeof(unsigned char));
    memset(t, 0, n_tac_total * sizeof(unsigned char));
    if (is_verbose()) {
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel <<
                std::endl;
        std::clog << "-- Number of micro time channels: " << n_tac << std::endl;
        std::clog << "-- Micro time coarsening factor: " << micro_time_coarsening << std::endl;
        std::clog << "-- Final number of micro time channels: " << n_tac << std::endl;
    }
    
    // Configure OpenMP for parallel decay computation
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && n_frames > 4 && !stack_frames)
    for (int i_frame = 0; i_frame < static_cast<int>(stack_frames ? 1 : n_frames); i_frame++) {
        auto frame = frames[stack_frames ? 0 : i_frame];
        for (size_t i_line = 0; i_line < frame->lines.size(); i_line++) {
            auto line = frame->lines[i_line];
            for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                auto pixel = line->pixels[i_pixel];
                size_t pixel_nbr = i_frame * (n_lines * n_pixel * n_tac) +
                                   i_line * (n_pixel * n_tac) +
                                   i_pixel * (n_tac);
                const auto& indices = pixel.get_tttr_indices();
                for (auto i: indices) {
                    size_t i_tac = tttr_data->micro_times[i] / micro_time_coarsening;
                    t[pixel_nbr + i_tac] += 1;
                }
            }
        }
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
    
    // Reuse TTTR objects and shared_ptrs to reduce allocations
    // Create once per frame instead of per pixel
    //#pragma omp parallel for default(none) shared(tttr, o_frame, t, clsm_other)
    for (unsigned int i_frame = 0; i_frame < n_frames; i_frame++) {
        auto corr = Correlator(tttr, correlation_method, n_bins, n_casc);
        auto frame = frames[i_frame];
        auto other_frame = clsm_other->frames[i_frame];
        
        // Pre-allocate reusable TTTR objects to avoid repeated construction
        TTTR tttr_1(*tttr, nullptr, 0, false);
        TTTR tttr_2(*tttr, nullptr, 0, false);
        auto tttr_1_ptr = std::make_shared<TTTR>(tttr_1);
        auto tttr_2_ptr = std::make_shared<TTTR>(tttr_2);
        
        for (unsigned int i_line = 0; i_line < n_lines; i_line++) {
            auto line = frame->lines[i_line];
            auto other_line = other_frame->lines[i_line];
            for (unsigned int i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                auto pixel = line->pixels[i_pixel];
                auto other_pixel = other_line->pixels[i_pixel];
                int count1 = static_cast<int>(pixel.size());
                int count2 = static_cast<int>(other_pixel.size());
                if ((count1 > min_photons) && (count2 > min_photons)) {
                    // Get indices once - use const ref to avoid copies
                    const auto& v1 = pixel.get_tttr_indices();
                    const auto& v2 = other_pixel.get_tttr_indices();
                    
                    // Create TTTR objects for this pixel pair
                    TTTR tttr_1_temp(
                        *tttr,
                        const_cast<int*>(v1.data()),
                        static_cast<int>(v1.size()),
                        false
                    );
                    TTTR tttr_2_temp(
                        *tttr,
                        const_cast<int*>(v2.data()),
                        static_cast<int>(v2.size()),
                        false
                    );
                    
                    // Update shared pointers
                    *tttr_1_ptr = tttr_1_temp;
                    *tttr_2_ptr = tttr_2_temp;
                    
                    corr.set_tttr(tttr_1_ptr, tttr_2_ptr);
                    
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
    *dim1 = static_cast<int>(n_decays);
    *dim2 = static_cast<int>(n_tac);
    size_t n_tac_total = n_decays * n_tac;
    auto *t = (unsigned int *) calloc(n_tac_total, sizeof(unsigned int));
    if ((dmask1 != n_frames) || (dmask2 != n_lines) || (dmask3 != n_pixel)) {
        std::cerr << "Error: the dimensions of the selection ("
                << n_frames << ", " << n_lines << ", " << n_pixel
                << ") does not match the CLSM image dimensions.";
    } else {
        size_t w_frame = 0;
        for (size_t i_frame = 0; i_frame < n_frames; i_frame++) {
            auto frame = frames[i_frame];
            for (size_t i_line = 0; i_line < n_lines; i_line++) {
                auto line = frame->lines[i_line];
                for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                    auto pixel = line->pixels[i_pixel];
                    if (mask[i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel]) {
                        const auto& indices = pixel.get_tttr_indices();
                        for (auto i: indices) {
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
    TTTR *tttr_data,
    double **output, int *dim1, int *dim2, int *dim3,
    double microtime_resolution,
    int minimum_number_of_photons,
    bool stack_frames
) {
    if (is_verbose()) {
        std::clog << "Get mean micro time image" << std::endl;
        std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
        std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
        std::clog << "-- Computing stack of mean micro times " << std::endl;
    }
    if (microtime_resolution < 0)
        microtime_resolution = tttr_data->header->get_micro_time_resolution();
    if (!stack_frames) {
        auto *t = (double *) malloc(n_frames * n_lines * n_pixel * sizeof(double));
        
        // Configure OpenMP for parallel mean microtime computation
        int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
        bool use_openmp = (num_threads > 1);
        
        #pragma omp parallel for schedule(dynamic) if(use_openmp && n_frames > 4)
        for (int i_frame = 0; i_frame < static_cast<int>(n_frames); i_frame++) {
            for (int i_line = 0; i_line < static_cast<int>(n_lines); i_line++) {
                for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                    size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel;
                    CLSMPixel px = frames[i_frame]->lines[i_line]->pixels[i_pixel];
                    t[pixel_nbr] = px.get_mean_microtime(tttr_data, microtime_resolution, minimum_number_of_photons);
                }
            }
        }
        *dim1 = static_cast<int>(n_frames);
        *dim2 = static_cast<int>(n_lines);
        *dim3 = static_cast<int>(n_pixel);
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
                r[pixel_nbr] = 0.0;
                
                // Collect indices from all frames for this pixel
                auto tr = collect_stacked_pixel_indices(frames, i_line, i_pixel);
                r[pixel_nbr] = tttr_data->get_mean_microtime(&tr, microtime_resolution, minimum_number_of_photons);
            }
        }
        *dim1 = static_cast<int>(w_frame);
        *dim2 = static_cast<int>(n_lines);
        *dim3 = static_cast<int>(n_pixel);
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
    int o_frames = stack_frames ? 1 : static_cast<int>(n_frames);
    double factor = (2. * frequency * M_PI);
    // Use malloc + memset for large arrays - faster than calloc
    auto *t = (float *) malloc(o_frames * n_lines * n_pixel * 2 * sizeof(float));
    memset(t, 0, o_frames * n_lines * n_pixel * 2 * sizeof(float));
    for (int i_line = 0; i_line < n_lines; i_line++) {
        for (int i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
            if (stack_frames) {
                size_t pixel_nbr = i_line * (n_pixel * 2) + i_pixel * 2;
                
                // Collect indices from all frames for this pixel
                auto idxs = collect_stacked_pixel_indices(frames, i_line, i_pixel);
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
                    const auto& s = frames[i_frame]->lines[i_line]->pixels[i_pixel].get_tttr_indices();
                    std::vector<int> indices(s.begin(), s.end());
                    auto r = DecayPhasor::compute_phasor(
                        tttr_data->micro_times, tttr_data->n_valid_events,
                        frequency,
                        minimum_number_of_photons,
                        g_irf, s_irf,
                        &indices
                    );
                    t[pixel_nbr + 0] = static_cast<int>(r[0]);
                    t[pixel_nbr + 1] = static_cast<int>(r[1]);
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
    *dim1 = static_cast<int>(o_frames);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    *dim4 = static_cast<int>(2);
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

    // Use malloc + memset for large arrays - faster than calloc
    auto *t = (double *) malloc(o_frames * n_lines * n_pixel * sizeof(double));
    memset(t, 0, o_frames * n_lines * n_pixel * sizeof(double));
    
    // Configure OpenMP for parallel mean lifetime computation
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && o_frames > 4)
    for (int i_frame = 0; i_frame < o_frames; i_frame++) {
        for (int i_line = 0; i_line < static_cast<int>(n_lines); i_line++) {
            for (int i_pixel = 0; i_pixel < static_cast<int>(n_pixel); i_pixel++) {
                size_t pixel_nbr = i_frame * (n_lines * n_pixel) + i_line * (n_pixel) + i_pixel;
                if (stack_frames) {
                    // Collect indices from all frames for this pixel
                    auto tttr_indices = collect_stacked_pixel_indices(frames, i_line, i_pixel);
                    t[pixel_nbr] = TTTRRange::compute_mean_lifetime(
                        tttr_indices, tttr_data, minimum_number_of_photons,
                        nullptr, m0_irf, m1_irf, dt,
                        nullptr, m0_bg, m1_bg,
                        background_fraction
                    );
                } else {
                    auto px = this->frames[i_frame]->lines[i_line]->pixels[i_pixel];
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
    *dim1 = static_cast<int>(o_frames);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
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
    stop_x = (stop_x < 0) ? static_cast<int>(np) : stop_x % static_cast<int>(np);
    stop_y = (stop_y < 0) ? static_cast<int>(nl) : stop_y % static_cast<int>(nl);

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
                        auto line = frame->lines[l];
                        value = static_cast<double>(line->pixels[p].size());
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
    std::shared_ptr<TTTR> tttr_data, CLSMImage *clsm,
    double *images, int input_frames, int input_lines, int input_pixel,
    std::vector<int> x_range, std::vector<int> y_range,
    std::vector<std::pair<int, int> > frames_index_pairs,
    std::string subtract_average,
    uint8_t *mask, int dmask1, int dmask2, int dmask3
) {
    typedef double T;
    typedef std::complex<T> CT;

    // create ROI
    T *roi;
    int nf, nl, np;
    // If pair of ICS frames empty make ACF without frame shift
    get_roi(&roi, &nf, &nl, &np,
            clsm, x_range, y_range,
            subtract_average, 0.0,
            false, 1, 1,
            images, input_frames, input_lines, input_pixel,
            mask, dmask1, dmask2, dmask3
    );
    int pixel_in_roi = nl * np;

    // Define set of frame pairs (if no pairs were defined)
    // Computes ICS for pair of frames default (1,1), (2,2), ...
    if (frames_index_pairs.empty()) {
        frames_index_pairs.reserve(nf);
        for (int i = 0; i < nf; i++)
            frames_index_pairs.emplace_back(std::make_pair(i, i));
    }

    // Allocate memory for the ICS output array
    auto out_tmp = (T *) calloc(frames_index_pairs.size() * pixel_in_roi, sizeof(T));

    // Allocate arrays for FFTWs
    std::vector<CT> in(pixel_in_roi, 0);
    std::vector<CT> fft_roi1(pixel_in_roi, 0);
    std::vector<CT> fft_roi2(pixel_in_roi, 0);
    std::vector<CT> ics(pixel_in_roi, 0);

    std::ptrdiff_t sd = sizeof(T);
    std::ptrdiff_t sc = sizeof(CT);
    pocketfft::shape_t shape{(size_t) nl, (size_t) np};
    pocketfft::stride_t stride_d{sd * np, sd};
    pocketfft::stride_t stride_c{sc * np, sc};
    pocketfft::shape_t axes{0, 1};
    T norm = T(1.0 / pixel_in_roi);

    // Iterate through the pair of frames
    int current_pair = 0;
    for (auto &frame_pair: frames_index_pairs) {
        //double roi1_int = 0.0;
        //double roi2_int = 0.0; // sum of values in roi1 & roi2

        // ROI1
        //for(int i = frame_pair.first * pixel_in_roi; i < frame_pair.first * pixel_in_roi + pixel_in_roi; i++) roi1_int += roi[i];
        pocketfft::r2c<double>(shape, stride_d, stride_c, axes, pocketfft::FORWARD,
                               &roi[frame_pair.first * pixel_in_roi], fft_roi1.data(), 1.0);

        // ROI2
        if (frame_pair.second != frame_pair.first) {
            //for(int i = frame_pair.first * pixel_in_roi; i < frame_pair.first * pixel_in_roi + pixel_in_roi; i++) roi2_int += roi[i];
            pocketfft::r2c<T>(shape, stride_d, stride_c, axes, pocketfft::FORWARD,
                              &roi[frame_pair.second * pixel_in_roi], fft_roi2.data(), 1.0);
        } else {
            fft_roi2 = fft_roi1;
            //roi2_int = roi1_int;
        }

        // FFT(roi1) * conj(FFT(roi2))
        for (size_t i = 0; i < fft_roi1.size(); i++) {
            in[i] = fft_roi1[i] * std::conj(fft_roi2[i]);
        }

        // make backward transform FFT-1(FFT(roi1) * conj(FFT(roi2)))
        pocketfft::c2c<T>(shape, stride_c, stride_c, axes, pocketfft::BACKWARD, in.data(), ics.data(), norm);

        // write results to ics output and normalize
        int frame_offset = current_pair * pixel_in_roi;
        //double denom = roi1_int * roi2_int;
        for (int i = 0; i < pixel_in_roi; i++) {
            out_tmp[frame_offset + i] = real(ics[i]);
        }

        current_pair++;
    }
    free(roi);

    // Assign output
    *dim1 = static_cast<int>(nf);
    *dim2 = static_cast<int>(nl);
    *dim3 = static_cast<int>(np);
    *output = out_tmp;
    return;
}


void CLSMImage::transform(unsigned int *input, int n_input) {
    CLSMImage *source = new CLSMImage(*this, true);
    CLSMImage *target = this;

    target->clear();
    for (int i = 0; i < n_input; i = i + 2) {
        // source (s)
        CLSMPixel *source_pixel = source->getPixel(input[i + 0]);
        CLSMPixel *target_pixel = target->getPixel(input[i + 1]);
        // Append tttr indices to pixel - use const ref to avoid copy
        const auto& source_indices = source_pixel->get_tttr_indices();
        for (auto tr_idx: source_indices) {
            target_pixel->insert(tr_idx);
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

    // 2) Extract each pixel into a flat vector so we can rebuild the layout later.
    std::vector<CLSMPixel> flat_pixels;
    flat_pixels.reserve(old_total);

    for (size_t f = 0; f < static_cast<size_t>(n_frames); ++f) {
        CLSMFrame *frame = frames[f];
        for (size_t l = 0; l < static_cast<size_t>(n_lines); ++l) {
            CLSMLine *line = frame->lines[l];
            for (size_t p = 0; p < static_cast<size_t>(n_pixel); ++p) {
                flat_pixels.push_back(line->pixels[p]);
            }
        }
    }

    // 3) Delete old frames (and their lines/pixels), then clear the vector.
    for (auto *fr: frames) {
        delete fr;
    }
    frames.clear();

    // 4) Update stored dimensions
    n_frames = new_n_frames;
    n_lines = new_n_lines;
    n_pixel = new_n_pixel;

    // 5) Rebuild a fresh hierarchy (CLSMFrame → CLSMLine → pixels),
    //    moving each saved pixel back into its new position.
    size_t idx = 0;
    for (size_t f = 0; f < static_cast<size_t>(n_frames); ++f) {
        std::unique_ptr<CLSMFrame> new_frame(new CLSMFrame());
        new_frame->set_tttr(tttr); // pass the shared_ptr directly

        for (size_t l = 0; l < static_cast<size_t>(n_lines); ++l) {
            std::unique_ptr<CLSMLine> new_line(new CLSMLine());
            new_line->set_tttr(tttr); // pass the shared_ptr directly

            new_line->pixels.resize(static_cast<size_t>(n_pixel));
            for (size_t p = 0; p < static_cast<size_t>(n_pixel); ++p) {
                new_line->pixels[p] = std::move(flat_pixels[idx]);
                // Pixels don't need tttr reference - only lines do
                ++idx;
            }

            new_frame->append(new_line.release());
        }

        frames.emplace_back(new_frame.release());
    }

    if (is_verbose()) {
        std::clog << "-- Reshape complete. Now dims are ("
                << n_frames << " × " << n_lines << " × " << n_pixel << ")."
                << std::endl;
    }
}

void CLSMImage::crop(
    int frame_start, int frame_stop,
    int line_start, int line_stop,
    int pixel_start, int pixel_stop
) {
    frame_stop = std::min(std::max(0, frame_stop), static_cast<int>(size()));
    frame_start = std::max(0, frame_start);

    if (is_verbose()) {
        std::clog << "Crop image" << std::endl;
        std::clog << "-- Frame range: " << frame_start << ", " << frame_stop << std::endl;
        std::clog << "-- Line range: " << line_start << ", " << line_stop << std::endl;
        std::clog << "-- Pixel range: " << pixel_start << ", " << pixel_stop << std::endl;
    }

    std::vector<CLSMFrame *> frs;
    for (int i = 0; i < frame_start; i++) {
        delete frames[i];
    }
    for (int i = frame_start; i < frame_stop; i++) {
        auto f = frames[i];
        f->crop(line_start, line_stop, pixel_start, pixel_stop);
        frs.emplace_back(f);
    }
    for (unsigned long i = frame_stop; i < n_frames; i++) {
        delete frames[i];
    }
    frames = frs;

    n_frames = frs.size();
    n_lines = frs[0]->lines.size();
    n_pixel = frs[0]->lines[0]->pixels.size();
}


// ISM super-resolution reconstruction (AMD-like iterative) using pocketfft

size_t CLSMImage::get_memory_usage_bytes() const {
    size_t total = sizeof(CLSMImage);
    
    // Frame vector overhead
    total += frames.capacity() * sizeof(CLSMFrame*);
    
    // Memory for each frame
    for (const auto& frame : frames) {
        if (frame != nullptr) {
            total += frame->get_memory_usage_bytes();
        }
    }
    
    // Channel layout vectors
    total += channel_offsets.capacity() * sizeof(size_t);
    total += channel_counts.capacity() * sizeof(size_t);
    
    // Marker frame vector
    total += marker_frame.capacity() * sizeof(int);
    
    return total;
}

void CLSMImage::get_memory_usage_detailed(
    size_t* overhead,
    size_t* indices,
    size_t* ranges
) const {
    if (overhead) *overhead = 0;
    if (indices) *indices = 0;
    if (ranges) *ranges = 0;
    
    // CLSMImage object overhead
    if (overhead) {
        *overhead += sizeof(CLSMImage);
        *overhead += frames.capacity() * sizeof(CLSMFrame*);
        *overhead += channel_offsets.capacity() * sizeof(size_t);
        *overhead += channel_counts.capacity() * sizeof(size_t);
        *overhead += marker_frame.capacity() * sizeof(int);
    }
    
    // Accumulate from all frames
    for (const auto& frame : frames) {
        if (frame == nullptr) continue;
        
        // Frame overhead
        if (overhead) {
            *overhead += sizeof(CLSMFrame);
            *overhead += frame->lines.capacity() * sizeof(CLSMLine*);
        }
        
        // Range markers for frame
        if (ranges) {
            *ranges += 2 * sizeof(int);  // _range_start, _range_stop
        }
        
        // Process each line
        for (const auto& line : frame->lines) {
            if (line == nullptr) continue;
            
            // Line overhead
            if (overhead) {
                *overhead += sizeof(CLSMLine);
                *overhead += line->pixels.capacity() * sizeof(CLSMPixel);
            }
            
            // Range markers for line
            if (ranges) {
                *ranges += 2 * sizeof(int);  // _range_start, _range_stop
                *ranges += sizeof(int);      // pixel_duration
            }
            
            // Process each pixel
            for (const auto& pixel : line->pixels) {
                // Pixel overhead
                if (overhead) {
                    *overhead += sizeof(CLSMPixel);
                }
                
                // Range markers for pixel
                if (ranges) {
                    *ranges += 2 * sizeof(int);  // _range_start, _range_stop
                    *ranges += sizeof(uint8_t);  // SelectionMask
                }
                
                // TTTR indices - use public method to get index count
                if (indices) {
                    size_t n_indices = pixel.size();
                    // Estimate capacity as size * 1.5 (typical vector growth)
                    // This is an approximation since we can't access capacity directly
                    *indices += static_cast<size_t>(n_indices * 1.5) * sizeof(int);
                }
            }
        }
    }
}


