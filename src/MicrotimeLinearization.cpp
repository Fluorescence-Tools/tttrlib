#include "include/MicrotimeLinearization.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

MicrotimeLinearization::MicrotimeLinearization() :
    _channel_luts(TTTRLIB_MAX_ROUTING_CHANNELS),  // TTTRLIB_MAX_ROUTING_CHANNELS channels
    _channel_shifts(TTTRLIB_MAX_ROUTING_CHANNELS, 0),  // Initialize all shifts to 0
    _expected_lut_size(0)  // No expected size initially
{
}

MicrotimeLinearization::MicrotimeLinearization(const MicrotimeLinearization& other) :
    _channel_luts(other._channel_luts),  // Copy LUTs
    _channel_shifts(other._channel_shifts),  // Copy shifts
    _expected_lut_size(other._expected_lut_size)  // Copy expected LUT size
{
}

MicrotimeLinearization::~MicrotimeLinearization() {
    // Vectors are automatically cleaned up
}

void MicrotimeLinearization::set_channel_lut(int routing_channel, const std::vector<float>& linearization_table) {
    if (routing_channel >= 0 && routing_channel < TTTRLIB_MAX_ROUTING_CHANNELS) {
        // Validate LUT size consistency
        if (!linearization_table.empty()) {
            if (_expected_lut_size == 0) {
                // First LUT sets the expected size
                _expected_lut_size = linearization_table.size();
            } else if (linearization_table.size() != _expected_lut_size) {
                // Size mismatch - this is an error
                std::stringstream ss;
                ss << "All LUT tables must have the same size. Expected size: " 
                   << _expected_lut_size << ", got: " << linearization_table.size();
                throw std::invalid_argument(ss.str());
            }
        }
        _channel_luts[routing_channel] = linearization_table;
    }
}

void MicrotimeLinearization::set_channel_shift(int routing_channel, int shift) {
    if (routing_channel >= 0 && routing_channel < TTTRLIB_MAX_ROUTING_CHANNELS) {
        _channel_shifts[routing_channel] = shift;
    }
}

std::vector<float> MicrotimeLinearization::get_channel_lut(int routing_channel) const {
    if (routing_channel >= 0 && routing_channel < TTTRLIB_MAX_ROUTING_CHANNELS) {
        return _channel_luts[routing_channel];
    }
    return std::vector<float>();
}

int MicrotimeLinearization::get_channel_shift(int routing_channel) const {
    if (routing_channel >= 0 && routing_channel < TTTRLIB_MAX_ROUTING_CHANNELS) {
        return _channel_shifts[routing_channel];
    }
    return 0;
}

bool MicrotimeLinearization::has_luts() const {
    for (const auto& lut : _channel_luts) {
        if (!lut.empty()) {
            return true;
        }
    }
    return false;
}

void MicrotimeLinearization::set_channel_luts_from_array(const float* luts, int n_channels, int lut_size) {
    // Validate input
    if (luts == nullptr || n_channels <= 0 || lut_size <= 0) {
        throw std::invalid_argument("Invalid input parameters for set_channel_luts_from_array");
    }
    if (n_channels > TTTRLIB_MAX_ROUTING_CHANNELS) {
        throw std::invalid_argument("Too many channels (max TTTRLIB_MAX_ROUTING_CHANNELS)");
    }

    // Set expected LUT size
    _expected_lut_size = lut_size;

    // Copy LUTs from the 2D array (row-major order: channels x lut_size)
    for (int ch = 0; ch < n_channels; ++ch) {
        if (ch < TTTRLIB_MAX_ROUTING_CHANNELS) {
            std::vector<float> lut(lut_size);
            for (int i = 0; i < lut_size; ++i) {
                lut[i] = luts[ch * lut_size + i];
            }
            _channel_luts[ch] = lut;
        }
    }
}

void MicrotimeLinearization::set_channel_shifts_from_array(const int* shifts, int n_channels) {
    // Validate input
    if (shifts == nullptr || n_channels <= 0) {
        throw std::invalid_argument("Invalid input parameters for set_channel_shifts_from_array");
    }
    if (n_channels > TTTRLIB_MAX_ROUTING_CHANNELS) {
        throw std::invalid_argument("Too many channels (max TTTRLIB_MAX_ROUTING_CHANNELS)");
    }

    // Copy shifts from the array
    for (int ch = 0; ch < n_channels; ++ch) {
        if (ch < TTTRLIB_MAX_ROUTING_CHANNELS) {
            _channel_shifts[ch] = shifts[ch];
        }
    }
}

void MicrotimeLinearization::get_channel_luts_as_array(float** luts, int* n_channels, int* lut_size) {
    if (luts == nullptr || n_channels == nullptr || lut_size == nullptr) {
        throw std::invalid_argument("Output parameters cannot be null");
    }

    // Find the maximum channel that has LUTs
    int max_channel = 0;
    for (int ch = 0; ch < TTTRLIB_MAX_ROUTING_CHANNELS; ++ch) {
        if (!_channel_luts[ch].empty()) {
            max_channel = ch;
        }
    }

    *n_channels = max_channel + 1;
    *lut_size = _expected_lut_size;

    // Allocate output array: [n_channels x lut_size]
    size_t total_size = static_cast<size_t>(*n_channels) * static_cast<size_t>(*lut_size);
    *luts = new float[total_size];

    // Copy LUTs to the output array (row-major order)
    for (int ch = 0; ch < *n_channels; ++ch) {
        const auto& lut = _channel_luts[ch];
        for (size_t i = 0; i < *lut_size; ++i) {
            if (i < lut.size()) {
                (*luts)[ch * (*lut_size) + i] = lut[i];
            } else {
                (*luts)[ch * (*lut_size) + i] = 0.0f; // Pad with zeros if LUT is shorter
            }
        }
    }
}

void MicrotimeLinearization::get_channel_shifts_as_array(int** shifts, int* n_channels) {
    if (shifts == nullptr || n_channels == nullptr) {
        throw std::invalid_argument("Output parameters cannot be null");
    }

    // Find the maximum channel that has non-zero shifts
    int max_channel = 0;
    for (int ch = 0; ch < TTTRLIB_MAX_ROUTING_CHANNELS; ++ch) {
        if (_channel_shifts[ch] != 0) {
            max_channel = ch;
        }
    }

    *n_channels = max_channel + 1;

    // Allocate output array
    *shifts = new int[*n_channels];

    // Copy shifts to the output array
    for (int ch = 0; ch < *n_channels; ++ch) {
        (*shifts)[ch] = _channel_shifts[ch];
    }
}

int MicrotimeLinearization::linearize(
    unsigned short* microtimes,
    const unsigned char* channels,
    int n_photons,
    unsigned int seed,
    bool use_dithering
) {
    if (microtimes == nullptr || channels == nullptr) {
        return 0;
    }
    
    if (n_photons <= 0) {
        return 1;
    }
    
    // Generate random numbers for dithering only if needed
    std::vector<float> random_numbers;
    if (use_dithering) {
        random_numbers = generate_random_numbers(n_photons, seed);
    }
    
    // Create LUT size lookup array for efficiency
    std::vector<size_t> lut_sizes(TTTRLIB_MAX_ROUTING_CHANNELS, 0);
    for (int ch = 0; ch < TTTRLIB_MAX_ROUTING_CHANNELS; ++ch) {
        if (ch < _channel_luts.size()) {
            lut_sizes[ch] = _channel_luts[ch].size();
        }
    }
    
    // Apply linearization and shifts in one pass
    for (int i = 0; i < n_photons; i++) {
        unsigned char channel = channels[i];
        
        if (channel >= _channel_luts.size()) {
            continue;  // Invalid channel
        }
        
        const std::vector<float>& lut = _channel_luts[channel];
        int shift = _channel_shifts[channel];
        size_t lut_size = lut_sizes[channel];
        unsigned short new_microtime = microtimes[i];  // Start with current value
        
        if (lut_size == 0) {
            // No LUT for this channel, just apply shift without modulo wrapping
            // since there's no defined LUT size
            if (shift != 0) {
                float result = microtimes[i] + shift;
                // Ensure result is non-negative
                if (result < 0) {
                    result = 0;  // Clamp to 0 instead of wrapping
                }
                new_microtime = static_cast<unsigned short>(result);
            }
        } else {
            // Apply linearization with optional dithering
            unsigned short microtime = microtimes[i];
            
            // Check bounds - microtime must be < lut_size - 1 for interpolation
            if (microtime >= lut_size - 1) {
                // Out of bounds, just apply shift using expected LUT size
                if (shift != 0 && _expected_lut_size > 0) {
                    float result = microtime + shift;
                    // Ensure result is non-negative before modulo
                    if (result < 0) {
                        result += _expected_lut_size;
                    }
                    new_microtime = static_cast<unsigned short>(result) % _expected_lut_size;
                }
            } else {
                // Linear interpolation with optional dithering
                float t;
                if (use_dithering) {
                    t = lut[microtime] * (1.0f - random_numbers[i]) + 
                        lut[microtime + 1] * random_numbers[i];
                } else {
                    // No dithering - use nearest neighbor
                    t = lut[microtime];
                }
                
                // Convert back to unsigned short with rounding and apply shift
                float result = std::floor(t + 0.5f) + shift;
                // Ensure result is non-negative before modulo
                if (result < 0) {
                    result += _expected_lut_size;
                }
                new_microtime = static_cast<unsigned short>(result) % _expected_lut_size;
            }
        }
        
        // Write back the computed value (single array write per iteration)
        microtimes[i] = new_microtime;
    }
    
    return 1;
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
