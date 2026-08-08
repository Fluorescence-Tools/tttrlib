// SPDX-License-Identifier: BSD-3-Clause
%{
#include "BurstFilter.h"
#include "Channel.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
%}

// Use shared_ptr for BurstFilter to pass it around
%shared_ptr(tttrlib::BurstFilter)

// Type mapping for int64_t parameters (Python-only: uses the Python C-API).
// R and Java rely on SWIG's built-in int64_t handling instead.
#ifdef SWIGPYTHON
%typemap(in) int64_t {
    $1 = (int64_t)PyLong_AsLongLong($input);
}
#endif

%extend tttrlib::BurstFilter {
    std::string to_json_string() const {
        return $self->to_json().dump();
    }

    void from_json_string(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }

#ifdef SWIGPYTHON
    %pythoncode %{
    
    @property
    def json(self) -> str:
        """JSON string representation of the BurstFilter parameters/state."""
        return self.to_json_string()

    def to_dict(self):
        """Parse JSON string to Python dict."""
        import json as _json
        return _json.loads(self.to_json_string())

    def from_dict(self, d):
        """Load parameters from Python dict."""
        import json as _json
        self.from_json_string(_json.dumps(d))

    def save_parameters(self, path):
        """Save parameters to a JSON file."""
        with open(path, 'w', encoding='utf-8') as f:
            f.write(self.to_json_string())
        return self

    def load_parameters(self, path):
        """Load parameters from a JSON file."""
        with open(path, 'r', encoding='utf-8') as f:
            self.from_json_string(f.read())
        return self

    def __len__(self):
        """Return number of bursts."""
        return self.get_burst_count()
        
    def __getitem__(self, index):
        """Get burst properties by index."""
        if index < 0 or index >= len(self):
            raise IndexError("Burst index out of range")
        return self.get_burst_properties(index)
        
    def __iter__(self):
        """Iterate over burst properties."""
        for i in range(len(self)):
            yield self.get_burst_properties(i)

    @property
    def burst_properties(self):
        """Get all burst properties as a structured NumPy array."""
        import numpy as np
        props = self.get_all_burst_properties()
        if len(props) == 0:  # ndarray: no truthiness
            return np.empty(0, dtype=[
                ('start', np.int64), ('stop', np.int64), ('size', np.int64),
                ('duration', np.float64), ('count_rate', np.float64)
            ])
        
        a = np.asarray(props)
        start = a[:, 0].astype(np.int64)
        stop = a[:, 1].astype(np.int64)
        size = a[:, 2].astype(np.int64)
        duration = a[:, 3].astype(np.float64)
        count_rate = a[:, 4].astype(np.float64)
        
        out = np.empty(start.shape[0], dtype=[
            ('start', np.int64), ('stop', np.int64), ('size', np.int64),
            ('duration', np.float64), ('count_rate', np.float64)
        ])
        out['start'] = start
        out['stop'] = stop
        out['size'] = size
        out['duration'] = duration
        out['count_rate'] = count_rate
        return out

    
    def add_channel_from_dict(self, name, components):
        """Add a channel from a dictionary specification
        
        Args:
            name: Channel name (e.g., 'redParallel')
            components: List of dicts with keys: routing_channel, microtime_range
                       Example: [{'routing_channel': 1, 'microtime_range': (0, 2048)}]
        """
        import tttrlib
        channel = tttrlib.Channel(name)
        for comp in components:
            rout = comp.get('routing_channel', 0)
            mt_range = comp.get('microtime_range', (0, 65535))
            if isinstance(mt_range, (list, tuple)) and len(mt_range) == 2:
                mt_start, mt_stop = mt_range
            else:
                mt_start, mt_stop = 0, 65535
            channel.add_component(rout, mt_start, mt_stop)
        self.add_channel(channel)
    
    def load_channels_from_json_dict(self, channels_dict):
        """Load channels from a list of dictionaries
        
        Args:
            channels_dict: List of channel dicts, each with 'name' and 'components' keys
        """
        import json
        json_str = json.dumps(channels_dict)
        self.load_channels_from_json(json_str)
    
    def reset_to_raw_bursts(self):
        """Reset to raw (unfiltered) bursts, undoing all filtering."""
        self._reset_to_raw_bursts()
    
    def reapply_filters(self):
        """Reapply all filters with current parameters to raw bursts."""
        self._reapply_filters()
    
    def clear_filters(self):
        """Clear all applied filters and reset to raw bursts."""
        self._clear_filters()
    
    @property
    def has_raw_bursts(self):
        """Check if raw bursts are available."""
        return len(self.get_bursts()) > 0  # Check if any bursts exist
    
    def get_burst_channel_photons_dict(self, burst_index):
        """Get photon counts for each channel in a specific burst
        
        Args:
            burst_index: Index of the burst
            
        Returns:
            Dict mapping channel names to photon counts
        """
        if burst_index < 0 or burst_index >= len(self):
            raise IndexError("Burst index out of range")
        
        # Get burst start/stop from interleaved array
        bursts = self.get_bursts()
        if len(bursts) == 0:
            return {}
        start_idx = int(bursts[burst_index][0])
        stop_idx = int(bursts[burst_index][1])
        
        # Get the C++ map and convert to Python dict
        cpp_map = self.get_burst_channel_photons(start_idx, stop_idx)
        result = {}
        for key in self.get_channel_names():
            try:
                result[key] = cpp_map[key]
            except:
                result[key] = 0
        return result
    
    def get_burst_channel_indices_dict(self, burst_index):
        """Get photon indices for each channel in a specific burst
        
        Args:
            burst_index: Index of the burst
            
        Returns:
            Dict mapping channel names to lists of photon indices
        """
        if burst_index < 0 or burst_index >= len(self):
            raise IndexError("Burst index out of range")
        
        # Get burst start/stop from interleaved array
        bursts = self.get_bursts()
        if len(bursts) == 0:
            return {}
        start_idx = int(bursts[burst_index][0])
        stop_idx = int(bursts[burst_index][1])
        
        # Get the C++ map and convert to Python dict
        cpp_map = self.get_burst_channel_indices(start_idx, stop_idx)
        result = {}
        for key in self.get_channel_names():
            try:
                result[key] = list(cpp_map[key])
            except:
                result[key] = []
        return result
    %}
#endif
}

// Use shared_ptr for BurstFilter to pass it around
%shared_ptr(tttrlib::BurstFilter)

// Type mapping for int64_t parameters (Python-only: uses the Python C-API).
// R and Java rely on SWIG's built-in int64_t handling instead.
#ifdef SWIGPYTHON
%typemap(in) int64_t {
    $1 = (int64_t)PyLong_AsLongLong($input);
}
#endif

%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long **output, int *dim1, int *dim2)};
%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long **find_output, int *find_dim1, int *find_dim2)};
%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long **size_output, int *size_dim1, int *size_dim2)};
%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long **duration_output, int *duration_dim1, int *duration_dim2)};
%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long **background_output, int *background_dim1, int *background_dim2)};
%apply(long long** ARGOUTVIEWM_ARRAY2, int* DIM1, int* DIM2) {(long long **merge_output, int *merge_dim1, int *merge_dim2)};

%rename(_reset_to_raw_bursts) tttrlib::BurstFilter::reset_to_raw_bursts;
%rename(_reapply_filters) tttrlib::BurstFilter::reapply_filters;
%rename(_clear_filters) tttrlib::BurstFilter::clear_filters;

// Burst selections arrive as a NumPy int array (Python) / numeric vector (R) /
// long[] (Java): std::vector<int64_t> inputs would reject Python lists and
// arrays on LP64 Linux, where int64_t stays opaque to SWIG.
%apply (long long* IN_ARRAY2, int DIM1, int DIM2) {(long long* selected_bursts, int n_selected_bursts, int n_cols)};

#ifdef SWIGPYTHON
// Array-out surface returns NumPy directly (no caller-side conversion):
// per-burst properties as float64 (2D for the all-bursts variant, one row per
// burst). Burst boundaries already come back as NumPy via get_bursts()
// (ARGOUTVIEWM above); get_burst_indices stays the C++-facing accessor.
%feature("pythonappend") tttrlib::BurstFilter::get_burst_properties %{
    import numpy as _np
    val = _np.asarray(val, dtype=_np.float64)
%}
%feature("pythonappend") tttrlib::BurstFilter::get_all_burst_properties %{
    import numpy as _np
    val = _np.asarray([_np.asarray(_row, dtype=_np.float64) for _row in val])
%}
#endif

// R has no 64-bit integers, and a JavaScript Number is exact only below 2^53,
// so neither backend can proxy a std::vector<int64_t>/map<...,vector<int64_t>>
// result faithfully. On LP64 Linux int64_t is 'long', which SWIG then cannot
// marshal as the long long containers below; the generated R/JS wrapper would
// not even compile. These are the burst-photon index accessors; the languages
// that need them (Python via numpy, Java via long[]) keep them, and R/JS read
// the same data through to_json_string() instead.
#if defined(SWIGR) || defined(SWIGJAVASCRIPT)
%ignore tttrlib::BurstFilter::get_burst_indices;
%ignore tttrlib::BurstFilter::get_burst_channel_indices;
%ignore tttrlib::BurstFilter::apply_mask;
%ignore tttrlib::BurstFilter::pairs_to_interleaved;
%ignore tttrlib::BurstFilter::interleaved_to_pairs;
#endif

%include "BurstFilter.h"
