#ifndef TTTRLIB_CHANNEL_H
#define TTTRLIB_CHANNEL_H

#include <string>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace tttrlib {

/**
 * @brief Represents a detection channel with routing channels and microtime ranges
 * 
 * A channel consists of one or more routing channel/microtime range pairs.
 * Example: redParallel = {rout=1, microtime=0-2048}, {rout=9, microtime=0-2048}
 */
class Channel {
private:
    std::string name;  // Channel name (e.g., "redParallel", "greenParallel")
    
    // List of (routing_channel, microtime_start, microtime_stop) tuples
    std::vector<std::tuple<int, int, int>> components;
    
public:
    /**
     * @brief Constructor
     * @param name Channel name
     */
    explicit Channel(const std::string& name = "");
    
    /**
     * @brief Get channel name
     */
    std::string get_name() const;
    
    /**
     * @brief Set channel name
     */
    void set_name(const std::string& name);
    
    /**
     * @brief Add a routing channel with microtime range
     * @param routing_channel Routing channel number
     * @param microtime_start Start of microtime range (inclusive)
     * @param microtime_stop End of microtime range (inclusive)
     */
    void add_component(int routing_channel, int microtime_start, int microtime_stop);
    
    /**
     * @brief Get all components
     * @return Vector of (routing_channel, microtime_start, microtime_stop) tuples
     */
    const std::vector<std::tuple<int, int, int>>& get_components() const;
    
    /**
     * @brief Get number of components
     */
    size_t get_component_count() const;
    
    /**
     * @brief Clear all components
     */
    void clear_components();
    
    /**
     * @brief Serialize channel to JSON
     */
    json to_json() const;
    
    /**
     * @brief Deserialize channel from JSON
     */
    void from_json(const json& j);
};

} // namespace tttrlib

#endif // TTTRLIB_CHANNEL_H
