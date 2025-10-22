#include "TTTRSelection.h"

#include <algorithm>

void TTTRSelection::set_selection_flags(uint8_t flags){
    SelectionMask mask;
    mask.value = flags;
    // Apply dense/inverted via setters to keep internal invariants
    set_dense(mask.bits.dense != 0U);
    set_inverted(mask.bits.inverted != 0U);
    // Preserve and apply additional bits without disturbing range/index state
    _selection_mask.bits.channel_flip   = mask.bits.channel_flip;
    _selection_mask.bits.is_z_stack     = mask.bits.is_z_stack;
    _selection_mask.bits.is_time_series = mask.bits.is_time_series;
    // Keep any remaining reserved bits zeroed for now
    _selection_mask.bits.reserved = 0U;
}

void TTTRSelection::set_dense(bool dense){
    bool was_dense = is_dense();
    if(dense){
        _selection_mask.bits.dense = 1U;
        return;
    }

    _selection_mask.bits.dense = 0U;
    if(was_dense){
        auto start = get_start();
        auto stop = get_stop();
        TTTRRange::clear();  // Use base class method
        if(start >= 0){
            TTTRRange::insert(start);
        }
        if(stop >= 0 && stop != start){
            TTTRRange::insert(stop);
        }
    }
}

void TTTRSelection::set_range_start(int start){
    if(!is_dense()){
        // In sparse mode, we need to maintain start/stop as explicit indices
        // Clear all existing indices and rebuild with new start and current stop
        int old_stop = get_stop();
        TTTRRange::clear();
        if(start >= 0){
            TTTRRange::insert(start);
        }
        if(old_stop >= 0 && old_stop != start){
            TTTRRange::insert(old_stop);
        }
    }
    // In dense mode, start is implicit (first index in set)
}

void TTTRSelection::set_range_stop(int stop){
    if(!is_dense()){
        // In sparse mode, we need to maintain start/stop as explicit indices
        // Clear all existing indices and rebuild with current start and new stop
        int old_start = get_start();
        TTTRRange::clear();
        if(old_start >= 0){
            TTTRRange::insert(old_start);
        }
        if(stop >= 0 && stop != old_start){
            TTTRRange::insert(stop);
        }
    }
    // In dense mode, stop is implicit (last index in set)
}

void TTTRSelection::set_range(int start, int stop){
    if(start >= 0 && stop >= 0 && stop < start){
        std::swap(start, stop);
    }
    set_range_start(start);
    set_range_stop(stop);
}

void TTTRSelection::insert(int idx){
    if(!is_dense()){
        set_dense(true);
    }
    TTTRRange::insert(idx);
    // Range is automatically maintained by TTTRRange (first/last elements)
}

void TTTRSelection::clear(){
    TTTRRange::clear();
    // Range is automatically cleared by TTTRRange
}

int TTTRSelection::resolve_start(const std::vector<int>& values) const{
    int start = get_start();
    if(start >= 0){
        return start;
    }
    if(!values.empty()){
        return values.front();
    }
    return -1;
}

int TTTRSelection::resolve_stop(const std::vector<int>& values) const{
    int stop = get_stop();
    if(stop >= 0){
        return stop;
    }
    if(!values.empty()){
        return values.back();
    }
    return -1;
}

std::vector<int> TTTRSelection::get_tttr_indices() const{
    // Use const ref to avoid unnecessary copy
    const auto& stored = TTTRRange::get_tttr_indices();
    int start = resolve_start(stored);
    int stop = resolve_stop(stored);

    auto build_complement = [](const std::vector<int>& values, int range_start, int range_stop){
        std::vector<int> result;
        if(range_start < 0 || range_stop < range_start){
            return result;
        }
        result.reserve(static_cast<size_t>(range_stop - range_start + 1));
        auto it = values.begin();
        auto end = values.end();
        for(int idx = range_start; idx <= range_stop; ++idx){
            while(it != end && *it < idx){
                ++it;
            }
            if(it != end && *it == idx){
                continue;
            }
            result.push_back(idx);
        }
        return result;
    };

    if(start < 0 || stop < start){
        return {};
    }

    if(is_dense()){
        if(!is_inverted()){
            return stored;
        }
        return build_complement(stored, start, stop);
    }

    if(is_inverted()){
        return {};
    }

    std::vector<int> range(static_cast<size_t>(stop - start + 1));
    for(int idx = start; idx <= stop; ++idx){
        range[static_cast<size_t>(idx - start)] = idx;
    }
    return range;
}

size_t TTTRSelection::get_index_count() const{
    // Get size directly from flat_set
    size_t stored_size = _tttr_indices ? _tttr_indices->size() : 0;
    
    // Get start/stop from base class
    int start = get_start();
    int stop = get_stop();
    
    if(start < 0 || stop < start){
        return 0U;
    }

    size_t span = static_cast<size_t>(stop - start + 1);
    if(is_dense()){
        if(!is_inverted()){
            return stored_size;
        }
        if(span < stored_size){
            return 0U;
        }
        return span - stored_size;
    }

    return is_inverted() ? 0U : span;
}

void TTTRSelection::get_tttr_indices(int** output, int* n_output) const{
    // For dense, non-inverted selections, we can use the base class implementation
    // which allocates a copy using malloc() that Python can safely free()
    if(is_dense() && !is_inverted()){
        TTTRRange::get_tttr_indices(output, n_output);
        return;
    }
    
    // For inverted or sparse selections, we need to build the result
    // Fall back to creating a temporary vector (less efficient but correct)
    // Note: This will allocate memory that Python must manage
    auto indices = get_tttr_indices();  // Get the vector
    
    // Allocate new memory for the output using malloc() because Python will call free()
    // Python/NumPy will take ownership via ARGOUTVIEWM_ARRAY1
    *n_output = static_cast<int>(indices.size());
    if(*n_output > 0){
        *output = static_cast<int*>(std::malloc(*n_output * sizeof(int)));
        if(*output){
            std::memcpy(*output, indices.data(), *n_output * sizeof(int));
        }
    } else {
        // Allocate dummy array for empty case using malloc() (Python will call free())
        *output = static_cast<int*>(std::malloc(sizeof(int)));
        if(*output) (*output)[0] = 0;
    }
}
