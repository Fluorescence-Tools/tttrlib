#ifndef TTTRLIB_MICROTIME_LINEARIZATION_H
#define TTTRLIB_MICROTIME_LINEARIZATION_H

#include <vector>
#include <cmath>
#include <cstring>
#include <random>
#include <memory>
#include <map>

/*!
 * \brief Microtime linearization for SPC Becker cards
 *
 * This module provides functionality to linearize microtimes (TAC values) from
 * Becker & Hickl SPC cards. The linearization corrects for non-linear response
 * of the Time-to-Amplitude Converter (TAC) using lookup tables.
 *
 * Supports any number of SPC cards with independent linearization tables and TAC parameters.
 *
 * Based on the Seidel Software taclin algorithm (version 2008-01-28).
 *
 * \see TTTR
 */
class MicrotimeLinearization {
public:

    /*!
     * \brief Constructor for MicrotimeLinearization with n cards
     *
     * \param linearization_tables Map of card_id -> linearization table (float vector)
     * \param channel_to_card_map Map of routing_channel -> card_id (defines which card handles each channel)
     * \param tacstart_map Map of card_id -> first TAC channel (default: 0 for all)
     * \param tacshift_map Map of card_id -> TAC shift (default: 0 for all)
     */
    MicrotimeLinearization(
        const std::map<int, std::vector<float>>& linearization_tables,
        const std::map<int, int>& channel_to_card_map,
        const std::map<int, int>& tacstart_map = std::map<int, int>(),
        const std::map<int, int>& tacshift_map = std::map<int, int>()
    );


    /*!
     * \brief Destructor
     */
    ~MicrotimeLinearization();

    /*!
     * \brief Linearize microtimes using provided random numbers
     *
     * Applies linearization to an array of microtimes using the stored linearization tables.
     * The linearization uses linear interpolation between table values with dithering using
     * random numbers to reduce quantization artifacts.
     *
     * \param microtimes Array of microtime values to linearize (modified in-place)
     * \param channels Array of channel numbers mapped to cards via channel_to_card_map
     * \param n_photons Number of photons to process
     * \param random_numbers Array of random numbers in range [0, 1) for dithering
     *
     * \return 1 on success, 0 on failure
     */
    int linearize(
        unsigned short* microtimes,
        const unsigned char* channels,
        int n_photons,
        const float* random_numbers
    );

    /*!
     * \brief Linearize microtimes with automatic random number generation
     *
     * Applies linearization to an array of microtimes. Random numbers are generated
     * internally using a Mersenne Twister generator for dithering.
     *
     * \param microtimes Array of microtime values to linearize (modified in-place)
     * \param channels Array of channel numbers mapped to cards via channel_to_card_map
     * \param n_photons Number of photons to process
     * \param seed Random seed (default: 0 for time-based seed)
     *
     * \return 1 on success, 0 on failure
     */
    int linearize(
        unsigned short* microtimes,
        const unsigned char* channels,
        int n_photons,
        unsigned int seed = 0
    );

    /*!
     * \brief Validate linearization tables
     *
     * Checks if the linearization tables are valid (non-empty, reasonable values).
     *
     * \return true if tables are valid, false otherwise
     */
    bool is_valid() const;

    /*!
     * \brief Add or update a linearization table for a card
     *
     * \param card_id Card ID (0-based)
     * \param linearization_table Linearization table for this card
     */
    void add_card(int card_id, const std::vector<float>& linearization_table);

    /*!
     * \brief Get linearization table for a specific card as numpy-compatible vector
     *
     * \param card_id Card ID (0-based)
     * \return Linearization table for the card as vector, or empty vector if not found
     */
    std::vector<float> get_linearization_table(int card_id) const;

    /*!
     * \brief Set TAC start value for a specific card
     *
     * \param card_id Card ID (0-based)
     * \param tacstart TAC start value
     */
    void set_tacstart(int card_id, int tacstart);

    /*!
     * \brief Get TAC start value for a specific card
     *
     * \param card_id Card ID (0-based)
     * \return TAC start value (default 0 if not set)
     */
    int get_tacstart(int card_id) const;

    /*!
     * \brief Set TAC shift for a specific card
     *
     * \param card_id Card ID (0-based)
     * \param tacshift TAC shift value
     */
    void set_tacshift(int card_id, int tacshift);

    /*!
     * \brief Get TAC shift for a specific card
     *
     * \param card_id Card ID (0-based)
     * \return TAC shift value (default 0 if not set)
     */
    int get_tacshift(int card_id) const;

    /*!
     * \brief Get number of configured cards
     *
     * \return Number of cards with linearization tables
     */
    int get_num_cards() const { return (int)_linearization_tables.size(); }

    /*!
     * \brief Get all card IDs
     *
     * \return Vector of card IDs
     */
    std::vector<int> get_card_ids() const;

    /*!
     * \brief Set card ID for a specific routing channel
     *
     * \param routing_channel Routing channel number
     * \param card_id Card ID to assign to this channel
     */
    void set_card_for_channel(int routing_channel, int card_id);

    /*!
     * \brief Get card ID for a specific routing channel
     *
     * \param routing_channel Routing channel number
     * \return Card ID for this channel, or -1 if not mapped
     */
    int get_card_for_channel(int routing_channel) const;

    /*!
     * \brief Get all routing channels mapped to a specific card
     *
     * \param card_id Card ID
     * \return Vector of routing channels for this card
     */
    std::vector<int> get_channels_for_card(int card_id) const;

protected:
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

private:
    /*!
     * \brief Map of card_id -> linearization table
     */
    std::map<int, std::vector<float>> _linearization_tables;

    /*!
     * \brief Map of routing_channel -> card_id (defines channel-to-card routing)
     */
    std::map<int, int> _channel_to_card_map;

    /*!
     * \brief Map of card_id -> TAC start value
     */
    std::map<int, int> _tacstart_map;

    /*!
     * \brief Map of card_id -> TAC shift value
     */
    std::map<int, int> _tacshift_map;

    /*!
     * \brief Internal linearization implementation
     */
    int _linearize_internal(
        unsigned short* microtimes,
        const unsigned char* channels,
        int n_photons,
        const float* random_numbers,
        bool use_provided_random
    );
};

#endif // TTTRLIB_MICROTIME_LINEARIZATION_H
