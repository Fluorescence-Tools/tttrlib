// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_STREAMING_CLSM_IMAGE_H
#define TTTRLIB_STREAMING_CLSM_IMAGE_H

// Validation: EQUIVALENCE-TESTED 2026-08-17 -- vs the batch CLSMImage frame by frame on a real HT3 scan, integrating and
//   live modes, mid-stream switches, chunked delivery
//   (test/python/streaming/test_streaming_clsm_image.py).
//   Register: okf/testing/algorithm-validation.md

#include "TTTR.h"
#include "TTTRHeader.h"
#include "CLSMImage.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <memory>
#include <stdexcept>
#include <vector>

namespace tttrlib {

// StreamingCLSMImage — scanned-image reconstruction from a live photon stream.
//
// A scan produces frames; a viewer wants either the frame that is arriving now
// or everything that has arrived so far. Those are the two modes:
//
//   LIVE         the image is the frame being scanned right now, filling in as
//                the beam sweeps. What the microscope is looking at.
//   INTEGRATING  the image is the sum over every frame completed since the last
//                clear(). What the batch CLSMImage produces for the same
//                events.
//
// `set_mode` switches between them **at any point, including mid-acquisition,
// while events are still arriving**. It discards nothing and costs nothing: the
// running sum accumulates in LIVE mode too, so switching to INTEGRATING shows
// the whole acquisition rather than starting a new one, and switching back to
// LIVE resumes on the frame currently being scanned. Feeding continues across
// the switch — the mode is a view over state that is maintained either way, not
// a processing pipeline that has to be torn down and rebuilt.
//
// The three views are individually reachable regardless of mode:
// `get_current_frame()` (partial, being scanned), `get_last_frame()` (the last
// one completed) and `get_integrated()` (the sum).
//
// # Why this delegates to CLSMImage per frame
//
// Where a frame begins is not a one-line predicate. It depends on the reading
// routine (the marker lives in the routing channel for SP5, in the micro time
// for SP8, in the event type plus routing channel by default), on a walk-back
// over markers sharing one macro-time tick, and on an instrument-specific
// correction for the first BH SPC frame. That logic exists, is tested, and is
// what every other consumer of this library agrees with. A second copy of it
// here would start out agreeing and end up not.
//
// So this class buffers exactly one frame of events and hands that buffer to
// the ordinary CLSMImage constructor. Memory is one frame rather than the whole
// acquisition — which is the part that actually has to change for a stream —
// and the pixel assignment is the batch's by construction, not by resemblance.
//
// # Usage
//
//   StreamingCLSMImage img(prototype, settings, StreamingCLSMImage::LIVE);
//   for each chunk: img.push_events(mt, micro, rc, et, n);
//   img.flush();                       // close a frame still open
//   img.get_intensity(&data, &n_lines, &n_pixel);
//
// `prototype` is a TTTR carrying the acquisition's header. The header is what
// tells CLSMImage the scan geometry and marker layout, so a live consumer needs
// the same one the file reader would have used.
//
class StreamingCLSMImage {
public:
    enum Mode { LIVE = 0, INTEGRATING = 1 };

    StreamingCLSMImage(
        std::shared_ptr<TTTR> prototype,
        CLSMSettings settings = CLSMSettings(),
        int mode = LIVE,
        std::vector<int> channels = std::vector<int>()
    ) : prototype_(std::move(prototype)),
        settings_(std::move(settings)),
        mode_(mode == INTEGRATING ? INTEGRATING : LIVE),
        channels_(std::move(channels)) {
        if (prototype_ == nullptr)
            throw std::invalid_argument(
                "StreamingCLSMImage: a prototype TTTR is required — its header "
                "carries the scan geometry and marker layout that decide where "
                "a frame begins.");
        // A frame is delimited by the marker that opens it and the marker that
        // opens the next one, so the buffer always starts on a frame marker and
        // the leading partial frame is never a frame.
        settings_.skip_before_first_frame_marker = true;
    }

    // --- feeding ----------------------------------------------------------

    void push_event(uint64_t macro_time, uint16_t micro_time,
                    int8_t routing_channel, int8_t event_type) {
        if (is_frame_marker(micro_time, routing_channel, event_type)) {
            if (have_open_frame_) close_frame();
            have_open_frame_ = true;
        }
        if (!have_open_frame_) return;     // events before the first frame marker
        buf_mt_.push_back(macro_time);
        buf_micro_.push_back(micro_time);
        buf_rc_.push_back(routing_channel);
        buf_et_.push_back(event_type);
    }

    void push_events(const uint64_t* macro_times, const uint16_t* micro_times,
                     const int8_t* routing_channels, const int8_t* event_types,
                     int n) {
        for (int i = 0; i < n; ++i)
            push_event(macro_times[i], micro_times[i],
                       routing_channels[i], event_types[i]);
    }

    /// Feed every event of a TTTR chunk, as an acquisition would deliver it.
    void push_tttr(std::shared_ptr<TTTR> chunk) {
        if (chunk == nullptr) return;
        const int n = static_cast<int>(chunk->get_n_valid_events());
        for (int i = 0; i < n; ++i)
            push_event(chunk->get_macro_time_at(i), chunk->get_micro_time_at(i),
                       chunk->get_routing_channel_at(i), chunk->get_event_type_at(i));
    }

    /// Close the frame still being filled. The last frame of an acquisition has
    /// no following frame marker, so without this it is never reconstructed.
    void flush() {
        if (have_open_frame_) close_frame();
        have_open_frame_ = false;
    }

    // --- mode -------------------------------------------------------------

    int get_mode() const { return mode_; }
    void set_mode(int mode) { mode_ = (mode == INTEGRATING) ? INTEGRATING : LIVE; }

    /// Drop the accumulated sum, the current frame, and the frame count.
    void clear() {
        sum_.clear();
        last_.clear();
        partial_.clear();
        partial_rows_ = partial_cols_ = 0;
        partial_valid_for_ = static_cast<size_t>(-1);
        geometry_fixed_ = false;
        buf_mt_.clear(); buf_micro_.clear(); buf_rc_.clear(); buf_et_.clear();
        have_open_frame_ = false;
        frames_completed_ = 0;
        n_lines_ = 0;
        n_pixel_ = 0;
    }

    // --- results ----------------------------------------------------------

    int frames_completed() const { return frames_completed_; }
    int n_lines() const { return n_lines_; }
    int n_pixel() const { return n_pixel_; }
    /// Events buffered for the frame currently being scanned.
    int events_in_current_frame() const { return static_cast<int>(buf_mt_.size()); }

    /// The image for the current mode: the frame being scanned (LIVE) or the
    /// sum over completed frames (INTEGRATING). Shape (n_lines, n_pixel).
    void get_intensity(unsigned int** output, int* dim1, int* dim2) {
        const std::vector<unsigned int>& src =
            (mode_ == INTEGRATING) ? sum_ : current_frame();
        emit(src, output, dim1, dim2);
    }

    /// The frame currently being scanned, partially filled. This is what a live
    /// viewer draws between frame markers; it falls back to the last completed
    /// frame when no events have arrived for a new one yet.
    void get_current_frame(unsigned int** output, int* dim1, int* dim2) {
        emit(current_frame(), output, dim1, dim2);
    }

    /// The most recently completed frame, whatever the mode.
    void get_last_frame(unsigned int** output, int* dim1, int* dim2) {
        emit(last_, output, dim1, dim2);
    }

    /// The sum over completed frames, whatever the mode.
    void get_integrated(unsigned int** output, int* dim1, int* dim2) {
        emit(sum_, output, dim1, dim2);
    }

private:
    std::shared_ptr<TTTR> prototype_;
    CLSMSettings settings_;
    int mode_;
    std::vector<int> channels_;

    std::vector<unsigned long long> buf_mt_;
    std::vector<unsigned short> buf_micro_;
    std::vector<signed char> buf_rc_;
    std::vector<signed char> buf_et_;
    bool have_open_frame_ = false;

    std::vector<unsigned int> last_;
    std::vector<unsigned int> sum_;
    std::vector<unsigned int> partial_;
    int partial_rows_ = 0, partial_cols_ = 0;
    size_t partial_valid_for_ = static_cast<size_t>(-1);
    int frames_completed_ = 0;
    int n_lines_ = 0;
    int n_pixel_ = 0;
    bool geometry_fixed_ = false;

    /// Reconstruct the frame currently being scanned. Cached on the buffer
    /// size, so redrawing the same partial frame is free: a viewer polls at
    /// display rate, which is thousands of times slower than events arrive, and
    /// reconstructing per query rather than per photon is what lets this reuse
    /// the batch reconstruction instead of copying its pixel assignment.
    ///
    /// Before the first frame has closed there is no settled geometry, and the
    /// frame being scanned supplies its own — a half-scanned frame has as many
    /// lines as have been scanned. Once a frame has completed, the settled
    /// geometry is imposed instead, so the partial frame is the full canvas
    /// with the unscanned part still zero. Letting an incomplete frame settle
    /// the geometry is what made the first live redraw pin the image at 26
    /// lines and every later frame mismatch it.
    const std::vector<unsigned int>& current_frame() {
        if (buf_mt_.empty()) return last_;
        if (partial_valid_for_ == buf_mt_.size()) return partial_;
        std::vector<unsigned int> img;
        int rows = 0, cols = 0;
        if (reconstruct(img, rows, cols)) {
            if (geometry_fixed_ && cols == n_pixel_ && rows != n_lines_) {
                // An incomplete frame reconstructs with only the lines that have
                // been scanned -- the reconstruction reports as much and settles
                // n_lines on what it found. Drawn on its own that is a picture
                // that changes shape on every redraw, so it goes into the
                // settled canvas at the top, where the beam actually put it,
                // and the rest stays black until it is scanned.
                partial_.assign(static_cast<size_t>(n_lines_) * n_pixel_, 0u);
                const int copy_rows = std::min(rows, n_lines_);
                std::copy(img.begin(),
                          img.begin() + static_cast<size_t>(copy_rows) * cols,
                          partial_.begin());
                partial_rows_ = n_lines_;
                partial_cols_ = n_pixel_;
            } else {
                partial_ = std::move(img);
                partial_rows_ = rows;
                partial_cols_ = cols;
            }
        } else {
            partial_.clear();
            partial_rows_ = partial_cols_ = 0;
        }
        partial_valid_for_ = buf_mt_.size();
        return partial_;
    }

    void emit(const std::vector<unsigned int>& src,
              unsigned int** output, int* dim1, int* dim2) const {
        int rows = n_lines_, cols = n_pixel_;
        if (&src == &partial_ && !geometry_fixed_) {
            rows = partial_rows_;
            cols = partial_cols_;
        }
        *dim1 = rows;
        *dim2 = cols;
        const size_t n = static_cast<size_t>(rows) * cols;
        auto* out = static_cast<unsigned int*>(
            std::malloc(std::max<size_t>(n, 1) * sizeof(unsigned int)));
        if (out == nullptr) throw std::bad_alloc();
        if (src.size() == n && n > 0) std::copy(src.begin(), src.end(), out);
        else std::fill(out, out + std::max<size_t>(n, 1), 0u);
        *output = out;
    }

    /// The same test the batch frame scan applies, per reading routine. It is
    /// only used to decide where to cut the buffer; the reconstruction itself
    /// re-derives the edges from the events handed to it.
    bool is_frame_marker(uint16_t micro_time, int8_t routing_channel,
                         int8_t event_type) const {
        const auto& fr = settings_.marker_frame_start;
        if (fr.empty()) return false;
        auto contains = [&fr](int v) {
            return std::find(fr.begin(), fr.end(), v) != fr.end();
        };
        switch (settings_.reading_routine) {
            case CLSM_SP8:
                return routing_channel == settings_.marker_event_type &&
                       contains(static_cast<int>(micro_time));
            case CLSM_SP5:
                return contains(static_cast<int>(routing_channel));
            default:
                return event_type == settings_.marker_event_type &&
                       contains(static_cast<int>(routing_channel));
        }
    }

    /// Run the ordinary reconstruction over the buffered events. The buffer
    /// starts on a frame marker, so this is exactly one frame -- complete when
    /// called from close_frame(), partial when called from current_frame().
    bool reconstruct(std::vector<unsigned int>& out, int& rows, int& cols) {
        if (buf_mt_.empty()) return false;
        auto frame_tttr = std::make_shared<TTTR>();
        frame_tttr->set_header(prototype_->get_header());
        frame_tttr->append_events(
            buf_mt_.data(), static_cast<int>(buf_mt_.size()),
            buf_micro_.data(), static_cast<int>(buf_micro_.size()),
            buf_rc_.data(), static_cast<int>(buf_rc_.size()),
            buf_et_.data(), static_cast<int>(buf_et_.size()),
            /* shift_macro_time */ false);

        CLSMImage frame(frame_tttr, settings_, nullptr, true, channels_);
        unsigned int* data = nullptr;
        int d1 = 0, d2 = 0, d3 = 0;
        frame.get_intensity_u32(&data, &d1, &d2, &d3);
        if (data == nullptr) return false;
        if (d1 < 1) { std::free(data); return false; }

        rows = d2; cols = d3;
        const size_t n = static_cast<size_t>(d2) * d3;
        out.assign(data, data + n);
        std::free(data);
        return true;
    }

    void close_frame() {
        std::vector<unsigned int> img;
        int rows = 0, cols = 0;
        const bool ok = reconstruct(img, rows, cols);
        // Geometry is settled by the first *completed* frame and then held: a
        // viewer cannot accumulate frames whose shape changes, and the
        // auto-detection that would let it change runs per construction. It is
        // deliberately not settled from a partial frame, which has only as many
        // lines as have been scanned so far.
        if (ok && !geometry_fixed_) {
            n_lines_ = rows;
            n_pixel_ = cols;
            settings_.n_lines = rows;
            settings_.n_pixel_per_line = cols;
            geometry_fixed_ = true;
            sum_.assign(static_cast<size_t>(n_lines_) * n_pixel_, 0u);
        }
        if (ok && rows == n_lines_ && cols == n_pixel_) {
            last_ = std::move(img);
            const size_t n = static_cast<size_t>(n_lines_) * n_pixel_;
            for (size_t i = 0; i < n; ++i) sum_[i] += last_[i];
            frames_completed_++;
        }
        buf_mt_.clear(); buf_micro_.clear(); buf_rc_.clear(); buf_et_.clear();
        partial_.clear();
        partial_valid_for_ = static_cast<size_t>(-1);
    }
};

} // namespace tttrlib

#endif // TTTRLIB_STREAMING_CLSM_IMAGE_H
