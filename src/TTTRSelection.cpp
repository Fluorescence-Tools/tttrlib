#include "TTTRSelection.h"

#include <algorithm>

void TTTRSelection::set_selection_flags(uint8_t flags){
    SelectionMask mask;
    mask.value = flags;
    set_dense(mask.bits.dense != 0U);
    set_inverted(mask.bits.inverted != 0U);
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
        auto start = _range_start;
        auto stop = _range_stop;
        _tttr_indices.clear();
        if(start >= 0){
            _tttr_indices.insert(start);
        }
        if(stop >= 0 && stop != start){
            _tttr_indices.insert(stop);
        }
    }
}

void TTTRSelection::set_range_start(int start){
    if(!is_dense() && _range_start >= 0){
        _tttr_indices.erase(_range_start);
    }
    _range_start = start;
    if(!is_dense() && start >= 0){
        _tttr_indices.insert(start);
    }
}

void TTTRSelection::set_range_stop(int stop){
    if(!is_dense() && _range_stop >= 0){
        _tttr_indices.erase(_range_stop);
    }
    _range_stop = stop;
    if(!is_dense() && stop >= 0){
        _tttr_indices.insert(stop);
    }
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
    if(_range_start < 0 || idx < _range_start){
        _range_start = idx;
    }
    if(_range_stop < 0 || idx > _range_stop){
        _range_stop = idx;
    }
}

void TTTRSelection::clear(){
    TTTRRange::clear();
    _range_start = -1;
    _range_stop = -1;
}

int TTTRSelection::resolve_start(const std::vector<int>& values) const{
    if(_range_start >= 0){
        return _range_start;
    }
    if(!values.empty()){
        return values.front();
    }
    return -1;
}

int TTTRSelection::resolve_stop(const std::vector<int>& values) const{
    if(_range_stop >= 0){
        return _range_stop;
    }
    if(!values.empty()){
        return values.back();
    }
    return -1;
}

std::vector<int> TTTRSelection::get_tttr_indices() const{
    auto stored = TTTRRange::get_tttr_indices();
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
    auto stored = TTTRRange::get_tttr_indices();
    int start = resolve_start(stored);
    int stop = resolve_stop(stored);
    if(start < 0 || stop < start){
        return 0U;
    }

    size_t span = static_cast<size_t>(stop - start + 1);
    if(is_dense()){
        if(!is_inverted()){
            return stored.size();
        }
        if(span < stored.size()){
            return 0U;
        }
        return span - stored.size();
    }

    return is_inverted() ? 0U : span;
}
