%{
#include "BurstFilter.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
%}

// Use shared_ptr for BurstFilter to pass it around
%shared_ptr(tttrlib::BurstFilter)

%apply (int64_t* IN_ARRAY1, int DIM1) {(int64_t *start_indices, int n_start_indices)}
%apply (int64_t* IN_ARRAY1, int DIM1) {(int64_t *stop_indices, int n_stop_indices)}

%extend tttrlib::BurstFilter {
    std::string to_json_string() const {
        return $self->to_json().dump();
    }

    void from_json_string(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }

    %pythoncode %{
    
    def __len__(self):
        return self.get_burst_count()
        
    def __getitem__(self, index):
        if index < 0 or index >= self.get_burst_count():
            raise IndexError("Burst index out of range")
        return self.get_burst_properties(index)
        
    def __iter__(self):
        for i in range(self.get_burst_count()):
            yield self.get_burst_properties(i)
    
    %}
}

%include "BurstFilter.h"
