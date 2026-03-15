#include "Channel.h"

namespace tttrlib {

Channel::Channel(const std::string& name) : name(name) {}

std::string Channel::get_name() const {
    return name;
}

void Channel::set_name(const std::string& name) {
    this->name = name;
}

void Channel::add_component(int routing_channel, int microtime_start, int microtime_stop) {
    components.emplace_back(routing_channel, microtime_start, microtime_stop);
}

const std::vector<std::tuple<int, int, int>>& Channel::get_components() const {
    return components;
}

size_t Channel::get_component_count() const {
    return components.size();
}

void Channel::clear_components() {
    components.clear();
}

json Channel::to_json() const {
    json j;
    j["name"] = name;
    j["components"] = json::array();
    
    for (const auto& comp : components) {
        json comp_j;
        comp_j["routing_channel"] = std::get<0>(comp);
        comp_j["microtime_range"] = {std::get<1>(comp), std::get<2>(comp)};
        j["components"].push_back(comp_j);
    }
    
    return j;
}

void Channel::from_json(const json& j) {
    name = j["name"].get<std::string>();
    components.clear();
    
    if (j.contains("components") && j["components"].is_array()) {
        for (const auto& comp_j : j["components"]) {
            int rout = comp_j["routing_channel"].get<int>();
            
            // Support both old format (microtime_min/max) and new format (microtime_range)
            int mt_start, mt_stop;
            if (comp_j.contains("microtime_range") && comp_j["microtime_range"].is_array()) {
                mt_start = comp_j["microtime_range"][0].get<int>();
                mt_stop = comp_j["microtime_range"][1].get<int>();
            } else {
                // Fallback for old format
                mt_start = comp_j.value("microtime_min", 0);
                mt_stop = comp_j.value("microtime_max", 65535);
            }
            add_component(rout, mt_start, mt_stop);
        }
    }
}

} // namespace tttrlib
