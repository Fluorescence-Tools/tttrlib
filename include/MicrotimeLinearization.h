#ifndef TTTRLIB_MICROTIME_LINEARIZATION_H
#define TTTRLIB_MICROTIME_LINEARIZATION_H

#include <vector>
#include <cmath>
#include <cstring>
#include <random>
#include <memory>
#include <map>
#include <stdexcept>

#include "info.h"  // For TTTRLIB_MAX_ROUTING_CHANNELS

/*!
 * \brief Microtime linearization
 *
 * This module provides functionality to linearize microtimes (TAC values) from
 * time-resolved fluorescence measurements. The linearization corrects for 
 * non-linear response of Time-to-Amplitude Converters (TAC) using lookup tables.
 *
 * Supports per-channel linearization tables and shift corrections for up to TTTRLIB_MAX_ROUTING_CHANNELS channels.
 * Can be used with any time-resolved measurement system that requires microtime correction
 * (e.g., Becker & Hickl SPC cards, etc.).
 *
 * Based on the Seidel taclin algorithm (version 2008-01-28).
 *
 * \see TTTR
 */
class MicrotimeLinearization {
public:

    /*!
     * \brief Constructor
     */
    MicrotimeLinearization();

    /*!
     * \brief Copy constructor
     */
    MicrotimeLinearization(const MicrotimeLinearization& other);

    /*!
     * \brief Destructor
     */
    ~MicrotimeLinearization();

    /*!
     * \brief Linearize microtimes using configured LUTs and shifts
     *
     * Applies linearization lookup tables and channel-specific shifts to microtime data.
     * Uses linear interpolation between LUT entries with optional random dithering to
     * reduce quantization artifacts.
     *
     * \param microtimes Array of microtime values to linearize (modified in-place)
     * \param channels Array of channel numbers (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     * \param n_photons Number of photons to process
     * \param seed Random seed (default: 0 for time-based seed)
     * \param use_dithering Whether to apply random dithering (default: true)
     *
     * \return 1 on success, 0 on failure
     */
    int linearize(
        unsigned short* microtimes,
        const unsigned char* channels,
        int n_photons,
        unsigned int seed = 0,
        bool use_dithering = true
    );

    /*!
     * \brief Set linearization LUT for a specific routing channel
     *
     * \param routing_channel Routing channel number (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     * \param linearization_table Linearization table for this channel
     */
    void set_channel_lut(int routing_channel, const std::vector<float>& linearization_table);

    /*!
     * \brief Set shift value for a specific routing channel
     *
     * \param routing_channel Routing channel number (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     * \param shift Shift value to add to linearized microtimes
     */
    void set_channel_shift(int routing_channel, int shift);

    /*!
     * \brief Get linearization LUT for a specific routing channel
     *
     * \param routing_channel Routing channel number (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     * \return Linearization table for the channel, or empty vector if not set
     */
    std::vector<float> get_channel_lut(int routing_channel) const;

    /*!
     * \brief Get shift value for a specific routing channel
     *
     * \param routing_channel Routing channel number (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     * \return Shift value for the channel (default: 0 if not set)
     */
    int get_channel_shift(int routing_channel) const;

    /*!
     * \brief Check if any channels have LUTs configured
     *
     * \return true if at least one channel has a LUT, false otherwise
     */
    bool has_luts() const;

    /*!
     * \brief Set all channel LUTs from a 2D array
     *
     * \param luts 2D array of LUTs [n_channels x lut_size]
     * \param n_channels Number of channels (first dimension)
     * \param lut_size Size of each LUT (second dimension)
     */
    void set_channel_luts_from_array(const float* luts, int n_channels, int lut_size);

    /*!
     * \brief Set all channel shifts from a 1D array
     *
     * \param shifts 1D array of shift values [n_channels]
     * \param n_channels Number of channels
     */
    void set_channel_shifts_from_array(const int* shifts, int n_channels);

    /*!
     * \brief Get all channel LUTs as a 2D array
     *
     * \param luts Output 2D array [n_channels x lut_size]
     * \param n_channels Number of channels (output)
     * \param lut_size Size of each LUT (output)
     */
    void get_channel_luts_as_array(float** luts, int* n_channels, int* lut_size);

    /*!
     * \brief Get all channel shifts as a 1D array
     *
     * \param shifts Output 1D array [n_channels]
     * \param n_channels Number of channels (output)
     */
    void get_channel_shifts_as_array(int** shifts, int* n_channels);

private:
    /*!
     * \brief LUTs indexed by routing channel (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     */
    std::vector<std::vector<float>> _channel_luts;

    /*!
     * \brief Shift values indexed by routing channel (0-TTTRLIB_MAX_ROUTING_CHANNELS)
     */
    std::vector<int> _channel_shifts;

    /*!
     * \brief Expected size for all LUT tables (for validation)
     */
    size_t _expected_lut_size;

    /*!
     * \brief Generate random numbers for dithering
     *
     * Generates an array of random numbers in range [0, 1) for use in linearization.
     *
     * \param n_random Number of random numbers to generate
     * \param seed Random seed (default: 0 for time-based seed)
     * \return Vector of random numbers
     */
    static std::vector<float> generate_random_numbers(int n_random, unsigned int seed = 0);
};

#endif // TTTRLIB_MICROTIME_LINEARIZATION_H
