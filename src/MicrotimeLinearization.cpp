#include "include/MicrotimeLinearization.h"
#include <algorithm>
#include <chrono>

MicrotimeLinearization::MicrotimeLinearization(
    const std::map<int, std::vector<float>>& linearization_tables,
    const std::map<int, int>& channel_to_card_map,
    const std::map<int, int>& tacstart_map,
    const std::map<int, int>& tacshift_map
) : _linearization_tables(linearization_tables),
    _channel_to_card_map(channel_to_card_map),
    _tacstart_map(tacstart_map),
    _tacshift_map(tacshift_map) {
    
    // Set default values for missing tacstart and tacshift
    for (const auto& pair : _linearization_tables) {
        int card_id = pair.first;
        if (_tacstart_map.find(card_id) == _tacstart_map.end()) {
            _tacstart_map[card_id] = 0;
        }
        if (_tacshift_map.find(card_id) == _tacshift_map.end()) {
            _tacshift_map[card_id] = 0;
        }
    }
}

MicrotimeLinearization::~MicrotimeLinearization() {
    // Vectors are automatically cleaned up
}

bool MicrotimeLinearization::is_valid() const {
    return !_linearization_tables.empty();
}

void MicrotimeLinearization::add_card(int card_id, const std::vector<float>& linearization_table) {
    _linearization_tables[card_id] = linearization_table;
    // Set default TAC parameters if not already set
    if (_tacstart_map.find(card_id) == _tacstart_map.end()) {
        _tacstart_map[card_id] = 0;
    }
    if (_tacshift_map.find(card_id) == _tacshift_map.end()) {
        _tacshift_map[card_id] = 0;
    }
}

std::vector<float> MicrotimeLinearization::get_linearization_table(int card_id) const {
    auto it = _linearization_tables.find(card_id);
    if (it != _linearization_tables.end()) {
        return it->second;
    }
    return std::vector<float>();  // Return empty vector
}

void MicrotimeLinearization::set_tacstart(int card_id, int tacstart) {
    _tacstart_map[card_id] = tacstart;
}

int MicrotimeLinearization::get_tacstart(int card_id) const {
    auto it = _tacstart_map.find(card_id);
    if (it != _tacstart_map.end()) {
        return it->second;
    }
    return 0;  // Default value
}

void MicrotimeLinearization::set_tacshift(int card_id, int tacshift) {
    _tacshift_map[card_id] = tacshift;
}

int MicrotimeLinearization::get_tacshift(int card_id) const {
    auto it = _tacshift_map.find(card_id);
    if (it != _tacshift_map.end()) {
        return it->second;
    }
    return 0;  // Default value
}

std::vector<int> MicrotimeLinearization::get_card_ids() const {
    std::vector<int> card_ids;
    for (const auto& pair : _linearization_tables) {
        card_ids.push_back(pair.first);
    }
    return card_ids;
}

void MicrotimeLinearization::set_card_for_channel(int routing_channel, int card_id) {
    _channel_to_card_map[routing_channel] = card_id;
}

int MicrotimeLinearization::get_card_for_channel(int routing_channel) const {
    auto it = _channel_to_card_map.find(routing_channel);
    if (it != _channel_to_card_map.end()) {
        return it->second;
    }
    return -1;  // Channel not mapped
}

std::vector<int> MicrotimeLinearization::get_channels_for_card(int card_id) const {
    std::vector<int> channels;
    for (const auto& pair : _channel_to_card_map) {
        if (pair.second == card_id) {
            channels.push_back(pair.first);
        }
    }
    return channels;
}

std::vector<float> MicrotimeLinearization::generate_random_numbers(int n_random, unsigned int seed) {
    std::vector<float> random_numbers(n_random);
    
    // Use time-based seed if seed is 0
    if (seed == 0) {
        seed = static_cast<unsigned int>(
            std::chrono::system_clock::now().time_since_epoch().count()
        );
    }
    
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    
    for (int i = 0; i < n_random; i++) {
        random_numbers[i] = dis(gen);
    }
    
    return random_numbers;
}

int MicrotimeLinearization::linearize(
    unsigned short* microtimes,
    const unsigned char* channels,
    int n_photons,
    const float* random_numbers
) {
    return _linearize_internal(microtimes, channels, n_photons, random_numbers, true);
}

int MicrotimeLinearization::linearize(
    unsigned short* microtimes,
    const unsigned char* channels,
    int n_photons,
    unsigned int seed
) {
    // Generate random numbers internally
    std::vector<float> random_numbers = generate_random_numbers(n_photons, seed);
    return _linearize_internal(microtimes, channels, n_photons, random_numbers.data(), true);
}

int MicrotimeLinearization::_linearize_internal(
    unsigned short* microtimes,
    const unsigned char* channels,
    int n_photons,
    const float* random_numbers,
    bool use_provided_random
) {
    if (!is_valid()) {
        return 0;
    }
    
    if (microtimes == nullptr || channels == nullptr) {
        return 0;
    }
    
    if (n_photons <= 0) {
        return 1;
    }
    
    // Ensure random_numbers is available
    std::vector<float> generated_random;
    if (!use_provided_random || random_numbers == nullptr) {
        generated_random = generate_random_numbers(n_photons);
        random_numbers = generated_random.data();
    }
    
    // Linearization loop (based on Seidel Software taclin algorithm)
    // Supports n cards with flexible channel-to-card mapping
    double t;
    int tn;
    
    for (int i = 0; i < n_photons; i++) {
        unsigned char channel = channels[i];
        
        // Determine card ID from channel using the channel-to-card map
        int card_id = get_card_for_channel(channel);
        if (card_id < 0) {
            continue;  // Channel not mapped to any card
        }
        
        // Get linearization table for this card
        const std::vector<float>& lt = get_linearization_table(card_id);
        if (lt.empty()) {
            continue;  // Skip if no linearization table for this card
        }
        
        int tacstart = get_tacstart(card_id);
        int tacshift = get_tacshift(card_id);
        
        tn = microtimes[i] - tacstart;
        
        // Check if TAC value is within valid range
        if ((tn < 1) || (tn > (int)lt.size() - 1)) {
            continue;
        }
        
        // Linear interpolation with dithering
        t = lt[tn - 1] * (1.0f - random_numbers[i]) + 
            lt[tn] * random_numbers[i];
        
        // Convert back to unsigned short with rounding
        microtimes[i] = (unsigned short)std::floor(t + 0.5) + tacstart + tacshift;
    }
    
    return 1;
}
