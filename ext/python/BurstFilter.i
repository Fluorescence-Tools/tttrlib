%{
#include "BurstFilter.h"
#include "Channel.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
%}

// Use shared_ptr for BurstFilter to pass it around
%shared_ptr(tttrlib::BurstFilter)

// Type mapping for int64_t parameters
%typemap(in) int64_t {
    $1 = (int64_t)PyLong_AsLongLong($input);
}

%extend tttrlib::BurstFilter {
    std::string to_json_string() const {
        return $self->to_json().dump();
    }

    void from_json_string(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }

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
        if not props:
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
        array_index = burst_index * 2
        start_idx = int(bursts[array_index])
        stop_idx = int(bursts[array_index + 1])
        
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
        array_index = burst_index * 2
        start_idx = int(bursts[array_index])
        stop_idx = int(bursts[array_index + 1])
        
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
}

// Use shared_ptr for BurstFilter to pass it around
%shared_ptr(tttrlib::BurstFilter)

// Type mapping for int64_t parameters
%typemap(in) int64_t {
    $1 = (int64_t)PyLong_AsLongLong($input);
}

%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **output, int *n_output)};
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **find_output, int *find_n_output)};
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **size_output, int *size_n_output)};
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **duration_output, int *duration_n_output)};
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **background_output, int *background_n_output)};
%apply(long long** ARGOUTVIEWM_ARRAY1, int* DIM1) {(long long **merge_output, int *merge_n_output)};

%rename(_reset_to_raw_bursts) tttrlib::BurstFilter::reset_to_raw_bursts;
%rename(_reapply_filters) tttrlib::BurstFilter::reapply_filters;
%rename(_clear_filters) tttrlib::BurstFilter::clear_filters;

%include "BurstFilter.h"
