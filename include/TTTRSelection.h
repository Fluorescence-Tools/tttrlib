#ifndef TTTRLIB_TTTRSELECTION_H
#define TTTRLIB_TTTRSELECTION_H

#include <cstdint>
#include <memory>       /* shared_ptr */
#include <utility>
#include <vector>

#include "TTTRRange.h"

class TTTRSelection : public TTTRRange{

protected:

    struct SelectionBits{
        unsigned char dense : 1;         // 0x01
        unsigned char inverted : 1;      // 0x02
        unsigned char channel_flip : 1;  // 0x04 - when set on a frame, indicates a channel change at this selection
        unsigned char is_z_stack : 1;    // 0x08 - frame belongs to a Z-stack acquisition
        unsigned char is_time_series : 1;// 0x10 - frame belongs to a time-series acquisition
        unsigned char reserved : 3;      // remaining reserved bits
    };

    union SelectionMask{
        uint8_t value;
        SelectionBits bits;

        SelectionMask() : value(0U){}
    };

    SelectionMask _selection_mask{};
    // Removed _range_start and _range_stop - use get_start()/get_stop() from TTTRRange
    // This saves 8 bytes per pixel (26.4 MB for 3.3M pixels)
    // Removed _tttr - moved to CLSMLine (only lines need it, not pixels)
    // This saves 16 bytes per pixel (52.8 MB for 3.3M pixels)

public:

    enum SelectionFlags : uint8_t {
        SelectionDense        = 0x01,
        SelectionInverted     = 0x02,
        SelectionChannelFlip  = 0x04,
        SelectionFrameZStack  = 0x08,
        SelectionFrameTime    = 0x10
    };

    static_assert(sizeof(SelectionMask) == sizeof(uint8_t), "SelectionMask must remain a single byte");

    uint8_t get_selection_flags() const{
        return _selection_mask.value;
    }

    void set_selection_flags(uint8_t flags);

    bool is_dense() const{
        return _selection_mask.bits.dense != 0U;
    }

    bool is_sparse() const{
        return !is_dense();
    }

    void set_dense(bool dense);

    bool is_inverted() const{
        return _selection_mask.bits.inverted != 0U;
    }

    void set_inverted(bool inverted){
        _selection_mask.bits.inverted = inverted ? 1U : 0U;
    }

    // Channel flip flag indicates a change in channel sequence at this selection (usually on frames)
    bool has_channel_flip() const{
        return _selection_mask.bits.channel_flip != 0U;
    }

    void set_channel_flip(bool flip){
        _selection_mask.bits.channel_flip = flip ? 1U : 0U;
    }

    // Frame-type markers (optional metadata)
    bool is_z_stack_frame() const{
        return _selection_mask.bits.is_z_stack != 0U;
    }

    void set_z_stack_frame(bool v){
        _selection_mask.bits.is_z_stack = v ? 1U : 0U;
    }

    bool is_time_series_frame() const{
        return _selection_mask.bits.is_time_series != 0U;
    }

    void set_time_series_frame(bool v){
        _selection_mask.bits.is_time_series = v ? 1U : 0U;
    }

    void set_range_start(int start);

    void set_range_stop(int stop);

    void set_range(int start, int stop);

    int get_range_start() const{
        return get_start();  // Use TTTRRange::get_start()
    }

    int get_range_stop() const{
        return get_stop();  // Use TTTRRange::get_stop()
    }

    void insert(int idx);

    void clear();

    size_t get_index_count() const;

    std::vector<int> get_tttr_indices() const override;

    /**
     * @brief Gets TTTR indices as a raw pointer and size (optimized for Python/NumPy).
     * 
     * This zero-copy accessor provides direct access to the internal data.
     * For TTTRSelection, this only works efficiently for dense, non-inverted selections.
     * For inverted or sparse selections, use the vector-returning version.
     * The pointer remains valid as long as the TTTRSelection object is not modified.
     *
     * @param output Pointer to receive the data pointer (const int*)
     * @param n_output Pointer to receive the number of elements
     */
    void get_tttr_indices(int** output, int* n_output) const override;

private:
    int resolve_start(const std::vector<int>& values) const;
    int resolve_stop(const std::vector<int>& values) const;

public:
    TTTRSelection() = default;
    
    TTTRSelection(int start, int stop){
        set_range(start, stop);
    }

    /// Copy constructor
    TTTRSelection(const TTTRSelection& p2) : TTTRRange(p2){
        _selection_mask.value = p2._selection_mask.value;
    }

};


#endif //TTTRLIB_TTTRSELECTION_H
