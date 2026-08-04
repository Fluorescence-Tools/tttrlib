// SPDX-License-Identifier: BSD-3-Clause
#include "include/CLSMImage.h"

#include <nlohmann/json.hpp>
#include "include/BitOps.h"
#include "include/Verbose.h"
#include "include/info.h"
#include <memory>
#include <tuple>
#include <cstring>  // for memset
#include <stdexcept>
#include <iostream>
#include <map>

/* FFT is an implementation detail of this file only -- keep the 71k-line
 * vendored header out of the installed CLSMImage.h. */
#include "pocketfft/pocketfft_hdronly.h"

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
        if (i_line >= lines.size()) continue;
        const auto& pixels = lines[i_line]->get_pixels();
        if (i_pixel >= pixels.size()) continue;
        total_size += pixels[i_pixel].size();
    }
    
    // Reserve space and collect indices
    std::vector<int> indices;
    indices.reserve(total_size);
    
    for (const auto& frame : frames) {
        const auto& lines = frame->get_lines();
        if (i_line >= lines.size()) continue;
        const auto& pixels = lines[i_line]->get_pixels();
        if (i_pixel >= pixels.size()) continue;
        const auto& dense_indices = pixels[i_pixel].get_tttr_indices();
        indices.insert(indices.end(), dense_indices.begin(), dense_indices.end());
    }
    
    return indices;
}

// Sequential macro time reader. Replicates TTTR::get_macro_time_at exactly
// but replaces the per-photon keyframe division (index / keyframe_interval)
// of the compressed representation with an incremental keyframe pointer.
// Requires monotonically increasing indices between reset() calls.
// The raw pointers are extracted inside CLSMImage member functions
// (CLSMImage is a friend of TTTR).
namespace {
struct SeqMacroTime {
    const unsigned long long* raw;        // non-null when compression is off
    const uint32_t* comp;
    const unsigned long long* kfs;
    size_t interval;
    size_t kf_idx = 0;
    unsigned long long kf = 0;
    size_t kf_end = 0;

    SeqMacroTime(const unsigned long long* raw_,
                 const uint32_t* comp_,
                 const unsigned long long* kfs_,
                 size_t interval_)
        : raw(raw_), comp(comp_), kfs(kfs_),
          interval(interval_ > 0 ? interval_ : 1) {}

    inline void reset(size_t start) {
        if (raw == nullptr) {
            kf_idx = start / interval;
            kf = kfs[kf_idx];
            kf_end = (kf_idx + 1) * interval;
        }
    }

    inline unsigned long long at(size_t i) {
        if (raw != nullptr) return raw[i];
        while (i >= kf_end) {
            ++kf_idx;
            kf = kfs[kf_idx];
            kf_end += interval;
        }
        return kf + static_cast<unsigned long long>(comp[i]);
    }
};
} // namespace

// ========================================================================
// CLSMImageInfo: container-agnostic imaging metadata from the TTTR header
// ========================================================================

namespace {

// True when TTTRHeader::get_tag reported "tag not found".
//
// get_tag does not signal absence with null -- it returns a sentinel
//     {"value": -1.0, "idx": -1, "name": "NONE"}
// so a plain "has a numeric value" test succeeds for tags that are not in the
// file and hands back -1. That silently turned every missing flag into a set
// one: a file without an ImgHdr_BiDirect tag produced
// bidirectional_scan = (-1 != 0) = true, mirroring every other scan line.
// (Python was unaffected only because its CLSMImage wrapper resolves the
// settings itself and skips this auto-configuration path.)
bool tag_is_absent(const nlohmann::json& t) {
    if (t.is_null() || !t.contains("value") || t["value"].is_null()) return true;
    return t.contains("name") && t["name"].is_string()
           && t["name"].get<std::string>() == "NONE";
}

// Read a scalar header tag value as a double. Returns false when the tag is
// absent or carries a non-numeric value.
bool read_tag_double(const nlohmann::json& json, const std::string& name, double& out) {
    auto t = TTTRHeader::get_tag(json, name);
    if (tag_is_absent(t)) return false;
    const auto& v = t["value"];
    if (v.is_number()) { out = v.get<double>(); return true; }
    if (v.is_boolean()) { out = v.get<bool>() ? 1.0 : 0.0; return true; }
    return false;
}

// Read a scalar header tag value as a string. Returns false when absent.
bool read_tag_string(const nlohmann::json& json, const std::string& name, std::string& out) {
    auto t = TTTRHeader::get_tag(json, name);
    if (tag_is_absent(t)) return false;
    const auto& v = t["value"];
    if (!v.is_string()) return false;
    out = v.get<std::string>();
    return true;
}

// Read a scalar header tag value as an int. Returns false when absent.
bool read_tag_int(const nlohmann::json& json, const std::string& name, int& out) {
    auto t = TTTRHeader::get_tag(json, name);
    if (tag_is_absent(t)) return false;
    const auto& v = t["value"];
    if (v.is_number()) { out = v.get<int>(); return true; }
    if (v.is_boolean()) { out = v.get<bool>() ? 1 : 0; return true; }
    return false;
}

} // namespace

CLSMImageInfo CLSMImageInfo::from_header(TTTRHeader* header) {
    CLSMImageInfo info;
    if (header == nullptr) return info;

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(header->get_json());
    } catch (...) {
        return info;
    }

    info.container_type = header->get_tttr_container_type();

    // --- geometry ---------------------------------------------------------
    if (!read_tag_int(json, "ImgHdr_PixX", info.n_pixel)) info.n_pixel = 0;
    if (!read_tag_int(json, "ImgHdr_PixY", info.n_lines)) info.n_lines = 0;
    read_tag_int(json, "ImgHdr_MaxFrames", info.n_frames);
    read_tag_int(json, "ImgHdr_Dimensions", info.dimensions);
    read_tag_int(json, "ImgHdr_Ident", info.ident);

    // --- markers (container-specific encoding) ----------------------------
    // PTU stores marker *indices* that decode to routing channels via 2^(idx-1)
    // (or 2^idx when ImgHdr_LineStart == 0); HT3 stores the routing channel
    // directly. The normalized routing-channel values are what the record
    // stream actually uses, and what CLSMSettings expects.
    int ls_raw = 0, le_raw = 0, frame_raw = 0;
    bool has_ls = read_tag_int(json, "ImgHdr_LineStart", ls_raw);
    bool has_le = read_tag_int(json, "ImgHdr_LineStop", le_raw);
    read_tag_int(json, "ImgHdr_Frame", frame_raw);
    info.marker_line_start_raw = ls_raw;
    info.marker_line_stop_raw  = le_raw;
    info.has_line_markers      = has_ls && has_le;

    if (info.container_type == PQ_PTU_CONTAINER) {
        if (has_ls && has_le) {
            if (ls_raw == 0) {
                // 2^index encoding
                info.marker_line_start = 1 << ls_raw;
                info.marker_line_stop  = 1 << le_raw;
                info.marker_frame_start = { frame_raw >= 0 ? (1 << frame_raw) : 4 };
            } else {
                info.marker_line_start = 1 << (ls_raw - 1);
                info.marker_line_stop  = 1 << (le_raw - 1);
                if (frame_raw > 0)
                    info.marker_frame_start = { 1 << (frame_raw - 1) };
            }
        }
    } else {
        // HT3 (and any other container storing raw routing channels)
        if (has_ls) info.marker_line_start = ls_raw;
        if (has_le) info.marker_line_stop  = le_raw;
        if (frame_raw > 0)
            info.marker_frame_start = { frame_raw };
        else if (has_ls)
            info.marker_frame_start = { 4 };
    }
    info.marker_event_type = 1;

    // Becker & Hickl SPC: the record file cannot carry the scan geometry, so it
    // arrives from the .set sidecar (SP_IMG_X/SP_IMG_Y/SP_PIX_CLK, normalized to
    // ImgHdr_PixX/ImgHdr_PixY/BH_UsePixelClock by TTTRHeader::read_bh_set_file),
    // and the marker layout is a property of the instrument rather than the file.
    // A BH measurement records which reading routine produced it; honour that
    // hint and apply the matching marker convention. Without this the markers
    // stay at the CLSMSettings zero defaults, nothing ever matches, and the image
    // reconstructs to zero frames. This lived only in the Python wrapper, which
    // is why BH images reconstructed from Python but not from Java, R or C++.
    std::string routine;
    if (read_tag_string(json, "BH_SPC_ReadingRoutine", routine)) {
        if (routine == "BH_SPC130") info.reading_routine = CLSM_BH_SPC130;
        else if (routine == "SP5")  info.reading_routine = CLSM_SP5;
        else if (routine == "SP8")  info.reading_routine = CLSM_SP8;
    }
    if (info.reading_routine == CLSM_BH_SPC130) {
        info.marker_event_type  = 1;
        info.marker_frame_start = { 4 };
        info.marker_line_start  = 2;
        info.marker_line_stop   = 255;
        info.skip_before_first_frame_marker = true;
    }

    int pix_clk = 0;
    if (read_tag_int(json, "BH_UsePixelClock", pix_clk))
        info.use_pixel_markers = (pix_clk != 0);

    // --- timing -----------------------------------------------------------
    read_tag_double(json, "ImgHdr_TimePerPixel", info.time_per_pixel_s);
    read_tag_double(json, "ImgHdr_LineFrequency", info.line_frequency_hz);
    read_tag_double(json, TTTRTagGlobRes, info.macro_time_resolution_s);
    read_tag_double(json, TTTRTagRes, info.micro_time_resolution_s);

    // --- physical calibration ----------------------------------------------
    read_tag_double(json, "ImgHdr_PixResol", info.pixel_resolution_um);
    read_tag_double(json, "ImgHdr_X0", info.x0);
    read_tag_double(json, "ImgHdr_Y0", info.y0);
    read_tag_double(json, "ImgHdr_Z0", info.z0);
    read_tag_double(json, "ImgHdr_Acceleration", info.acceleration);
    read_tag_int(json, "ImgHdr_ScanDirection", info.scan_direction);

    int bd = 0;
    if (read_tag_int(json, "ImgHdr_BiDirect", bd))
        info.bidirectional_scan = (bd != 0);

    return info;
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
    // Pixel contents are only copied from a materialized source; a lazy
    // source hands over its stream masks instead (photons stay recomputable)
    const bool copy_pixels = fill && p2.pixels_materialized_;
    for (auto f: p2.frames) {
        i_frame++;
        if (is_verbose()) {
            std::clog << i_frame << " " << std::flush;
        }
        frames.emplace_back(new CLSMFrame(*f, copy_pixels));
    }
    if (is_verbose()) {
        std::clog << std::endl;
    }
    if (is_verbose()) {
        std::clog << "-- Linking TTTR: " << std::endl << std::flush;
    }
    this->tttr = p2.tttr;
    settings = p2.settings;
    image_info_ = p2.image_info_;
    n_frames = p2.n_frames;
    n_lines = p2.n_lines;
    n_pixel = p2.n_pixel;
    pixel_duration_matrix = p2.pixel_duration_matrix;
    pixel_duration_cumsum = p2.pixel_duration_cumsum;

    // Lazy fill state
    pixel_access_observed_ = false;
    if (fill) {
        _is_filled_ = p2._is_filled_;
        if (p2.pixels_materialized_) {
            drop_stream_masks();
            pixels_materialized_ = true;
        } else {
            stream_masks_ = p2.stream_masks_;
            mask_block_size_ = p2.mask_block_size_;
            mask_split_ = p2.mask_split_;
            mask_tttr_ = p2.mask_tttr_;
            pixels_materialized_ = false;
        }
    } else {
        // Structure-only copy: empty pixels, no masks (legacy contract)
        drop_stream_masks();
        pixels_materialized_ = true;
        _is_filled_ = false;
    }
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

            // Read image dimensions from header metadata (set by read_bh_set_file)
            auto header = tttr_data->get_header();
            if (header != nullptr) {
                try {
                    auto json = nlohmann::json::parse(header->get_json());

                    // Read n_pixel_per_line from header if not explicitly binned by user
                    if (this->settings.n_pixel_per_line == 0) {
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
            if (this->settings.n_pixel_per_line == 0 || this->settings.n_lines <= 0) {
                if (is_verbose()) {
                    std::clog << "-- BH: Inferring dimensions from markers" << std::endl;
                }

                auto frame_edges = get_frame_edges(tttr_data.get(), 0, -1,
                    this->settings.marker_frame_start, this->settings.marker_event_type,
                    this->settings.reading_routine, true, true,
                    this->settings.marker_line_start, this->settings.n_lines);

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
                    if (this->settings.n_pixel_per_line == 0) {
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

        // Auto-configure CLSM markers and dimensions from PicoQuant PTU/HT3 header
        // tags via the container-agnostic CLSMImageInfo parser. Runs only for the
        // default reading routine and only when the caller has not already supplied
        // pixel dimensions (n_pixel_per_line == 0), so callers that pass explicit
        // settings -- including the Python __init__, which resolves these itself --
        // are unaffected. The full parsed metadata (geometry, timing, calibration)
        // is always stored in image_info_ for introspection.
        {
            auto pq_header = tttr_data->get_header();
            if (pq_header != nullptr) {
                try {
                    this->image_info_ = CLSMImageInfo::from_header(pq_header);
                    // Geometry is a property of the acquisition, not of the
                    // reconstruction routine: an SP5/SP8 caller still needs
                    // ImgHdr_PixX/PixY. Only the marker layout is routine-specific,
                    // so the two are gated separately. (Applying both under
                    // reading_routine == CLSM_DEFAULT left explicit-routine callers
                    // with no dimensions at all.)
                    if (this->settings.n_pixel_per_line == 0 && this->image_info_.is_valid()) {
                        this->settings.n_pixel_per_line   = this->image_info_.n_pixel;
                        this->settings.n_lines            = this->image_info_.n_lines;
                        this->settings.bidirectional_scan = this->image_info_.bidirectional_scan;
                        this->n_pixel = this->settings.n_pixel_per_line;

                        if (this->settings.reading_routine == CLSM_DEFAULT) {
                            this->settings.marker_line_start  = this->image_info_.marker_line_start;
                            this->settings.marker_line_stop   = this->image_info_.marker_line_stop;
                            this->settings.marker_frame_start = this->image_info_.marker_frame_start;
                            this->settings.marker_event_type  = this->image_info_.marker_event_type;
                            // BH pixel-clock binning and the mid-frame start, so the
                            // sidecar-derived configuration is honoured identically
                            // in every language binding.
                            if (this->image_info_.use_pixel_markers) {
                                this->settings.use_pixel_markers = true;
                            }
                            if (this->image_info_.skip_before_first_frame_marker) {
                                this->settings.skip_before_first_frame_marker = true;
                            }
                            // A header-recorded reading routine (BH SPC) selects the
                            // instrument-specific reconstruction path, not just the
                            // marker numbers.
                            if (this->image_info_.reading_routine != CLSM_DEFAULT) {
                                this->settings.reading_routine = this->image_info_.reading_routine;
                            }
                        }
                    }
                    // Instrument marker conventions for the non-default reading
                    // routines. These lived only in the Python wrapper, so a
                    // Leica SP5/SP8 or BH SPC measurement reconstructed correctly
                    // from Python and not from R, Java or native C++. As in the
                    // Python original, a named routine overrides caller-supplied
                    // markers -- the routine *is* the marker convention.
                    switch (this->settings.reading_routine) {
                        case CLSM_SP5:
                            this->settings.marker_event_type  = 1;
                            this->settings.marker_frame_start = { 4, 6 };
                            this->settings.marker_line_start  = 1;
                            this->settings.marker_line_stop   = 2;
                            break;
                        case CLSM_SP8:
                            this->settings.marker_event_type  = 15;
                            this->settings.marker_frame_start = { 4, 6 };
                            // SP8 uses the RAW ImgHdr_LineStart/Stop values, not
                            // the 2^index decoding the default PTU routine applies.
                            if (this->image_info_.has_line_markers) {
                                this->settings.marker_line_start = this->image_info_.marker_line_start_raw;
                                this->settings.marker_line_stop  = this->image_info_.marker_line_stop_raw;
                            } else {
                                this->settings.marker_line_start = 1;
                                this->settings.marker_line_stop  = 2;
                            }
                            break;
                        case CLSM_BH_SPC130:
                            this->settings.marker_event_type  = 1;
                            this->settings.marker_frame_start = { 4 };
                            this->settings.marker_line_start  = 2;
                            this->settings.marker_line_stop   = 255;
                            this->settings.skip_before_first_frame_marker = true;
                            this->settings.skip_after_last_frame_marker   = false;
                            break;
                        default:
                            break;
                    }
                } catch (...) {
                    if (is_verbose())
                        std::clog << "-- CLSM: could not read PQ header settings" << std::endl;
                }
            }
        }

        // “No frame marker” case ===
        if (this->settings.marker_frame_start.empty()) {
            if (is_verbose()) {
                std::clog << "-- CLSM: no frame marker; creating one full-span frame"
                          << std::endl;
            }

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
            if (settings.build_pixels) create_pixels_in_lines();
        } else {
            // “frame marker provided” path ===
            create_frames(true);

            // PRD-004: a frame marker is configured but the stream contains none
            // (a single-frame FLIM acquisition, e.g. some PicoHarp/SymPhoTime PTU
            // files). Fall back to one full-span frame so the line markers still
            // reconstruct an image instead of an empty 0-frame stack.
            if (n_frames == 0) {
                std::clog << "WARNING: frame marker configured but none found in "
                             "the stream - reconstructing a single full-span frame"
                          << std::endl;
                int n_events = static_cast<int>(tttr_data->get_n_valid_events());
                auto singleFrame = new CLSMFrame(0, n_events, tttr);
                singleFrame->set_tttr(tttr);
                frames.emplace_back(singleFrame);
                n_frames = 1;
            }

            create_lines();

            if (is_verbose()) {
                std::clog << "-- Initial number of frames: " << n_frames << std::endl;
                std::clog << "-- Lines per frame: " << n_lines << std::endl;
            }
            determine_number_of_lines();
            remove_incomplete_frames();
            if (settings.build_pixels) create_pixels_in_lines();
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
    _pixels_ready_ = true;

    if (is_verbose()) {
        std::clog << "-- Number of pixels per line: " << n_pixel << std::endl;
        std::clog << "-- Pre-allocated capacity per pixel: " << estimated_photons_per_pixel << std::endl;
    }
}

void CLSMImage::ensure_pixels_built() {
    if (_pixels_ready_) return;
    create_pixels_in_lines();
}

void CLSMImage::append(CLSMFrame *frame) {
    // External pixel state enters the image: bring our own pixels up to date
    // and invalidate the stream masks (they do not cover the new frame)
    materialize_pixel_handles();
    drop_stream_masks();
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
    bool skip_after_last_frame_marker,
    int marker_line_start,
    int expected_n_lines) {
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
    // Heap-allocate the 64KB lookup table to avoid stack overflow on Windows
    // (especially with OpenMP threads that may have smaller stacks)
    auto frame_marker_lookup_u16 = std::make_unique<bool[]>(65536);  // For SP8 (micro_times)
    std::memset(frame_marker_lookup_u16.get(), 0, 65536 * sizeof(bool));
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
        int line_m = (marker_line_start > 0) ? marker_line_start : 2;  // Use configured value, fallback to BH default
        if (detect_bh_frame1_extra_line(tttr, frame_m, line_m, marker_event_type, expected_n_lines)) {
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
        // N start markers define N-1 lines (each line runs from marker[i] to marker[i+1])
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


/**
 * @brief Count line markers in a range of events
 * @param event_types Array of event types
 * @param routing_channels Array of routing channels
 * @param start_idx Start index (inclusive)
 * @param end_idx End index (exclusive)
 * @param marker_event_type Event type for markers
 * @param line_marker Routing channel for line markers
 * @return Number of line markers found
 */
static int count_line_markers_in_range(
    const signed char* event_types,
    const signed char* routing_channels,
    int start_idx,
    int end_idx,
    int marker_event_type,
    int line_marker
) {
    int count = 0;
    signed char event_sc = static_cast<signed char>(marker_event_type);
    signed char line_sc = static_cast<signed char>(line_marker);
    
    for (int i = start_idx; i < end_idx; ++i) {
        if (event_types[i] == event_sc && routing_channels[i] == line_sc) {
            count++;
        }
    }
    return count;
}


bool CLSMImage::detect_bh_frame1_extra_line(
    TTTR* tttr,
    int frame_marker,
    int line_marker,
    int marker_event_type,
    int expected_n_lines
) {
    if (tttr == nullptr) return false;

    // We only need to check the first 4 frame markers to compare three complete frames
    // (F1, F2, F3) to establish a baseline.
    std::vector<int> frame_marker_positions;

    const signed char* event_types = tttr->event_types;
    const signed char* routing_channels = tttr->routing_channels;
    size_t n_events = tttr->get_n_events();

    for (size_t i = 0; i < n_events; ++i) {
        if (event_types[i] == marker_event_type) {
            if (routing_channels[i] == frame_marker) {
                frame_marker_positions.push_back(static_cast<int>(i));
                if (frame_marker_positions.size() >= 4) break;
            }
        }
    }

    // PRIMARY CONDITION: Compare F1 vs F2 vs F3
    if (frame_marker_positions.size() >= 4) {
        int n_lines_f1 = count_line_markers_in_range(event_types, routing_channels, 
            frame_marker_positions[0], frame_marker_positions[1], marker_event_type, line_marker);
        int n_lines_f2 = count_line_markers_in_range(event_types, routing_channels, 
            frame_marker_positions[1], frame_marker_positions[2], marker_event_type, line_marker);
        int n_lines_f3 = count_line_markers_in_range(event_types, routing_channels, 
            frame_marker_positions[2], frame_marker_positions[3], marker_event_type, line_marker);

        // If F2 and F3 have same number of lines, they are the baseline.
        // If F1 has exactly one more, it has the BH extra initialization marker.
        if (n_lines_f2 == n_lines_f3 && n_lines_f1 == n_lines_f2 + 1) {
            return true;
        }
    }

    // FALLBACK CONDITION: Compare F1 to expected_n_lines
    // (Used when we don't have enough frames for 3-way comparison, but know expected count)
    if (frame_marker_positions.size() >= 2 && expected_n_lines > 0) {
        int n_lines_f1 = count_line_markers_in_range(event_types, routing_channels, 
            frame_marker_positions[0], frame_marker_positions[1], marker_event_type, line_marker);
        
        // BH uses N+1 markers for N lines. If we see N+2, it's the extra marker.
        if (n_lines_f1 == expected_n_lines + 2) {
            return true;
        }
    }

    return false;
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
        settings.skip_after_last_frame_marker,
        settings.marker_line_start,
        settings.n_lines
    );
    if (is_verbose()) {
        std::clog << "-- CREATE_FRAMES" << std::endl;
        std::cout << "-- Creating " << frame_edges.size() << " frames: " << std::flush;
    }
    if (frame_edges.size() <= 1) {
        if (is_verbose()) {
            std::clog << "-- Not enough frame edges to create frames: " << frame_edges.size() << std::endl;
        }
        return;
    }
    for (size_t i = 0; i < frame_edges.size() - 1; i++) {
        auto frame = new CLSMFrame(
            frame_edges[i], frame_edges[i + 1], tttr);
        frame->set_tttr(tttr);
        // Internal structural append: bypass the public append() so image
        // construction does not trip the lazy-fill access tracking
        frames.emplace_back(frame);
        n_frames++;
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

    // NOTE: create_lines() runs serially (no OpenMP) on purpose.
    // Concurrent heap allocation of CLSMLine objects via new CLSMLine() inside
    // an OpenMP parallel for causes heap corruption (0xC0000374) with MSVC's
    // OpenMP 2.0 runtime on Windows. The per-frame line-finding work is not a
    // bottleneck; the expensive parallelism lives in fill() and get_intensity().
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

        // BH SPC-130 specific truncated recording recovery:
        // For the last frame, if exactly one line is missing, add an extra line
        // pairing the last line marker with the frame end.
        // This recovers the final line when the recording was truncated.
        size_t current_line_count = line_edges.size() / 2;
        bool is_bh_spc130 = (settings.reading_routine == CLSM_BH_SPC130);
        bool is_last_frame = (f_idx == static_cast<int>(frames.size()) - 1);
        bool is_start_only_markers = (settings.marker_line_stop == CLSM_MARKER_NO_STOP ||
                                      settings.marker_line_stop == settings.marker_line_start);
        size_t expected_lines = (settings.n_lines > 0) ? static_cast<size_t>(settings.n_lines) : 0;
        bool missing_exactly_one = (expected_lines > 0 && current_line_count + 1 == expected_lines);
        
        if (is_bh_spc130 && is_last_frame && is_start_only_markers && 
            missing_exactly_one && !line_edges.empty()) {
            // Add extra line: from last line stop to frame end. The frame
            // edge can be one past the last event (end-of-stream); the line
            // stop must be a valid event index, otherwise the line duration
            // is computed from an out-of-bounds macro time read (undefined,
            // heap-layout dependent binning).
            int last_line_stop = line_edges.back();  // Current last edge
            int frame_end = std::min(frame->get_stop(),
                                     static_cast<int>(tttr->size()) - 1);
            line_edges.push_back(last_line_stop);  // New line start
            line_edges.push_back(frame_end);       // New line stop
            
            if (is_verbose()) {
                #ifdef _OPENMP
                #pragma omp critical
                #endif
                {
                    std::clog << "-- BH SPC-130: Recovered truncated last line in frame " 
                              << f_idx << " (events " << last_line_stop << "-" << frame_end << ")" 
                              << std::endl;
                }
            }
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
    std::vector<CLSMFrame *> complete_frames, incomplete_frames;
    n_frames = frames.size();
    size_t i_frame = 0;
    for (auto frame: frames) {
        if (frame->lines.size() == n_lines) {
            complete_frames.push_back(frame);
        } else {
            if (is_verbose()) {
                std::cerr << "WARNING: Frame " << i_frame + 1 << " / " << frames.size() <<
                        " incomplete only " << frame->lines.size() << " / " << n_lines << " lines." << std::endl;
            }
            incomplete_frames.push_back(frame);
        }
        i_frame++;
    }

    // PRD-004: if NOTHING is complete (e.g. a single-frame FLIM acquisition whose
    // only frame has fewer line markers than the header-declared n_lines), don't
    // throw the whole image away. Salvage the frame(s) with the most lines and
    // adopt that count as n_lines, reconstructing a (possibly partial) image
    // instead of an empty 0-frame stack. Normal multi-frame stacks are unaffected
    // because complete_frames is non-empty there.
    if (complete_frames.empty() && !incomplete_frames.empty()) {
        size_t best = 0;
        for (auto f: incomplete_frames) best = std::max(best, f->lines.size());
        if (best > 0) {
            for (auto f: incomplete_frames) {
                if (f->lines.size() == best) complete_frames.push_back(f);
                else delete f;
            }
            n_lines = best;
            std::clog << "WARNING: no complete frames; salvaging "
                      << complete_frames.size() << " frame(s) with " << best
                      << " line(s) as n_lines" << std::endl;
        } else {
            for (auto f: incomplete_frames) delete f;
        }
    } else {
        for (auto f: incomplete_frames) delete f;
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
    drop_stream_masks();
    invalidate_derived_caches();
    pixels_materialized_ = true;  // empty pixels reflect the (empty) fill state
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

    // The rebuilt frames start with empty pixels; pending masks from an
    // earlier fill no longer apply to the new layout
    drop_stream_masks();
    pixels_materialized_ = true;

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

    // fill() writes into per-pixel containers; materialize them if construction
    // deferred the allocation (build_pixels=false).
    ensure_pixels_built();
    // New photon selection -> the cached lifetime moments no longer apply.
    invalidate_derived_caches();

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
    // NOTE: OpenMP is disabled on Windows in CLSMImage to avoid heap corruption
    // (0xC0000374) with MSVC's OpenMP 2.0 runtime. Linux/macOS use GCC/Clang
    // OpenMP which does not exhibit this issue.
#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
#else
    bool use_openmp = false;
    size_t min_frames_for_parallel = SIZE_MAX;
#endif
    (void) use_openmp;

    // ------------------------------------------------------------------
    // Precomputed per-event acceptance bitmask
    //
    // One pass over the event stream bakes photon-type, channel and
    // micro-time acceptance into packed 64-bit words. The per-line photon
    // loops then visit only accepted events, skipping 64 rejected events per
    // zero word. In split-by-channel mode one mask is built per channel in
    // the same single pass, replacing n_channels rescans of the raw arrays.
    // ------------------------------------------------------------------
    const size_t n_events = static_cast<size_t>(tttr_data->size());

    // Shared micro-time table when ranges are given without a bitmap
    // (inclusive bounds, matching setup_microtime_filter). Built once and
    // read-only in the parallel region, replacing the previous per-thread
    // per-frame allocations.
    std::unique_ptr<bool[]> shared_mt_table;
    if (!micro_time_ranges.empty() && micro_time_bitmap == nullptr) {
        shared_mt_table.reset(new bool[65536]);
        std::memset(shared_mt_table.get(), 0, 65536);
        for (const auto& r: micro_time_ranges) {
            int start = std::max(0, r.first);
            int end = std::min(65535, r.second);
            if (end >= start) {
                std::memset(&shared_mt_table[start], 1, end - start + 1);
            }
        }
    }

    const bool mask_has_per_channel_bitmap = (micro_time_bitmap != nullptr &&
            n_micro_time_bitmap == 65536 * static_cast<int>(channels.size()));
    const bool mask_has_global_bitmap = (micro_time_bitmap != nullptr &&
            n_micro_time_bitmap == 65536);
    const bool mt_filter_any = (shared_mt_table != nullptr) ||
            mask_has_per_channel_bitmap || mask_has_global_bitmap;

    // The acceptance mask is the primary representation of a filled image:
    // fill() normally stops after building it and the per-pixel index vectors
    // are materialized lazily on first access. Only degenerate cases (no
    // events, no frames, oversized per-channel masks) run the legacy eager
    // loop below.
    constexpr size_t ACCEPT_MASK_MIN_EVENTS = size_t(1) << 20;
    const size_t n_masks = do_split_fill ? channels.size() : 1;
    bool use_accept_mask = n_events > 0 && !frames.empty();
    // With an absurd number of channels fall back to the legacy loop instead
    // of allocating oversized masks.
    if (use_accept_mask && n_masks * ((n_events + 7) / 8) > (size_t(256) << 20)) {
        use_accept_mask = false;
    }

    std::vector<std::vector<uint64_t>> accept_masks;
    if (use_accept_mask) {
        // For every routing channel value: which mask(s) receive its photons
        // (several in split mode when a channel is listed twice) and its first
        // position in 'channels' (per-channel bitmap slice, first match wins
        // exactly like passes_microtime_filter).
        std::vector<std::vector<int>> mask_targets(TTTRLIB_MAX_ROUTING_CHANNELS);
        int first_pos[TTTRLIB_MAX_ROUTING_CHANNELS];
        std::fill(std::begin(first_pos), std::end(first_pos), -1);
        for (size_t i = 0; i < channels.size(); i++) {
            int ch = channels[i];
            if (ch < 0 || ch >= TTTRLIB_MAX_ROUTING_CHANNELS) continue;
            if (first_pos[ch] < 0) first_pos[ch] = static_cast<int>(i);
            if (do_split_fill) {
                mask_targets[ch].push_back(static_cast<int>(i));
            } else if (mask_targets[ch].empty()) {
                mask_targets[ch].push_back(0);
            }
        }

        const size_t n_words = (n_events + 63) >> 6;
        accept_masks.assign(n_masks, std::vector<uint64_t>(n_words, 0));

        // Word-per-thread build: each thread owns whole 64-bit words of every
        // mask, so the read-modify-writes are race-free.
        // 'int' induction variable: MSVC's OpenMP 2.0 is strict about the
        // loop index type (n_words fits: 2^31 words = 137G events)
        #pragma omp parallel for schedule(static) if(use_openmp && n_events >= ACCEPT_MASK_MIN_EVENTS)
        for (int wi = 0; wi < static_cast<int>(n_words); ++wi) {
            const size_t base = static_cast<size_t>(wi) << 6;
            const int lim = static_cast<int>(std::min<size_t>(64, n_events - base));
            for (int b = 0; b < lim; ++b) {
                const size_t i = base + b;
                if (event_types[i] != RECORD_PHOTON) continue;
                const unsigned char uc = static_cast<unsigned char>(routing_channels_ptr[i]);
                const auto& targets = mask_targets[uc];
                if (targets.empty()) continue;
                for (int mi : targets) {
                    if (mt_filter_any) {
                        const bool* table;
                        if (mask_has_per_channel_bitmap) {
                            table = micro_time_bitmap + static_cast<size_t>(
                                    do_split_fill ? mi : first_pos[uc]) * 65536;
                        } else if (mask_has_global_bitmap) {
                            table = micro_time_bitmap;
                        } else {
                            table = shared_mt_table.get();
                        }
                        if (!table[micro_times[i]]) continue;
                    }
                    accept_masks[mi][static_cast<size_t>(wi)] |= 1ull << b;
                }
            }
        }
    }

    if (use_accept_mask) {
        // Keep the fill-time timing of the "line without pixel" warning
        // (materialization only skips such lines silently)
        for (auto* f: frames) {
            for (auto* l: f->lines) {
                if (l->pixels.empty()) {
                    std::clog << "WARNING: Line without pixel." << std::endl;
                }
            }
        }

        // The constructor calls fill(..., clear=false) on a fresh image, so
        // the lazy/eager decision cannot key on 'clear' alone: a fill over an
        // image that never held photons (and whose pixels were never handed
        // out) is also "fresh".
        const bool fresh = clear || (!_is_filled_ && !pixel_access_observed_);
        if (fresh) {
            if (clear) {
                // Drop content of a previous (possibly materialized) fill
                for (auto* f: frames) {
                    for (auto* l: f->lines) {
                        for (auto &p: l->pixels) p.clear();
                    }
                }
            }
            stream_masks_ = std::move(accept_masks);
            mask_block_size_ = channel_block_size;
            mask_split_ = do_split_fill;
            mask_tttr_ = tttr_data;
            pixels_materialized_ = false;
        } else {
            // clear==false on an image that already holds photons: legacy
            // accumulate semantics (duplicates preserved — OR-merging masks
            // would collapse them), so materialize and insert on top.
            ensure_pixels_materialized();
            consume_masks_into_pixels(accept_masks, channel_block_size,
                                      do_split_fill, tttr_data);
            for (auto &f: frames) {
                for (auto &l: f->lines) {
                    for (auto &p: l->pixels) p.shrink_to_fit();
                }
            }
            drop_stream_masks();
            pixels_materialized_ = true;
        }
        _is_filled_ = true;
        // Protect frame/line/pixel handles the caller may still hold from an
        // earlier access: refills stay eager once a handle escaped.
        if (pixel_access_observed_) ensure_pixels_materialized();
        return;
    }

    // ------------------------------------------------------------------
    // Legacy eager population (mask cap exceeded or degenerate input)
    // ------------------------------------------------------------------
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

            // Precomputed cumulative pixel durations for this line (nullptr
            // when uniform durations are used); fetched once per line so the
            // event loop below stays allocation-free
            const std::vector<double>* cumsum =
                    has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;

            // Guard against division by zero: if pixel_duration is 0,
            // no valid pixel binning is possible for this line
            // (with non-uniform durations the cumulative sums are used instead)
            if (pixel_duration == 0 && cumsum == nullptr) {
                continue;
            }

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
            
            // Micro-time filtering for the legacy event loop
            bool* micro_time_valid = nullptr;
            bool owns_bitmap = false;
            bool use_micro_time_filter = false;
            if (shared_mt_table != nullptr) {
                // Shared read-only table built once before the parallel region
                micro_time_valid = shared_mt_table.get();
                use_micro_time_filter = true;
            } else {
                std::tie(micro_time_valid, owns_bitmap, use_micro_time_filter) = setup_microtime_filter(
                    micro_time_bitmap, n_micro_time_bitmap, micro_time_ranges,
                    channels, do_split_fill, ch_idx
                );
            }
            
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

            // Assign one accepted photon to its pixel. Type/channel/micro-time
            // acceptance has already been checked by the per-event checks below.
            auto assign_photon = [&](int event_i) {
                // Compute the "raw" pixel index
                int raw_pixel = -1;
                if (settings.use_pixel_markers) {
                    if (pixel_marker_times.empty()) return;
                    unsigned long long photon_time = tttr_data->get_macro_time_at(event_i);
                    auto it = std::upper_bound(pixel_marker_times.begin(), pixel_marker_times.end(), photon_time);

                    if (it == pixel_marker_times.begin()) {
                        return; // Before first marker
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

                    if (cumsum != nullptr) {
                        // Non-uniform pixel durations: binary search on the
                        // cumulative durations precomputed for this line
                        auto it = std::upper_bound(cumsum->begin(), cumsum->end(), static_cast<double>(time_offset));
                        raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                    } else {
                        // Uniform pixel durations: use simple division
                        double pixel_idx_float = static_cast<double>(time_offset) * pixel_duration_reciprocal;
                        raw_pixel = static_cast<int>(pixel_idx_float);
                    }
                }

                // If the computed raw index falls outside the line, skip (single comparison)
                if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) {
                    return;
                }
                // If the line was scanned right→left, flip the raw index
                int pixel_nbr = is_reversed ? (n_pixels_minus_1 - raw_pixel) : raw_pixel;

                // Insert the event index using cached pointer
                pixels_ptr[pixel_nbr].insert(event_i);
            };

            // Legacy per-event loop
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

                // Verify that the photon's micro time falls within the specified ranges (bitmap lookup)
                if (use_micro_time_filter) {
                    unsigned short micro_time = micro_times[event_i];
                    if (!passes_microtime_filter(micro_time, micro_time_valid, has_per_channel_bitmap,
                                                 do_split_fill, c, channels)) {
                        continue;
                    }
                }

                assign_photon(event_i);
            }

            // Clean up micro-time filter bitmap only if setup_microtime_filter allocated it
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

    // Legacy path populated the pixels eagerly
    drop_stream_masks();
    pixels_materialized_ = true;

    // Mark the image as filled so downstream methods know pixels are populated
    _is_filled_ = true;
}

void CLSMImage::ensure_pixels_materialized() {
    if (pixels_materialized_) return;
    pixels_materialized_ = true;  // set first: guards against re-entry
    if (!stream_masks_.empty() && mask_tttr_ != nullptr) {
        consume_masks_into_pixels(stream_masks_, mask_block_size_, mask_split_,
                                  mask_tttr_);
        for (auto &f: frames) {
            for (auto &l: f->lines) {
                for (auto &p: l->pixels) p.shrink_to_fit();
            }
        }
    }
    // The masks are kept: they are small (n_events/8 bytes per block) and
    // enable fused reads such as get_intensity without touching pixels.
}

void CLSMImage::consume_masks_into_pixels(
        const std::vector<std::vector<uint64_t>>& masks,
        size_t block_size, bool split, std::shared_ptr<TTTR> tttr_data
) {
    if (tttr_data == nullptr || masks.empty()) return;
    const size_t n_events = static_cast<size_t>(tttr_data->size());
    if (n_events == 0) return;

    // Hoisted macro time access (sequential keyframe reader avoids the
    // per-photon division of the compressed representation; bit-identical)
    const unsigned long long* mt_raw = tttr_data->is_macro_time_compression_enabled()
            ? nullptr : tttr_data->macro_times;
    const uint32_t* mt_comp = tttr_data->macro_times_compressed;
    const unsigned long long* mt_kfs = tttr_data->macro_time_keyframes;
    const size_t mt_interval = tttr_data->keyframe_interval;

    const signed char* event_types = tttr_data->event_types;
    const signed char* routing_channels_ptr = tttr_data->routing_channels;

#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
#else
    bool use_openmp = false;
    size_t min_frames_for_parallel = SIZE_MAX;
#endif
    (void) use_openmp;

    #pragma omp parallel for schedule(dynamic) if(use_openmp && frames.size() >= min_frames_for_parallel)
    for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
        CLSMFrame* frame = frames[f_idx];
        SeqMacroTime smt(mt_raw, mt_comp, mt_kfs, mt_interval);

        // Channel block of this frame; blocks beyond the mask list receive
        // nothing (matches the legacy check_channel_match == false behavior)
        const size_t mi = (split && block_size > 0)
                ? static_cast<size_t>(f_idx) / block_size : 0;
        if (mi >= masks.size()) continue;
        const uint64_t* accept_words = masks[mi].data();

        std::vector<unsigned long long> pixel_marker_times;
        if (settings.use_pixel_markers) {
            pixel_marker_times.reserve(n_pixel + 16);
        }

        for (size_t l_idx = 0; l_idx < frame->lines.size(); ++l_idx) {
            CLSMLine *line = frame->lines[l_idx];
            if (line->pixels.empty()) continue;

            auto pixel_duration = line->get_pixel_duration();
            const std::vector<double>* cumsum =
                    has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;
            if (pixel_duration == 0 && cumsum == nullptr) continue;

            bool is_reversed = settings.bidirectional_scan && ((l_idx % 2) == 1);
            int start_idx = line->get_start();
            int stop_idx = std::min(line->get_stop(), static_cast<int>(n_events));
            if (start_idx < 0 || stop_idx <= start_idx) continue;

            unsigned long long line_start_time = line->get_start_time(tttr_data);
            int n_pixels_minus_1 = static_cast<int>(line->pixels.size()) - 1;
            double pixel_duration_reciprocal = 1.0 / static_cast<double>(pixel_duration);
            auto* pixels_ptr = line->pixels.data();

            // Collect pixel marker times if marker-based binning is enabled
            if (settings.use_pixel_markers) {
                pixel_marker_times.clear();
                smt.reset(static_cast<size_t>(start_idx));
                for (int event_i = start_idx; event_i < stop_idx; ++event_i) {
                    if (event_types[event_i] == RECORD_MARKER && routing_channels_ptr[event_i] == settings.marker_pixel) {
                        pixel_marker_times.push_back(smt.at(static_cast<size_t>(event_i)));
                    }
                }
            }

            // Assign one accepted photon to its pixel (acceptance was baked
            // into the mask at fill time)
            auto assign_photon = [&](int event_i) {
                int raw_pixel = -1;
                if (settings.use_pixel_markers) {
                    if (pixel_marker_times.empty()) return;
                    unsigned long long photon_time = smt.at(static_cast<size_t>(event_i));
                    auto it = std::upper_bound(pixel_marker_times.begin(), pixel_marker_times.end(), photon_time);

                    if (it == pixel_marker_times.begin()) {
                        return; // Before first marker
                    } else if (it == pixel_marker_times.end()) {
                        raw_pixel = static_cast<int>(pixel_marker_times.size()) - 1;
                    } else {
                        raw_pixel = static_cast<int>(std::distance(pixel_marker_times.begin(), it) - 1);
                    }
                } else {
                    unsigned long long time_offset = smt.at(static_cast<size_t>(event_i)) - line_start_time;
                    if (cumsum != nullptr) {
                        auto it = std::upper_bound(cumsum->begin(), cumsum->end(), static_cast<double>(time_offset));
                        raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                    } else {
                        raw_pixel = static_cast<int>(
                                static_cast<double>(time_offset) * pixel_duration_reciprocal);
                    }
                }
                if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) return;
                int pixel_nbr = is_reversed ? (n_pixels_minus_1 - raw_pixel) : raw_pixel;
                pixels_ptr[pixel_nbr].insert(event_i);
            };

            // Visit only accepted photons; zero words skip 64 events at once
            smt.reset(static_cast<size_t>(start_idx));
            const int64_t w_first = start_idx >> 6;
            const int64_t w_last = (stop_idx - 1) >> 6;
            for (int64_t wi = w_first; wi <= w_last; ++wi) {
                uint64_t w = accept_words[wi];
                if (wi == w_first) {
                    w &= (~0ull) << (start_idx & 63);
                }
                if (wi == w_last) {
                    const int r = (stop_idx - 1) & 63;
                    if (r != 63) w &= (1ull << (r + 1)) - 1;
                }
                while (w) {
                    const int b = tttrlib::bitops::ctz64(w);
                    w &= w - 1;   // clear lowest set bit; ascending order
                    assign_photon(static_cast<int>((wi << 6) + b));
                }
            }
        }
    }
}

template<typename Visitor>
void CLSMImage::for_each_mask_photon(bool parallel_frames, Visitor&& visit) {
    if (stream_masks_.empty() || mask_tttr_ == nullptr) return;
    std::shared_ptr<TTTR> tttr_data = mask_tttr_;
    const size_t n_events = static_cast<size_t>(tttr_data->size());
    if (n_events == 0) return;

    // Hoisted macro time access (sequential keyframe reader avoids the
    // per-photon division of the compressed representation; bit-identical)
    const unsigned long long* mt_raw = tttr_data->is_macro_time_compression_enabled()
            ? nullptr : tttr_data->macro_times;
    const uint32_t* mt_comp = tttr_data->macro_times_compressed;
    const unsigned long long* mt_kfs = tttr_data->macro_time_keyframes;
    const size_t mt_interval = tttr_data->keyframe_interval;

    const signed char* event_types = tttr_data->event_types;
    const signed char* routing_channels_ptr = tttr_data->routing_channels;

#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
#else
    bool use_openmp = false;
    size_t min_frames_for_parallel = SIZE_MAX;
#endif
    (void) use_openmp;

    #pragma omp parallel for schedule(dynamic) if(parallel_frames && use_openmp && frames.size() >= min_frames_for_parallel)
    for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
        CLSMFrame* frame = frames[f_idx];
        SeqMacroTime smt(mt_raw, mt_comp, mt_kfs, mt_interval);

        const size_t mi = (mask_split_ && mask_block_size_ > 0)
                ? static_cast<size_t>(f_idx) / mask_block_size_ : 0;
        if (mi >= stream_masks_.size()) continue;
        const uint64_t* accept_words = stream_masks_[mi].data();

        std::vector<unsigned long long> pixel_marker_times;
        if (settings.use_pixel_markers) {
            pixel_marker_times.reserve(n_pixel + 16);
        }

        for (size_t l_idx = 0; l_idx < frame->lines.size(); ++l_idx) {
            CLSMLine *line = frame->lines[l_idx];
            if (line->pixels.empty()) continue;

            auto pixel_duration = line->get_pixel_duration();
            const std::vector<double>* cumsum =
                    has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;
            if (pixel_duration == 0 && cumsum == nullptr) continue;

            bool is_reversed = settings.bidirectional_scan && ((l_idx % 2) == 1);
            int start_idx = line->get_start();
            int stop_idx = std::min(line->get_stop(), static_cast<int>(n_events));
            if (start_idx < 0 || stop_idx <= start_idx) continue;

            unsigned long long line_start_time = line->get_start_time(tttr_data);
            int n_pixels_minus_1 = static_cast<int>(line->pixels.size()) - 1;
            double pixel_duration_reciprocal = 1.0 / static_cast<double>(pixel_duration);

            if (settings.use_pixel_markers) {
                pixel_marker_times.clear();
                smt.reset(static_cast<size_t>(start_idx));
                for (int event_i = start_idx; event_i < stop_idx; ++event_i) {
                    if (event_types[event_i] == RECORD_MARKER && routing_channels_ptr[event_i] == settings.marker_pixel) {
                        pixel_marker_times.push_back(smt.at(static_cast<size_t>(event_i)));
                    }
                }
            }

            auto assign_photon = [&](int event_i) {
                int raw_pixel = -1;
                if (settings.use_pixel_markers) {
                    if (pixel_marker_times.empty()) return;
                    unsigned long long photon_time = smt.at(static_cast<size_t>(event_i));
                    auto it = std::upper_bound(pixel_marker_times.begin(), pixel_marker_times.end(), photon_time);
                    if (it == pixel_marker_times.begin()) {
                        return;
                    } else if (it == pixel_marker_times.end()) {
                        raw_pixel = static_cast<int>(pixel_marker_times.size()) - 1;
                    } else {
                        raw_pixel = static_cast<int>(std::distance(pixel_marker_times.begin(), it) - 1);
                    }
                } else {
                    unsigned long long time_offset = smt.at(static_cast<size_t>(event_i)) - line_start_time;
                    if (cumsum != nullptr) {
                        auto it = std::upper_bound(cumsum->begin(), cumsum->end(), static_cast<double>(time_offset));
                        raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                    } else {
                        raw_pixel = static_cast<int>(
                                static_cast<double>(time_offset) * pixel_duration_reciprocal);
                    }
                }
                if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) return;
                int pixel_nbr = is_reversed ? (n_pixels_minus_1 - raw_pixel) : raw_pixel;
                visit(f_idx, l_idx, line, pixel_nbr, event_i);
            };

            smt.reset(static_cast<size_t>(start_idx));
            const int64_t w_first = start_idx >> 6;
            const int64_t w_last = (stop_idx - 1) >> 6;
            for (int64_t wi = w_first; wi <= w_last; ++wi) {
                uint64_t w = accept_words[wi];
                if (wi == w_first) w &= (~0ull) << (start_idx & 63);
                if (wi == w_last) {
                    const int r = (stop_idx - 1) & 63;
                    if (r != 63) w &= (1ull << (r + 1)) - 1;
                }
                while (w) {
                    const int b = tttrlib::bitops::ctz64(w);
                    w &= w - 1;
                    assign_photon(static_cast<int>((wi << 6) + b));
                }
            }
        }
    }
}

void CLSMImage::strip(const std::vector<int> &tttr_indices, int offset) {
    // Pixel contents change: bring them up to date first, the stream masks
    // become stale afterwards
    materialize_pixel_handles();
    drop_stream_masks();
    for (auto &f: get_frames()) {
        for (auto &l: f->get_lines()) {
            for (auto &p: l->get_pixels()) {
                offset = p.strip(tttr_indices, offset);
            }
        }
    }
}

void CLSMImage::get_intensity_masked(
        unsigned short **output, int *dim1, int *dim2, int *dim3,
        std::shared_ptr<TTTR> tttr_data,
        std::vector<int> channels,
        std::vector<std::pair<int,int>> micro_time_ranges
) {
    *dim1 = static_cast<int>(n_frames);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    const size_t n_total = n_frames * n_lines * n_pixel;
    auto* img = static_cast<unsigned short*>(
            calloc(std::max(n_total, size_t(1)), sizeof(unsigned short)));
    *output = img;

    if (tttr_data == nullptr) tttr_data = tttr;
    if (img == nullptr || tttr_data == nullptr || n_total == 0) return;

    if (channels.empty()) {
        signed char* chs; int nchs;
        tttr_data->get_used_routing_channels(&chs, &nchs);
        for (int i = 0; i < nchs; i++) channels.emplace_back(chs[i]);
        free(chs);
    }

    const size_t n_events = static_cast<size_t>(tttr_data->size());
    if (n_events == 0) return;

    bool channel_lookup[TTTRLIB_MAX_ROUTING_CHANNELS] = {false};
    for (auto ch: channels) {
        if (ch >= 0 && ch < TTTRLIB_MAX_ROUTING_CHANNELS) channel_lookup[ch] = true;
    }

    // Micro time acceptance table (inclusive bounds, same as fill)
    std::unique_ptr<bool[]> mt_table;
    if (!micro_time_ranges.empty()) {
        mt_table.reset(new bool[65536]);
        std::memset(mt_table.get(), 0, 65536);
        for (const auto& r: micro_time_ranges) {
            int start = std::max(0, r.first);
            int end = std::min(65535, r.second);
            if (end >= start) std::memset(&mt_table[start], 1, end - start + 1);
        }
    }

    const signed char* event_types = tttr_data->event_types;
    const signed char* routing_channels_ptr = tttr_data->routing_channels;
    const unsigned short* micro_times = tttr_data->micro_times;

    // Hoist access to macro times. A compressed TTTR stores deltas and
    // keyframes; SeqMacroTime advances the keyframe cursor as a line is
    // consumed, avoiding a division in TTTR::get_macro_time_at for every
    // accepted photon.
    const unsigned long long* mt_raw = tttr_data->is_macro_time_compression_enabled()
            ? nullptr : tttr_data->macro_times;
    const uint32_t* mt_comp = tttr_data->macro_times_compressed;
    const unsigned long long* mt_kfs = tttr_data->macro_time_keyframes;
    const size_t mt_interval = tttr_data->keyframe_interval;

#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
#else
    bool use_openmp = false;
    size_t min_frames_for_parallel = SIZE_MAX;
#endif
    (void) use_openmp;

    // Fused filtering and consumption: intensity is the terminal result, so
    // unlike fill() there is no later consumer for a stream-wide acceptance
    // bitmask. Scanning that bitmap after creating it made this path visit
    // most imaging photons twice. Filter and scatter within the same line
    // traversal instead. Flatten (frame, line)
    //    into independent work items: every line writes a disjoint image
    //    region, so a single- or few-frame image (e.g. one confocal frame with
    //    many lines) still parallelizes across all cores over lines rather than
    //    running serially because there is only one frame.
    struct MaskLineWork { CLSMFrame* frame; unsigned short* frame_img; size_t l_idx; };
    std::vector<MaskLineWork> line_work;
    {
        size_t total_lines = 0;
        for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
            if (static_cast<size_t>(f_idx) >= n_frames) continue;
            total_lines += std::min(frames[f_idx]->lines.size(), n_lines);
        }
        line_work.reserve(total_lines);
        for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
            if (static_cast<size_t>(f_idx) >= n_frames) continue;
            CLSMFrame* frame = frames[f_idx];
            unsigned short* frame_img =
                    img + static_cast<size_t>(f_idx) * n_lines * n_pixel;
            const size_t lines_in_frame = std::min(frame->lines.size(), n_lines);
            for (size_t l_idx = 0; l_idx < lines_in_frame; ++l_idx)
                line_work.push_back({frame, frame_img, l_idx});
        }
    }
    const bool par_lines = use_openmp && line_work.size() >= min_frames_for_parallel;
    #pragma omp parallel for schedule(dynamic) if(par_lines)
    for (int wk = 0; wk < static_cast<int>(line_work.size()); ++wk) {
        CLSMFrame* frame = line_work[wk].frame;
        unsigned short* frame_img = line_work[wk].frame_img;
        const size_t l_idx = line_work[wk].l_idx;
        {
            CLSMLine* line = frame->lines[l_idx];

            // Use the image n_pixel (not line->size()) so this "virtual fill"
            // works even when per-pixel objects were never allocated.
            auto pixel_duration = line->get_pixel_duration_for(n_pixel);
            const std::vector<double>* cumsum =
                    has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;
            if (pixel_duration == 0 && cumsum == nullptr) continue;

            const bool is_reversed = settings.bidirectional_scan && ((l_idx % 2) == 1);
            const int start_idx = line->get_start();
            const int stop_idx = std::min(line->get_stop(), static_cast<int>(n_events));
            if (start_idx < 0 || stop_idx <= start_idx) continue;

            const unsigned long long line_start_time = line->get_start_time(tttr_data);
            const int n_pixels_minus_1 = static_cast<int>(n_pixel) - 1;
            const double pixel_duration_reciprocal = 1.0 / static_cast<double>(pixel_duration);
            unsigned short* line_img = frame_img + l_idx * n_pixel;
            SeqMacroTime smt(mt_raw, mt_comp, mt_kfs, mt_interval);
            smt.reset(static_cast<size_t>(start_idx));

            for (int event_i = start_idx; event_i < stop_idx; ++event_i) {
                if (event_types[event_i] != RECORD_PHOTON) continue;
                const unsigned char channel =
                        static_cast<unsigned char>(routing_channels_ptr[event_i]);
                if (!channel_lookup[channel]) continue;
                if (mt_table != nullptr && !mt_table[micro_times[event_i]]) continue;

                const unsigned long long time_offset =
                        smt.at(static_cast<size_t>(event_i)) - line_start_time;
                int raw_pixel;
                if (cumsum != nullptr) {
                    auto it = std::upper_bound(cumsum->begin(), cumsum->end(),
                                               static_cast<double>(time_offset));
                    raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                } else {
                    raw_pixel = static_cast<int>(
                            static_cast<double>(time_offset) * pixel_duration_reciprocal);
                }
                if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) continue;
                const int pixel_nbr = is_reversed ? (n_pixels_minus_1 - raw_pixel) : raw_pixel;
                line_img[pixel_nbr]++;
            }
        }
    }
}

template<typename T>
void CLSMImage::get_intensity_from_masks_t(
        T **output, int *dim1, int *dim2, int *dim3
) {
    // Fused counts from the stream acceptance masks: one pass per channel
    // block, no per-pixel index vectors touched (same binning as
    // consume_masks_into_pixels; scatter-add instead of insert).
    //
    // The counter width is a template parameter: the 16-bit instantiation wraps
    // at 65536 photons/pixel (historical behaviour, kept bit-identical), the
    // 32-bit one does not.
    *dim1 = static_cast<int>(n_frames);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    const size_t n_total = n_frames * n_lines * n_pixel;
    auto* img = static_cast<T*>(
            calloc(std::max(n_total, size_t(1)), sizeof(T)));
    *output = img;
    if (img == nullptr || n_total == 0) return;
    if (stream_masks_.empty() || mask_tttr_ == nullptr) return;

    std::shared_ptr<TTTR> tttr_data = mask_tttr_;
    const size_t n_events = static_cast<size_t>(tttr_data->size());
    if (n_events == 0) return;

    // Hoisted macro time access (sequential keyframe reader avoids the
    // per-photon division of the compressed representation; bit-identical)
    const unsigned long long* mt_raw = tttr_data->is_macro_time_compression_enabled()
            ? nullptr : tttr_data->macro_times;
    const uint32_t* mt_comp = tttr_data->macro_times_compressed;
    const unsigned long long* mt_kfs = tttr_data->macro_time_keyframes;
    const size_t mt_interval = tttr_data->keyframe_interval;

#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
#else
    bool use_openmp = false;
    size_t min_frames_for_parallel = SIZE_MAX;
#endif
    (void) use_openmp;

    #pragma omp parallel for schedule(dynamic) if(use_openmp && frames.size() >= min_frames_for_parallel)
    for (int f_idx = 0; f_idx < static_cast<int>(frames.size()); ++f_idx) {
        if (static_cast<size_t>(f_idx) >= n_frames) continue;
        CLSMFrame* frame = frames[f_idx];
        SeqMacroTime smt(mt_raw, mt_comp, mt_kfs, mt_interval);

        const size_t mi = (mask_split_ && mask_block_size_ > 0)
                ? static_cast<size_t>(f_idx) / mask_block_size_ : 0;
        if (mi >= stream_masks_.size()) continue;
        const uint64_t* accept_words = stream_masks_[mi].data();

        T* frame_img = img + static_cast<size_t>(f_idx) * n_lines * n_pixel;
        const size_t lines_in_frame = std::min(frame->lines.size(), n_lines);
        for (size_t l_idx = 0; l_idx < lines_in_frame; ++l_idx) {
            CLSMLine* line = frame->lines[l_idx];
            if (line->pixels.empty()) continue;

            auto pixel_duration = line->get_pixel_duration();
            const std::vector<double>* cumsum =
                    has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;
            if (pixel_duration == 0 && cumsum == nullptr) continue;

            const bool is_reversed = settings.bidirectional_scan && ((l_idx % 2) == 1);
            const int start_idx = line->get_start();
            const int stop_idx = std::min(line->get_stop(), static_cast<int>(n_events));
            if (start_idx < 0 || stop_idx <= start_idx) continue;

            const unsigned long long line_start_time = line->get_start_time(tttr_data);
            const int n_pixels_minus_1 = static_cast<int>(line->pixels.size()) - 1;
            const double pixel_duration_reciprocal = 1.0 / static_cast<double>(pixel_duration);
            T* line_img = frame_img + l_idx * n_pixel;

            smt.reset(static_cast<size_t>(start_idx));
            const int64_t w_first = start_idx >> 6;
            const int64_t w_last = (stop_idx - 1) >> 6;
            for (int64_t wi = w_first; wi <= w_last; ++wi) {
                uint64_t w = accept_words[wi];
                if (wi == w_first) w &= (~0ull) << (start_idx & 63);
                if (wi == w_last) {
                    const int r = (stop_idx - 1) & 63;
                    if (r != 63) w &= (1ull << (r + 1)) - 1;
                }
                while (w) {
                    const int b = tttrlib::bitops::ctz64(w);
                    w &= w - 1;
                    const int event_i = static_cast<int>((wi << 6) + b);

                    unsigned long long time_offset =
                            smt.at(static_cast<size_t>(event_i)) - line_start_time;
                    int raw_pixel;
                    if (cumsum != nullptr) {
                        auto it = std::upper_bound(cumsum->begin(), cumsum->end(),
                                                   static_cast<double>(time_offset));
                        raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                    } else {
                        raw_pixel = static_cast<int>(
                                static_cast<double>(time_offset) * pixel_duration_reciprocal);
                    }
                    if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) continue;
                    const int pixel_nbr = is_reversed ? (n_pixels_minus_1 - raw_pixel) : raw_pixel;
                    if (pixel_nbr < static_cast<int>(n_pixel)) line_img[pixel_nbr]++;
                }
            }
        }
    }
}

template void CLSMImage::get_intensity_from_masks_t<unsigned short>(unsigned short**, int*, int*, int*);
template void CLSMImage::get_intensity_from_masks_t<unsigned int>(unsigned int**, int*, int*, int*);

void CLSMImage::get_intensity_from_masks(unsigned short **output, int *dim1, int *dim2, int *dim3) {
    get_intensity_from_masks_t<unsigned short>(output, dim1, dim2, dim3);
}

void CLSMImage::get_intensity_from_masks_u32(unsigned int **output, int *dim1, int *dim2, int *dim3) {
    get_intensity_from_masks_t<unsigned int>(output, dim1, dim2, dim3);
}

void CLSMImage::get_tttr_indices(int** output, int* n_output) {
    *output = nullptr;
    *n_output = 0;

    // Marker-based binning is not replicated in the mask scan below
    if (settings.use_pixel_markers) ensure_pixels_materialized();

    std::vector<int> all;
    if (!pixels_materialized_ && !stream_masks_.empty() && mask_tttr_ != nullptr) {
        // Collect the assigned photons straight from the stream masks
        // (serial; per-line bounds identical to consume_masks_into_pixels)
        const size_t n_events = static_cast<size_t>(mask_tttr_->size());
        const unsigned long long* mt_raw = mask_tttr_->is_macro_time_compression_enabled()
                ? nullptr : mask_tttr_->macro_times;
        SeqMacroTime smt(mt_raw, mask_tttr_->macro_times_compressed,
                         mask_tttr_->macro_time_keyframes, mask_tttr_->keyframe_interval);
        for (size_t f_idx = 0; f_idx < frames.size(); ++f_idx) {
            const size_t mi = (mask_split_ && mask_block_size_ > 0)
                    ? f_idx / mask_block_size_ : 0;
            if (mi >= stream_masks_.size()) continue;
            const uint64_t* accept_words = stream_masks_[mi].data();
            CLSMFrame* frame = frames[f_idx];
            for (size_t l_idx = 0; l_idx < frame->lines.size(); ++l_idx) {
                CLSMLine* line = frame->lines[l_idx];
                if (line->pixels.empty()) continue;
                auto pixel_duration = line->get_pixel_duration();
                const std::vector<double>* cumsum =
                        has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;
                if (pixel_duration == 0 && cumsum == nullptr) continue;
                const int start_idx = line->get_start();
                const int stop_idx = std::min(line->get_stop(), static_cast<int>(n_events));
                if (start_idx < 0 || stop_idx <= start_idx) continue;
                const unsigned long long line_start_time = line->get_start_time(mask_tttr_);
                const int n_pixels_minus_1 = static_cast<int>(line->pixels.size()) - 1;
                const double reciprocal = 1.0 / static_cast<double>(pixel_duration);

                smt.reset(static_cast<size_t>(start_idx));
                const int64_t w_first = start_idx >> 6;
                const int64_t w_last = (stop_idx - 1) >> 6;
                for (int64_t wi = w_first; wi <= w_last; ++wi) {
                    uint64_t w = accept_words[wi];
                    if (wi == w_first) w &= (~0ull) << (start_idx & 63);
                    if (wi == w_last) {
                        const int r = (stop_idx - 1) & 63;
                        if (r != 63) w &= (1ull << (r + 1)) - 1;
                    }
                    while (w) {
                        const int b = tttrlib::bitops::ctz64(w);
                        w &= w - 1;
                        const int event_i = static_cast<int>((wi << 6) + b);
                        unsigned long long time_offset =
                                smt.at(static_cast<size_t>(event_i)) - line_start_time;
                        int raw_pixel;
                        if (cumsum != nullptr) {
                            auto it = std::upper_bound(cumsum->begin(), cumsum->end(),
                                                       static_cast<double>(time_offset));
                            raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                        } else {
                            raw_pixel = static_cast<int>(
                                    static_cast<double>(time_offset) * reciprocal);
                        }
                        if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) continue;
                        all.push_back(event_i);
                    }
                }
            }
        }
    } else {
        // Aggregate from the materialized pixels
        for (auto* f: frames) {
            for (auto* l: f->lines) {
                for (auto& p: l->pixels) {
                    const auto& v = p.get_tttr_indices();
                    all.insert(all.end(), v.begin(), v.end());
                }
            }
        }
    }
    std::sort(all.begin(), all.end());

    *n_output = static_cast<int>(all.size());
    auto* out = static_cast<int*>(malloc(std::max(all.size(), size_t(1)) * sizeof(int)));
    if (out == nullptr) { *n_output = 0; return; }
    std::memcpy(out, all.data(), all.size() * sizeof(int));
    *output = out;
}

void CLSMImage::get_photon_positions(
    TTTR* tttr,
    int** out_frame,
    int** out_line,
    double** out_x_exact,
    double** out_y_line,
    int** out_event_idx,
    int* n_photons
) {
    // Initialize outputs
    *out_frame = nullptr;
    *out_line = nullptr;
    *out_x_exact = nullptr;
    *out_y_line = nullptr;
    *out_event_idx = nullptr;
    *n_photons = 0;

    if (!tttr) {
        return;
    }

    // Temporary vectors to collect data
    std::vector<int> frames_vec;
    std::vector<int> lines_vec;
    std::vector<double> x_exact_vec;
    std::vector<double> y_line_vec;
    std::vector<int> event_idx_vec;

    // Use the mask path if available (more efficient)
    if (!pixels_materialized_ && !stream_masks_.empty() && mask_tttr_ != nullptr) {
        const size_t n_events = static_cast<size_t>(mask_tttr_->size());
        const unsigned long long* mt_raw = mask_tttr_->is_macro_time_compression_enabled()
                ? nullptr : mask_tttr_->macro_times;
        SeqMacroTime smt(mt_raw, mask_tttr_->macro_times_compressed,
                         mask_tttr_->macro_time_keyframes, mask_tttr_->keyframe_interval);

        for (size_t f_idx = 0; f_idx < frames.size(); ++f_idx) {
            const size_t mi = (mask_split_ && mask_block_size_ > 0)
                    ? f_idx / mask_block_size_ : 0;
            if (mi >= stream_masks_.size()) continue;
            const uint64_t* accept_words = stream_masks_[mi].data();
            CLSMFrame* frame = frames[f_idx];
            int frame_idx = static_cast<int>(f_idx);

            for (size_t l_idx = 0; l_idx < frame->lines.size(); ++l_idx) {
                CLSMLine* line = frame->lines[l_idx];
                if (line->pixels.empty()) continue;
                auto pixel_duration = line->get_pixel_duration();
                const std::vector<double>* cumsum =
                        has_non_uniform_durations() ? line_cumsum(l_idx) : nullptr;
                if (pixel_duration == 0 && cumsum == nullptr) continue;
                const int start_idx = line->get_start();
                const int stop_idx = std::min(line->get_stop(), static_cast<int>(n_events));
                if (start_idx < 0 || stop_idx <= start_idx) continue;
                const unsigned long long line_start_time = line->get_start_time(mask_tttr_);
                const int n_pixels_minus_1 = static_cast<int>(line->pixels.size()) - 1;
                const double reciprocal = 1.0 / static_cast<double>(pixel_duration);
                int line_idx = static_cast<int>(l_idx);

                smt.reset(static_cast<size_t>(start_idx));
                const int64_t w_first = start_idx >> 6;
                const int64_t w_last = (stop_idx - 1) >> 6;

                for (int64_t wi = w_first; wi <= w_last; ++wi) {
                    uint64_t w = accept_words[wi];
                    if (wi == w_first) w &= (~0ull) << (start_idx & 63);
                    if (wi == w_last) {
                        const int r = (stop_idx - 1) & 63;
                        if (r != 63) w &= (1ull << (r + 1)) - 1;
                    }

                    while (w) {
                        const int b = tttrlib::bitops::ctz64(w);
                        w &= w - 1;
                        const int event_i = static_cast<int>((wi << 6) + b);
                        unsigned long long time_offset =
                                smt.at(static_cast<size_t>(event_i)) - line_start_time;

                        // Compute exact x position
                        int raw_pixel;
                        if (cumsum != nullptr) {
                            auto it = std::upper_bound(cumsum->begin(), cumsum->end(),
                                                           static_cast<double>(time_offset));
                            raw_pixel = static_cast<int>(std::distance(cumsum->begin(), it));
                        } else {
                            raw_pixel = static_cast<int>(
                                    static_cast<double>(time_offset) * reciprocal);
                        }

                        if (raw_pixel > n_pixels_minus_1 || raw_pixel < 0) continue;

                        // Exact fractional x position within the line
                        double x_exact = static_cast<double>(raw_pixel) +
                                       (static_cast<double>(time_offset) -
                                        static_cast<double>(raw_pixel) * pixel_duration) / pixel_duration;
                        double y_line = static_cast<double>(line_idx);

                        frames_vec.push_back(frame_idx);
                        lines_vec.push_back(line_idx);
                        x_exact_vec.push_back(x_exact);
                        y_line_vec.push_back(y_line);
                        event_idx_vec.push_back(event_i);
                    }
                }
            }
        }
    } else {
        // Fallback: materialized pixel path
        ensure_pixels_materialized();
        for (size_t f_idx = 0; f_idx < frames.size(); ++f_idx) {
            CLSMFrame* frame = frames[f_idx];
            int frame_idx = static_cast<int>(f_idx);
            for (size_t l_idx = 0; l_idx < frame->lines.size(); ++l_idx) {
                CLSMLine* line = frame->lines[l_idx];
                int line_idx = static_cast<int>(l_idx);
                double y_line = static_cast<double>(line_idx);
                const unsigned long long line_start_time = line->get_start_time(this->tttr);
                auto pixel_duration = line->get_pixel_duration();
                if (pixel_duration == 0) pixel_duration = 1;  // avoid division by zero

                const int n_pixels_minus_1 = static_cast<int>(line->pixels.size()) - 1;
                for (size_t p_idx = 0; p_idx < line->pixels.size(); ++p_idx) {
                    CLSMPixel& pixel = line->pixels[p_idx];
                    const auto& indices = pixel.get_tttr_indices();

                    for (int event_i : indices) {
                        unsigned long long mt = tttr->get_macro_time_at(event_i);
                        unsigned long long time_offset = mt - line_start_time;

                        // Exact fractional x position. time_offset runs from the
                        // start of the *line*, so dividing by the dwell time
                        // already yields the position in pixel units -- adding
                        // p_idx on top would roughly double it.
                        double x_exact = static_cast<double>(time_offset) / pixel_duration;
                        if (x_exact < 0.0 || x_exact > static_cast<double>(n_pixels_minus_1) + 1.0)
                            continue;

                        frames_vec.push_back(frame_idx);
                        lines_vec.push_back(line_idx);
                        x_exact_vec.push_back(x_exact);
                        y_line_vec.push_back(y_line);
                        event_idx_vec.push_back(event_i);
                    }
                }
            }
        }
    }

    // Allocate output arrays
    size_t n = frames_vec.size();
    if (n == 0) {
        *n_photons = 0;
        return;
    }

    *n_photons = static_cast<int>(n);

    int* frame_arr = static_cast<int*>(malloc(n * sizeof(int)));
    int* line_arr = static_cast<int*>(malloc(n * sizeof(int)));
    double* x_arr = static_cast<double*>(malloc(n * sizeof(double)));
    double* y_arr = static_cast<double*>(malloc(n * sizeof(double)));
    int* event_arr = static_cast<int*>(malloc(n * sizeof(int)));

    if (!frame_arr || !line_arr || !x_arr || !y_arr || !event_arr) {
        // Clean up on allocation failure
        if (frame_arr) std::free(frame_arr);
        if (line_arr) std::free(line_arr);
        if (x_arr) std::free(x_arr);
        if (y_arr) std::free(y_arr);
        if (event_arr) std::free(event_arr);
        *n_photons = 0;
        return;
    }

    // Copy data
    std::memcpy(frame_arr, frames_vec.data(), n * sizeof(int));
    std::memcpy(line_arr, lines_vec.data(), n * sizeof(int));
    std::memcpy(x_arr, x_exact_vec.data(), n * sizeof(double));
    std::memcpy(y_arr, y_line_vec.data(), n * sizeof(double));
    std::memcpy(event_arr, event_idx_vec.data(), n * sizeof(int));

    *out_frame = frame_arr;
    *out_line = line_arr;
    *out_x_exact = x_arr;
    *out_y_line = y_arr;
    *out_event_idx = event_arr;
}

template<typename T>
void CLSMImage::get_intensity_t(T **output, int *dim1, int *dim2, int *dim3) {
    // Lazy fill: compute the counts straight from the stream masks (no
    // per-pixel index materialization). Marker-based binning is handled by
    // the materialized path.
    if (!pixels_materialized_ && !stream_masks_.empty()) {
        if (settings.use_pixel_markers) {
            ensure_pixels_materialized();
        } else {
            get_intensity_from_masks_t<T>(output, dim1, dim2, dim3);
            return;
        }
    }
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
    auto *t = (T *) malloc(n_pixel_total * sizeof(T));
    
    // Configure OpenMP for parallel intensity computation
#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
    size_t min_frames_for_parallel = use_openmp ? std::max(size_t(8), size_t(num_threads * 2)) : SIZE_MAX;
#else
    bool use_openmp = false;
    size_t min_frames_for_parallel = SIZE_MAX;
#endif
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && n_frames >= min_frames_for_parallel)
    for (int i_frame = 0; i_frame < static_cast<int>(n_frames); i_frame++) {
        auto &frame = frames[i_frame];
        
        // Use CLSMFrame::get_intensity_t() to get frame data
        T *frame_intensity = nullptr;
        int frame_lines = 0, frame_pixels = 0;
        frame->template get_intensity_t<T>(&frame_intensity, &frame_lines, &frame_pixels);
        
        // Copy frame intensity into the output array at the correct offset
        if (frame_intensity != nullptr) {
            size_t frame_offset = i_frame * n_lines * n_pixel;
            // Guard against buffer overflow: use the minimum of actual and expected frame size
            size_t frame_size = std::min(
                static_cast<size_t>(frame_lines) * static_cast<size_t>(frame_pixels),
                static_cast<size_t>(n_lines) * static_cast<size_t>(n_pixel)
            );
            std::memcpy(&t[frame_offset], frame_intensity, frame_size * sizeof(T));
            free(frame_intensity);
        }
    }

    *output = t;
}

template void CLSMImage::get_intensity_t<unsigned short>(unsigned short**, int*, int*, int*);
template void CLSMImage::get_intensity_t<unsigned int>(unsigned int**, int*, int*, int*);

void CLSMImage::get_intensity(unsigned short **output, int *dim1, int *dim2, int *dim3) {
    get_intensity_t<unsigned short>(output, dim1, dim2, dim3);
}

void CLSMImage::get_intensity_u32(unsigned int **output, int *dim1, int *dim2, int *dim3) {
    get_intensity_t<unsigned int>(output, dim1, dim2, dim3);
}

void CLSMImage::get_fluorescence_decay(
    TTTR *tttr_data,
    unsigned char **output, int *dim1, int *dim2, int *dim3, int *dim4,
    int micro_time_coarsening,
    bool stack_frames,
    int max_micro_time_channels
) {
    if (is_verbose()) {
        std::clog << "Get decay image" << std::endl;
    }
    size_t nf = (stack_frames) ? 1 : n_frames;
    size_t n_tac = tttr_data->header->get_number_of_micro_time_channels() / micro_time_coarsening;
    if (max_micro_time_channels > 0) {
        n_tac = std::min(n_tac, static_cast<size_t>(max_micro_time_channels));
    }
    *dim1 = static_cast<int>(nf);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    *dim4 = static_cast<int>(n_tac);

    size_t n_tac_total = nf * n_lines * n_pixel * n_tac;
    // Use malloc + memset for large arrays - faster than calloc
    auto *t = (unsigned char *) malloc(std::max(n_tac_total, size_t(1)) * sizeof(unsigned char));
    memset(t, 0, n_tac_total * sizeof(unsigned char));
    // Degenerate micro time axis: return the empty histogram instead of
    // writing out of bounds below
    if (n_tac == 0) {
        *output = t;
        return;
    }

    // Fused path: histogram straight from the stream masks (no per-pixel
    // index materialization). Stacked output shares frame 0, so it runs the
    // frame loop serially; non-stacked frames are independent and parallel.
    if (!pixels_materialized_ && !stream_masks_.empty()) {
        const unsigned short* mt = tttr_data->micro_times;
        if (micro_time_coarsening == 1) {
            // The overwhelmingly common uncoarsened case needs neither a
            // 65,536-entry division LUT nor an indirect lookup per photon.
            if (stack_frames) {
                // Keep the stacked hot loop branch-free: all frames share the
                // same output plane, so no frame-stride multiply is needed.
                for_each_mask_photon(false,
                    [&](int, size_t l, CLSMLine*, int p, int i) {
                        if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                        const size_t q = mt[i];
                        if (q >= n_tac) return;
                        t[(l * n_pixel + static_cast<size_t>(p)) * n_tac + q] += 1;
                    });
            } else {
                const size_t frame_stride = n_lines * n_pixel * n_tac;
                for_each_mask_photon(true,
                    [&](int f, size_t l, CLSMLine*, int p, int i) {
                        if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                        const size_t q = mt[i];
                        if (q >= n_tac) return;
                        const size_t base = static_cast<size_t>(f) * frame_stride +
                                            (l * n_pixel + static_cast<size_t>(p)) * n_tac;
                        t[base + q] += 1;
                    });
            }
            *output = t;
            return;
        }
        // Micro time -> histogram bin lookup (identical to the division,
        // computed once per possible micro time; -1 marks out-of-range bins)
        std::vector<int32_t> tac_lut(65536);
        for (int v = 0; v < 65536; v++) {
            size_t q = static_cast<size_t>(v) / micro_time_coarsening;
            tac_lut[v] = (q < n_tac) ? static_cast<int32_t>(q) : -1;
        }
        for_each_mask_photon(!stack_frames,
            [&](int f, size_t l, CLSMLine*, int p, int i) {
                if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                size_t target_frame = stack_frames ? 0 : static_cast<size_t>(f);
                size_t base = target_frame * (n_lines * n_pixel * n_tac) +
                              l * (n_pixel * n_tac) +
                              static_cast<size_t>(p) * n_tac;
                int32_t q = tac_lut[mt[i]];
                if (q >= 0) t[base + static_cast<size_t>(q)] += 1;
            });
        *output = t;
        return;
    }
    if (is_verbose()) {
        std::clog << "-- Number of frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel <<
                std::endl;
        std::clog << "-- Number of micro time channels: " << n_tac << std::endl;
        std::clog << "-- Micro time coarsening factor: " << micro_time_coarsening << std::endl;
        std::clog << "-- Final number of micro time channels: " << n_tac << std::endl;
    }
    
    // Configure OpenMP for parallel decay computation
#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
#else
    bool use_openmp = false;
#endif
    
    #pragma omp parallel for schedule(dynamic) if(use_openmp && n_frames > 4 && !stack_frames)
    for (int i_frame = 0; i_frame < static_cast<int>(n_frames); i_frame++) {
        auto frame = frames[i_frame];
        for (size_t i_line = 0; i_line < frame->lines.size(); i_line++) {
            auto line = frame->lines[i_line];
            for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                auto& pixel = line->pixels[i_pixel];
                // When stacking, all photons go into frame 0 of output
                size_t target_frame = stack_frames ? 0 : i_frame;
                size_t pixel_nbr = target_frame * (n_lines * n_pixel * n_tac) +
                                   i_line * (n_pixel * n_tac) +
                                   i_pixel * (n_tac);
                const auto& indices = pixel.get_tttr_indices();
                for (auto i: indices) {
                    size_t i_tac = tttr_data->micro_times[i] / micro_time_coarsening;
                    if (i_tac < n_tac) t[pixel_nbr + i_tac] += 1;
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
    ensure_pixels_materialized();
    if (clsm_other != nullptr) clsm_other->ensure_pixels_materialized();
    // clsm_other is documented as optional (nullptr = autocorrelate each pixel
    // with itself), and the guard above already allows for it -- but the frame
    // loop below dereferenced it unconditionally, so every call that omitted a
    // second image segfaulted. Fall back to this image.
    CLSMImage* other = (clsm_other != nullptr) ? clsm_other : this;
    // This method declares correlation_method = "default", but Correlator only
    // knows "wahl", "felekyan" and "laurence"; anything else logs a warning per
    // pixel and leaves the curve empty, so the default returned all zeros. Map it
    // onto Correlator's own default instead.
    const std::string method =
            (correlation_method == "default" || correlation_method.empty())
            ? std::string("wahl") : correlation_method;
    if (is_verbose()) {
        std::clog << "Get fluorescence correlation image" << std::endl;
    }
    size_t nf = (stack_frames) ? 1 : n_frames;
    auto corr = Correlator(tttr, method, n_bins, n_casc);
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
        auto corr = Correlator(tttr, method, n_bins, n_casc);
        auto frame = frames[i_frame];
        auto other_frame = other->frames[i_frame];
        
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
                    
                    // Build the pixel selections directly in the shared_ptrs.
                    // These used to be constructed as stack temporaries and then
                    // copy-assigned into the reused shared_ptrs; the assignment
                    // does not deep-copy the event buffers, so once the temporary
                    // went out of scope the correlator was reading freed memory
                    // and aborted on the first pixel with photons in it.
                    tttr_1_ptr = std::make_shared<TTTR>(
                        *tttr,
                        const_cast<int*>(v1.data()),
                        static_cast<int>(v1.size()),
                        false
                    );
                    tttr_2_ptr = std::make_shared<TTTR>(
                        *tttr,
                        const_cast<int*>(v2.data()),
                        static_cast<int>(v2.size()),
                        false
                    );

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
    bool stack_frames,
    std::vector<int> channels
) {
    // Channel-split extension: one decay per routing channel (frames stacked),
    // vstacked as [n_channels][n_tac]. Photons on unlisted channels are ignored.
    if (!channels.empty()) {
        std::map<int,int> col;
        for (size_t c = 0; c < channels.size(); ++c) col[channels[c]] = static_cast<int>(c);
        int n_ch = static_cast<int>(channels.size());
        size_t n_tac = tttr_data->header->get_number_of_micro_time_channels() / tac_coarsening;
        *dim1 = n_ch;
        *dim2 = static_cast<int>(n_tac);
        auto *t = (unsigned int *) calloc(std::max(static_cast<size_t>(n_ch) * n_tac, size_t(1)),
                                          sizeof(unsigned int));
        if (n_tac == 0) { *output = t; return; }
        if ((dmask1 != (int) n_frames) || (dmask2 != (int) n_lines) || (dmask3 != (int) n_pixel)) {
            std::cerr << "Error: the dimensions of the selection ("
                      << n_frames << ", " << n_lines << ", " << n_pixel
                      << ") does not match the CLSM image dimensions.";
            *output = t; return;
        }
        const unsigned short* mts = tttr_data->micro_times;
        const signed char* rc = tttr_data->routing_channels;
        if (!pixels_materialized_ && !stream_masks_.empty()) {
            for_each_mask_photon(false,
                [&](int f, size_t l, CLSMLine*, int p, int i) {
                    if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                    if (!mask[static_cast<size_t>(f) * (n_lines * n_pixel) + l * n_pixel + p]) return;
                    std::map<int,int>::const_iterator it = col.find((int) rc[i]);
                    if (it == col.end()) return;
                    size_t q = static_cast<size_t>(mts[i]) / tac_coarsening;
                    if (q < n_tac) t[static_cast<size_t>(it->second) * n_tac + q] += 1;
                });
        } else {
            for (size_t i_frame = 0; i_frame < n_frames; i_frame++) {
                auto frame = frames[i_frame];
                for (size_t i_line = 0; i_line < n_lines; i_line++) {
                    auto line = frame->lines[i_line];
                    for (size_t i_pixel = 0; i_pixel < n_pixel; i_pixel++) {
                        if (mask[i_frame * (n_lines * n_pixel) + i_line * n_pixel + i_pixel]) {
                            const auto& indices = line->pixels[i_pixel].get_tttr_indices();
                            for (auto i: indices) {
                                std::map<int,int>::const_iterator it = col.find((int) rc[i]);
                                if (it == col.end()) continue;
                                size_t q = static_cast<size_t>(mts[i]) / tac_coarsening;
                                if (q < n_tac) t[static_cast<size_t>(it->second) * n_tac + q] += 1;
                            }
                        }
                    }
                }
            }
        }
        *output = t;
        return;
    }

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
    auto *t = (unsigned int *) calloc(std::max(n_tac_total, size_t(1)), sizeof(unsigned int));
    if (n_tac == 0) {
        *output = t;  // degenerate micro time axis: empty histogram
        return;
    }
    if ((dmask1 != n_frames) || (dmask2 != n_lines) || (dmask3 != n_pixel)) {
        std::cerr << "Error: the dimensions of the selection ("
                << n_frames << ", " << n_lines << ", " << n_pixel
                << ") does not match the CLSM image dimensions.";
    } else if (!pixels_materialized_ && !stream_masks_.empty()) {
        // Fused path: masked pixel decays straight from the stream masks
        const unsigned short* mts = tttr_data->micro_times;
        std::vector<int32_t> tac_lut(65536);
        for (int v = 0; v < 65536; v++) {
            size_t q = static_cast<size_t>(v) / tac_coarsening;
            tac_lut[v] = (q < n_tac) ? static_cast<int32_t>(q) : -1;
        }
        for_each_mask_photon(false,
            [&](int f, size_t l, CLSMLine*, int p, int i) {
                if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                if (!mask[static_cast<size_t>(f) * (n_lines * n_pixel) + l * n_pixel + p]) return;
                int32_t q = tac_lut[mts[i]];
                size_t w_frame = stack_frames ? 0 : static_cast<size_t>(f);
                if (q >= 0) t[w_frame * n_tac + static_cast<size_t>(q)] += 1;
            });
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
                            if (i_tac < n_tac) t[w_frame * n_tac + i_tac] += 1;
                        }
                    }
                }
            }
            w_frame += !stack_frames;
        }
    }
    *output = t;
}

double CLSMImage::get_decay_irf_offset(TTTR *tttr_data, double microtime_resolution) {
    if (tttr_data == nullptr) tttr_data = tttr.get();
    if (tttr_data == nullptr) return 0.0;
    if (microtime_resolution < 0)
        microtime_resolution = tttr_data->header->get_micro_time_resolution();
    double *hist = nullptr; int nh = 0; double *tax = nullptr; int nt = 0;
    tttr_data->get_microtime_histogram(&hist, &nh, &tax, &nt, 1);
    double offset = 0.0;
    if (hist != nullptr && nh > 1) {
        // The micro_time == 0 bin holds a marker/zero-arrival-time spike that is
        // NOT the fluorescence rise, so ignore it when locating the decay peak.
        double peak = 0.0;
        for (int i = 1; i < nh; ++i) if (hist[i] > peak) peak = hist[i];
        // IRF offset = leading edge (rise): first bin reaching half the peak.
        double half = 0.5 * peak;
        int rise = 0;
        for (int i = 1; i < nh; ++i) if (hist[i] >= half) { rise = i; break; }
        offset = static_cast<double>(rise) * microtime_resolution;
    }
    if (hist != nullptr) free(hist);
    if (tax != nullptr) free(tax);
    return offset;
}

void CLSMImage::ensure_moment_cache(
        TTTR* tttr_data, bool stack_frames
) {
    size_t o_frames = stack_frames ? 1 : n_frames;
    const size_t n_out = o_frames * n_lines * n_pixel;
    bool ok = _lt_cache_valid_ && _lt_cache_stacked_ == stack_frames
              && _lt_m0_cache_.size() == n_out
              && _lt_m1_cache_.size() == n_out;
    if (ok) return;

    _lt_m0_cache_.assign(n_out, 0.0);
    _lt_m1_cache_.assign(n_out, 0.0);
    const unsigned short* mts = tttr_data->micro_times;

    // Accumulate in integers (exact, cheaper than FP, half the cache traffic for
    // m0), then convert to double once for the correction stage.
    std::vector<unsigned long long> m0_raw(n_out, 0), m1_raw(n_out, 0);

    if (!pixels_materialized_ && !stream_masks_.empty()) {
        // Fused stream-mask scan. for_each_mask_photon parallelizes over frames;
        // the stacked image would otherwise force a serial pass (all frames
        // write pixel 0). Because m0/m1 are integer sums, build the per-frame
        // moments in parallel and reduce -> bit-identical and fully threaded.
        const size_t plane = n_lines * n_pixel;
        if (!stack_frames) {
            for_each_mask_photon(true,
                [&](int f, size_t l, CLSMLine*, int p, int i) {
                    if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                    size_t idx = static_cast<size_t>(f) * plane + l * n_pixel + p;
                    m0_raw[idx] += 1;
                    m1_raw[idx] += mts[i];
                });
        } else {
            std::vector<unsigned long long> pf0(n_frames * plane, 0), pf1(n_frames * plane, 0);
            for_each_mask_photon(true,
                [&](int f, size_t l, CLSMLine*, int p, int i) {
                    if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                    size_t idx = static_cast<size_t>(f) * plane + l * n_pixel + p;
                    pf0[idx] += 1;
                    pf1[idx] += mts[i];
                });
            for (size_t f = 0; f < n_frames; ++f)
                for (size_t k = 0; k < plane; ++k) {
                    m0_raw[k] += pf0[f * plane + k];
                    m1_raw[k] += pf1[f * plane + k];
                }
        }
    } else {
        // Materialized per-pixel photon indices, parallelized over lines (each
        // line writes a disjoint row of the moment cache). m0/m1 are integer
        // sums, so the stacked image is just the per-frame sums added together
        // (associative -> bit-identical, and no per-pixel index copy needed).
#ifndef _WIN32
        int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
        bool use_openmp = (num_threads > 1);
#else
        bool use_openmp = false;
#endif
        const int NL = static_cast<int>(n_lines);
        #pragma omp parallel for schedule(dynamic) if(use_openmp && NL >= 8)
        for (int l = 0; l < NL; ++l) {
            if (stack_frames) {
                unsigned long long* m0row = &m0_raw[static_cast<size_t>(l) * n_pixel];
                unsigned long long* m1row = &m1_raw[static_cast<size_t>(l) * n_pixel];
                for (size_t f = 0; f < n_frames; ++f) {
                    if (static_cast<size_t>(l) >= frames[f]->lines.size()) continue;
                    CLSMLine* line = frames[f]->lines[l];
                    const size_t np = std::min(n_pixel, line->pixels.size());
                    for (size_t p = 0; p < np; ++p)
                        line->pixels[p].accumulate_moments(mts, m0row[p], m1row[p]);
                }
            } else {
                for (size_t of = 0; of < o_frames; ++of) {
                    if (static_cast<size_t>(l) >= frames[of]->lines.size()) continue;
                    CLSMLine* line = frames[of]->lines[l];
                    const size_t base = of * (n_lines * n_pixel) + static_cast<size_t>(l) * n_pixel;
                    const size_t np = std::min(n_pixel, line->pixels.size());
                    for (size_t p = 0; p < np; ++p)
                        line->pixels[p].accumulate_moments(mts, m0_raw[base + p], m1_raw[base + p]);
                }
            }
        }
    }
    for (size_t idx = 0; idx < n_out; ++idx) {
        _lt_m0_cache_[idx] = static_cast<double>(m0_raw[idx]);
        _lt_m1_cache_[idx] = static_cast<double>(m1_raw[idx]);
    }
    _lt_cache_valid_ = true;
    _lt_cache_stacked_ = stack_frames;
}

void CLSMImage::ensure_phasor_cache(
        TTTR* tttr_data, bool stack_frames, double frequency
) {
    size_t o_frames = stack_frames ? 1 : n_frames;
    const size_t n_out = o_frames * n_lines * n_pixel;
    bool ok = _ph_cache_valid_ && _ph_cache_stacked_ == stack_frames
              && _ph_cache_freq_ == frequency
              && _ph_g_cache_.size() == n_out
              && _ph_s_cache_.size() == n_out
              && _ph_cnt_cache_.size() == n_out;
    if (ok) return;

    // This traversal also (re)builds the shared moment cache when it is stale,
    // so a following get_mean_lifetime / get_mean_micro_time is correction-only
    // — the phasor pass and the moment pass are fused into one visit of the
    // photons, transparently, without a separate warm-up call.
    const bool need_moments = !(_lt_cache_valid_ && _lt_cache_stacked_ == stack_frames
                                && _lt_m0_cache_.size() == n_out
                                && _lt_m1_cache_.size() == n_out);

    _ph_g_cache_.assign(n_out, 0.0);
    _ph_s_cache_.assign(n_out, 0.0);
    _ph_cnt_cache_.assign(n_out, 0.0);
    if (need_moments) {
        _lt_m0_cache_.assign(n_out, 0.0);
        _lt_m1_cache_.assign(n_out, 0.0);
    }
    const unsigned short* mts = tttr_data->micro_times;
    const double factor = (2. * frequency * M_PI);

    if (!pixels_materialized_ && !stream_masks_.empty()) {
        // Micro times take at most 65536 values: LUT the cos/sin once (the same
        // std::cos/std::sin inputs the per-pixel path uses -> bit-identical).
        std::vector<double> cos_lut(65536), sin_lut(65536);
        for (int v = 0; v < 65536; v++) {
            unsigned short mtv = static_cast<unsigned short>(v);
            cos_lut[v] = std::cos(mtv * factor);
            sin_lut[v] = std::sin(mtv * factor);
        }
        for_each_mask_photon(!stack_frames,
            [&](int f, size_t l, CLSMLine*, int p, int i) {
                if (l >= n_lines || static_cast<size_t>(p) >= n_pixel) return;
                size_t of = stack_frames ? 0 : static_cast<size_t>(f);
                size_t idx = of * (n_lines * n_pixel) + l * n_pixel + p;
                const auto mtv = mts[i];
                _ph_g_cache_[idx] += cos_lut[mtv];
                _ph_s_cache_[idx] += sin_lut[mtv];
                _ph_cnt_cache_[idx] += 1.0;
                if (need_moments) {
                    _lt_m0_cache_[idx] += 1.0;
                    _lt_m1_cache_[idx] += mtv;
                }
            });
    } else {
        for (size_t of = 0; of < o_frames; ++of) {
            for (size_t l = 0; l < n_lines; ++l) {
                if (l >= frames[of]->lines.size()) continue;
                for (size_t p = 0; p < n_pixel; ++p) {
                    size_t idx = of * (n_lines * n_pixel) + l * n_pixel + p;
                    std::vector<int> idxs = stack_frames
                        ? collect_stacked_pixel_indices(frames, l, p)
                        : frames[of]->lines[l]->pixels[p].get_tttr_indices();
                    double g = 0.0, s = 0.0, sm = 0.0;
                    for (int ii : idxs) {
                        const auto mt = mts[ii];
                        g += std::cos(mt * factor);
                        s += std::sin(mt * factor);
                        if (need_moments) sm += mt;
                    }
                    _ph_g_cache_[idx] = g;
                    _ph_s_cache_[idx] = s;
                    _ph_cnt_cache_[idx] = static_cast<double>(idxs.size());
                    if (need_moments) {
                        _lt_m0_cache_[idx] = static_cast<double>(idxs.size());
                        _lt_m1_cache_[idx] = sm;
                    }
                }
            }
        }
    }
    _ph_cache_valid_ = true;
    _ph_cache_stacked_ = stack_frames;
    _ph_cache_freq_ = frequency;
    if (need_moments) {
        _lt_cache_valid_ = true;
        _lt_cache_stacked_ = stack_frames;
    }
}

void CLSMImage::get_mean_micro_time(
    TTTR *tttr_data,
    double **output, int *dim1, int *dim2, int *dim3,
    double microtime_resolution,
    int minimum_number_of_photons,
    bool stack_frames,
    bool correct_irf_offset
) {
    if (is_verbose()) {
        std::clog << "Get mean micro time image" << std::endl;
        std::clog << "-- Frames, lines, pixel: " << n_frames << ", " << n_lines << ", " << n_pixel << std::endl;
        std::clog << "-- Minimum number of photos: " << minimum_number_of_photons << std::endl;
        std::clog << "-- Computing stack of mean micro times " << std::endl;
    }
    if (microtime_resolution < 0)
        microtime_resolution = tttr_data->header->get_micro_time_resolution();

    // Instrument-response offset (from the decay rise), subtracted from valid
    // pixels so the mean arrival time becomes an IRF-referenced FastLifetime.
    // Invalid pixels keep their -1.0 sentinel; corrected values clamp at 0.
    double irf_offset = correct_irf_offset
        ? get_decay_irf_offset(tttr_data, microtime_resolution) : 0.0;
    auto apply_irf = [&](double *arr, size_t count) {
        if (irf_offset == 0.0) return;
        for (size_t i = 0; i < count; ++i)
            if (arr[i] >= 0.0) { arr[i] -= irf_offset; if (arr[i] < 0.0) arr[i] = 0.0; }
    };

    // The per-pixel iterative micro-time mean is independent of the resolution
    // and IRF offset, and is shared with get_mean_lifetime, so cache it: a
    // changed resolution / IRF offset then re-runs only the O(pixels)
    // correction. The mean micro time is m1/m0 (agrees with the legacy
    // iterative mean to ~1e-15, within the reference tolerance).
    ensure_moment_cache(tttr_data, stack_frames);
    const size_t o_frames = stack_frames ? 1 : n_frames;
    const size_t n_total = o_frames * n_lines * n_pixel;
    auto *t = (double *) malloc(std::max(n_total, size_t(1)) * sizeof(double));
    for (size_t idx = 0; idx < n_total; idx++) {
        const double m0 = _lt_m0_cache_[idx];
        double v = (m0 > 0.0 ? _lt_m1_cache_[idx] / m0 : 0.0) * microtime_resolution;
        if (m0 < minimum_number_of_photons) v = -1.0;
        t[idx] = v;
    }
    apply_irf(t, n_total);
    *dim1 = static_cast<int>(o_frames);
    *dim2 = static_cast<int>(n_lines);
    *dim3 = static_cast<int>(n_pixel);
    *output = t;
}

void CLSMImage::get_phasor(
    float **output, int *dim1, int *dim2, int *dim3, int *dim4,
    TTTR *tttr_data,
    TTTR *tttr_irf,
    double frequency,
    int minimum_number_of_photons,
    bool stack_frames,
    bool correct_irf_offset
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
        // compute_phasor returns the sentinel {-1, -1} when the IRF holds too
        // few photons. Feeding that back in as an IRF phasor is not a division
        // by zero -- its modulus is 2 -- so it used to sail through and rotate
        // every pixel by 225 degrees while halving it. Reject it instead.
        if (gs[0] == -1.0 && gs[1] == -1.0) {
            throw std::invalid_argument(
                "CLSMImage::get_phasor: the IRF has too few photons (" +
                std::to_string(tttr_irf->n_valid_events) +
                ") to define a phasor at this frequency, so it cannot be used "
                "as a calibration.");
        }
        g_irf = gs[0];
        s_irf = gs[1];
    }
    int o_frames = stack_frames ? 1 : static_cast<int>(n_frames);
    double factor = (2. * frequency * M_PI);

    // Rising-edge (IRF) correction: with no explicit IRF file, treat the decay
    // rise as a delta-function IRF at that micro-time channel. Its phasor is
    // (cos theta, sin theta) with theta = rise_channel * factor; DecayPhasor::g/s
    // then rotate every pixel phasor by -theta (both output paths use g_irf/s_irf).
    if (correct_irf_offset && tttr_irf == nullptr) {
        double rise_channels = get_decay_irf_offset(tttr_data, 1.0);
        g_irf = std::cos(rise_channels * factor);
        s_irf = std::sin(rise_channels * factor);
    }
    // The per-pixel g/s sums depend only on the frequency (and the photon
    // selection), not on the IRF — whose calibration (g_irf, s_irf) is a cheap
    // output rotation. Cache the sums so a changed IRF re-runs only the
    // O(pixels) rotation. The cached sums match the legacy fused / per-pixel
    // DecayPhasor accumulation bit-for-bit.
    ensure_phasor_cache(tttr_data, stack_frames, frequency);
    const size_t n_out = static_cast<size_t>(o_frames) * n_lines * n_pixel;
    auto *t = (float *) malloc(std::max(n_out, size_t(1)) * 2 * sizeof(float));
    for (size_t idx = 0; idx < n_out; idx++) {
        double gg = -1.0, ss = -1.0;
        const double cnt = _ph_cnt_cache_[idx];
        if (cnt > minimum_number_of_photons) {
            double g_exp = _ph_g_cache_[idx] / std::max(1., cnt);
            double s_exp = _ph_s_cache_[idx] / std::max(1., cnt);
            gg = DecayPhasor::g(g_irf, s_irf, g_exp, s_exp);
            ss = DecayPhasor::s(g_irf, s_irf, g_exp, s_exp);
        }
        t[idx * 2 + 0] = static_cast<float>(gg);
        t[idx * 2 + 1] = static_cast<float>(ss);
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

    const size_t n_out = o_frames * n_lines * n_pixel;

    // --- Per-pixel raw moments (m0 = count, m1 = integer sum of micro times).
    //     Independent of the IRF / background, so once cached an IRF/background
    //     change re-runs only the O(pixels) correction loop below. ---
    ensure_moment_cache(tttr_data, stack_frames);

    // --- IRF / background correction from the cached moments (cheap) ---
    // Replicates TTTR::compute_mean_lifetime exactly (dt<0 -> header resolution).
    double dt_eff = dt;
    if (dt_eff < 0.0) dt_eff = tttr_data->header->get_micro_time_resolution();
    auto *t = (double *) malloc(n_out * sizeof(double));

#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(is_verbose());
    bool use_openmp = (num_threads > 1);
#else
    bool use_openmp = false;
#endif
    #pragma omp parallel for schedule(static) if(use_openmp && n_out > (size_t(1) << 14))
    for (long idx = 0; idx < static_cast<long>(n_out); idx++) {
        const double m0 = _lt_m0_cache_[idx];
        const double m1 = _lt_m1_cache_[idx];
        double m0b = m0_bg, m1b = m1_bg;
        if (background_fraction > 0.0) {
            m1b = m1_bg * (m0 / m0_bg) * background_fraction;
            m0b = m0 * background_fraction;
        }
        double lt = 0.0;
        if (m0 > minimum_number_of_photons) {
            lt = (m1 - m1b) / (m0 - m0b) - m1_irf / m0_irf;
            lt *= dt_eff;
        }
        t[idx] = lt;
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
    // When the CLSM image is in lazy (bitmask) state, take the per-pixel
    // counts from the fused intensity instead of materializing index vectors
    std::vector<unsigned short> clsm_counts;
    size_t clsm_nl = 0, clsm_np = 0;
    if (clsm != nullptr) {
        if (!clsm->pixels_materialized_ && !clsm->stream_masks_.empty() &&
            !clsm->settings.use_pixel_markers) {
            unsigned short* buf = nullptr;
            int d1 = 0, d2 = 0, d3 = 0;
            clsm->get_intensity_from_masks(&buf, &d1, &d2, &d3);
            if (buf != nullptr) {
                clsm_counts.assign(buf, buf + static_cast<size_t>(d1) * d2 * d3);
                free(buf);
            }
            clsm_nl = static_cast<size_t>(d2);
            clsm_np = static_cast<size_t>(d3);
        } else {
            clsm->ensure_pixels_materialized();
        }
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
                        if (!clsm_counts.empty()) {
                            value = static_cast<double>(clsm_counts[
                                    static_cast<size_t>(f) * (clsm_nl * clsm_np) +
                                    static_cast<size_t>(l) * clsm_np + p]);
                        } else {
                            auto frame = clsm->frames[f];
                            auto line = frame->lines[l];
                            value = static_cast<double>(line->pixels[p].size());
                        }
                    } else if (images != nullptr) {
                        // Row stride is the number of PIXELS PER LINE, not the number
                        // of lines. With `l * nl` a non-square frame is read scrambled,
                        // and when nl > np the index runs past the frame -- for the last
                        // frame, past the allocation, which is where the NaNs came from.
                        // The two agree when nl == np, which is why square ROIs were fine
                        // and this survived so long.
                        value = images[f * (nl * np) + l * np + p];
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

    // Discard pairs that address frames outside the ROI. The correlation loop
    // below indexes roi[frame * pixel_in_roi] directly, so an out-of-range
    // frame number would read past the allocation. Dropping such pairs here
    // (rather than in the loop) also keeps the output length equal to the
    // number of maps actually written.
    frames_index_pairs.erase(
            std::remove_if(
                    frames_index_pairs.begin(), frames_index_pairs.end(),
                    [nf](const std::pair<int, int> &p) {
                        return p.first < 0 || p.first >= nf ||
                               p.second < 0 || p.second >= nf;
                    }),
            frames_index_pairs.end()
    );

    if (frames_index_pairs.empty()) {
        free(roi);
        *dim1 = 0; *dim2 = 0; *dim3 = 0;
        *output = (T *) calloc(1, sizeof(T));
        return;
    }

    // Allocate memory for the ICS output array
    auto out_tmp = (T *) calloc(frames_index_pairs.size() * pixel_in_roi, sizeof(T));

    // Allocate arrays for the transforms.
    //
    // `r2c` produces a HALF spectrum: `np/2 + 1` complex numbers per row, packed
    // compactly. Handing it strides for a full `nl x np` complex array made it write that
    // compact result into a buffer read as if it were full-width, so the spectrum was
    // simply misread -- the autocorrelation of a single delta came back as 1.0625 at the
    // peak with 0.0625 = 2/np repeated at every even column, instead of 1 and 0. A flat
    // field survived it (its spectrum is pure DC), which is how it went unnoticed, and the
    // RICS fits recover D straight through it because they fit a shape and rescale.
    //
    // The half spectrum is now given its own strides, and the inverse is `c2r`, which
    // consumes exactly that layout and produces the real correlation directly.
    const int nh = np / 2 + 1;                       // columns in the half spectrum
    const size_t n_half = (size_t) nl * nh;
    std::vector<CT> in(n_half, 0);
    std::vector<CT> fft_roi1(n_half, 0);
    std::vector<CT> fft_roi2(n_half, 0);
    std::vector<T> ics(pixel_in_roi, 0);

    std::ptrdiff_t sd = sizeof(T);
    std::ptrdiff_t sc = sizeof(CT);
    pocketfft::shape_t shape{(size_t) nl, (size_t) np};
    pocketfft::stride_t stride_d{sd * np, sd};
    pocketfft::stride_t stride_c{sc * nh, sc};        // half spectrum, compact rows
    pocketfft::shape_t axes{0, 1};
    T norm = T(1.0 / pixel_in_roi);

    // Iterate through the pair of frames
    int current_pair = 0;
    for (auto &frame_pair: frames_index_pairs) {
        //double roi1_int = 0.0;
        //double roi2_int = 0.0; // sum of values in roi1 & roi2

        // ROI1
        pocketfft::r2c<T>(shape, stride_d, stride_c, axes, pocketfft::FORWARD,
                          &roi[frame_pair.first * pixel_in_roi], fft_roi1.data(), 1.0);

        // ROI2
        if (frame_pair.second != frame_pair.first) {
            pocketfft::r2c<T>(shape, stride_d, stride_c, axes, pocketfft::FORWARD,
                              &roi[frame_pair.second * pixel_in_roi], fft_roi2.data(), 1.0);
        } else {
            fft_roi2 = fft_roi1;
        }

        // FFT(roi1) * conj(FFT(roi2)), over the half spectrum only
        for (size_t i = 0; i < n_half; i++) {
            in[i] = fft_roi1[i] * std::conj(fft_roi2[i]);
        }

        // Inverse: c2r consumes the half spectrum and yields the real correlation.
        pocketfft::c2r<T>(shape, stride_c, stride_d, axes, pocketfft::BACKWARD,
                          in.data(), ics.data(), norm);

        // write results to ics output
        int frame_offset = current_pair * pixel_in_roi;
        for (int i = 0; i < pixel_in_roi; i++) {
            out_tmp[frame_offset + i] = ics[i];
        }

        current_pair++;
    }
    free(roi);

    // Assign output. The first dimension is the number of correlated frame
    // PAIRS, which is what was allocated and written above. Reporting the
    // number of input frames instead over-declares the array whenever fewer
    // pairs than frames were correlated (any frame lag > 0), so callers read
    // past the allocation.
    *dim1 = static_cast<int>(frames_index_pairs.size());
    *dim2 = static_cast<int>(nl);
    *dim3 = static_cast<int>(np);
    *output = out_tmp;
    return;
}


void CLSMImage::transform(unsigned int *input, int n_input) {
    // Pixel contents are rearranged: work on materialized pixels, masks stale
    materialize_pixel_handles();
    drop_stream_masks();
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



void CLSMImage::reshape(int new_n_frames, int new_n_lines, int new_n_pixel) {
    // Geometry changes: materialize with the old layout, masks become stale
    materialize_pixel_handles();
    drop_stream_masks();
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
    // Geometry changes: materialize with the old layout, masks become stale
    materialize_pixel_handles();
    drop_stream_masks();
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

    // Stream acceptance masks (lazy fill representation)
    total += stream_masks_.capacity() * sizeof(std::vector<uint64_t>);
    for (const auto& m: stream_masks_) {
        total += m.capacity() * sizeof(uint64_t);
    }

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

    // Stream acceptance masks: the photon-index representation of a lazy fill
    if (indices) {
        for (const auto& m: stream_masks_) {
            *indices += m.capacity() * sizeof(uint64_t);
        }
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

void CLSMImage::rebuild_pixel_duration_cumsum(){
    pixel_duration_cumsum.clear();
    pixel_duration_cumsum.resize(pixel_duration_matrix.size());
    for(size_t l = 0; l < pixel_duration_matrix.size(); l++){
        const auto& durations = pixel_duration_matrix[l];
        auto& cumsum = pixel_duration_cumsum[l];
        cumsum.resize(durations.size());
        double sum = 0.0;
        for(size_t i = 0; i < durations.size(); i++){
            sum += durations[i];
            cumsum[i] = sum;
        }
    }
}

void CLSMImage::set_pixel_duration_matrix(const std::vector<std::vector<double>>& durations){
    // A deferred fill must be binned with the durations that were active at
    // fill time, so materialize before the cumulative sums change
    ensure_pixels_materialized();
    pixel_duration_matrix = durations;
    rebuild_pixel_duration_cumsum();
}

void CLSMImage::set_pixel_duration_matrix(double* data, int nx, int ny){
    // See the vector overload: deferred fills bin with the fill-time durations
    ensure_pixels_materialized();
    pixel_duration_matrix.clear();
    if(data != nullptr && nx > 0 && ny > 0){
        pixel_duration_matrix.resize(static_cast<size_t>(nx));
        for(int l = 0; l < nx; l++){
            const double* row = data + static_cast<size_t>(l) * static_cast<size_t>(ny);
            pixel_duration_matrix[static_cast<size_t>(l)].assign(row, row + ny);
        }
    }
    rebuild_pixel_duration_cumsum();
}

void CLSMImage::get_pixel_duration_matrix(double** data, int* nx, int* ny) const{
    *data = nullptr;
    *nx = 0;
    *ny = 0;
    size_t rows = pixel_duration_matrix.size();
    size_t cols = 0;
    for(const auto& line: pixel_duration_matrix) cols = std::max(cols, line.size());
    if(rows == 0 || cols == 0) return;
    auto* out = static_cast<double*>(malloc(rows * cols * sizeof(double)));
    if(out == nullptr) return;
    for(size_t l = 0; l < rows; l++){
        size_t n = std::min(cols, pixel_duration_matrix[l].size());
        for(size_t p = 0; p < n; p++) out[l * cols + p] = pixel_duration_matrix[l][p];
        for(size_t p = n; p < cols; p++) out[l * cols + p] = 0.0;
    }
    *data = out;
    *nx = static_cast<int>(rows);
    *ny = static_cast<int>(cols);
}

const std::vector<std::vector<double>>& CLSMImage::get_pixel_duration_matrix() const{
    return pixel_duration_matrix;
}

bool CLSMImage::has_non_uniform_durations() const{
    return !pixel_duration_matrix.empty();
}

std::vector<double> CLSMImage::get_cumulative_durations(int frame_idx, int line_idx) const{
    if(frame_idx < 0 || line_idx < 0) return {};
    const std::vector<double>* c = line_cumsum(static_cast<size_t>(line_idx));
    return c ? *c : std::vector<double>{};
}
