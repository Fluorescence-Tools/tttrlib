%{
#include "Channel.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
%}

// Use shared_ptr for Channel
%shared_ptr(tttrlib::Channel)

%extend tttrlib::Channel {
    std::string to_json_string() const {
        return $self->to_json().dump();
    }

    void from_json_string(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }

    %pythoncode %{
    
    def __repr__(self):
        return f"Channel(name='{self.get_name()}', components={self.get_component_count()})"
    
    def __str__(self):
        return f"{self.get_name()}: {self.get_component_count()} component(s)"
    
    def add_routing_channel(self, routing_channel, microtime_min=0, microtime_max=65535):
        """Add a routing channel with optional microtime range
        
        Args:
            routing_channel: Routing channel number
            microtime_min: Minimum microtime value (default: 0)
            microtime_max: Maximum microtime value (default: 65535)
        """
        self.add_component(routing_channel, microtime_min, microtime_max)
    
    def get_components_as_list(self):
        """Get all components as a list of dicts
        
        Returns:
            List of dicts with keys: routing_channel, microtime_min, microtime_max
        """
        components = []
        for i in range(self.get_component_count()):
            # Access via C++ getter - need to iterate through components
            pass
        # This will be enhanced when we add proper iteration support
        return components
    
    %}
}

%include "Channel.h"
