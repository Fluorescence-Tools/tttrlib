#ifndef TTTRLIB_CORRELATORPHOTONSTREAM_H
#define TTTRLIB_CORRELATORPHOTONSTREAM_H

#include <vector>
#include <numeric>  /* std::accumulate */

#include "TTTR.h"



/*!
 * @brief CorrelatorPhotonStream class gathers event times and weights.
 */
class CorrelatorPhotonStream{

    friend class Correlator;

protected:

    /// Time axis calibration is a factor that converts the time axsis
    /// of the time events to milliseconds. By default this value is set
    /// to 1.0 and the returned time axis is in units of the time events.
    double time_axis_calibration = 1.0;

    std::shared_ptr<TTTR> tttr = nullptr;

public:

    /*!
     * @brief Default constructor for CorrelatorPhotonStream.
     */
    CorrelatorPhotonStream() = default;

    /*!
     * @brief Copy constructor for CorrelatorPhotonStream.
     *
     * @param a Another CorrelatorPhotonStream object to copy.
     */
    CorrelatorPhotonStream(const CorrelatorPhotonStream& a):
    times(a.times), weights(a.weights) {};

    /*!
     * @brief Default destructor for CorrelatorPhotonStream.
     */
    ~CorrelatorPhotonStream() = default;

    /*!
     * @brief The array containing the time points of the first correlation channel.
     */
    std::vector<unsigned long long> times;

    /*!
     * @brief The array containing the weights of the first correlation channel.
     */
    std::vector<double> weights;

    /**
     * @brief Check if the CorrelatorPhotonStream is empty.
     *
     * @return True if both the times and weights arrays are empty, false otherwise.
     */
    bool empty() const{
        return times.empty() && weights.empty();
    }

    /**
     * @brief Get the size of the CorrelatorPhotonStream.
     *
     * @return The number of elements in the times array.
     */
    size_t size() const{
        return times.size();
    }

    /**
     * @brief Clear the CorrelatorPhotonStream.
     *
     * Clears the times and weights arrays, making them empty.
     */
    void clear(){
        times.clear();
        weights.clear();
    }

    /**
     * @brief Resize the CorrelatorPhotonStream.
     *
     * Resizes the times and weights arrays to the specified size, setting the
     * initial value of weights to the provided value.
     *
     * @param n The new size for the arrays.
     * @param x The initial value of the weights (default is 1.0).
     */
    void resize(size_t n, double x = 1.0){
        times.resize(n);
        weights.resize(n, x);
    }

    /**
     * @brief Make time events fine by adding micro time to macro time.
     *
     * Changes the time events by adding the micro time to the macro time.
     * The micro times should match the macro time, i.e., the length of
     * the micro time array should be at least the same length as the
     * macro time array.
     *
     * @param[in,out] t An array containing the time events (macro times).
     * @param[in] n_times The number of macro times.
     * @param[in] tac An array containing the micro times corresponding to macro times.
     */
    static void make_fine_times(
            unsigned long long *t, unsigned int n_times,
            unsigned short *tac,
            unsigned int n_tac
    );

    /**
     * @brief Make time events fine by adding micro time to macro time.
     *
     * Changes the time events by adding the micro time to the macro time.
     * The micro times should match the macro time, i.e., the length of
     * the micro time array should be at least the same length as the
     * macro time array.
     *
     * @param[in] tac An array containing the micro times corresponding to macro times.
     * @param[in] n_tac The number of micro times.
     * @param[in] number_of_microtime_channels The maximum number of TAC channels of the micro times.
     */
    void make_fine(
            unsigned short *tac, int n_tac,
            unsigned int number_of_microtime_channels
    );

    /**
     * @brief Set weights using a filter for time events.
     *
     * The weights are set based on a filter map and optional micro times and routing channels.
     *
     * @param[in] filter Map of filters. The first element in the map is the routing
     * channel number, and the second element of the map is a vector that maps a
     * micro time to a filter value.
     * @param[in] micro_times Optional array containing micro times.
     * @param[in] routing_channels Optional array containing routing channels.
     */
    void set_weights(
        const std::map<short, std::vector<double>>& filter,
        std::vector<unsigned int> micro_times = std::vector<unsigned int>(),
        std::vector<signed char> routing_channels = std::vector<signed char>()
    );

    /**
     * @brief Set time events and weights.
     *
     * Sets the time events and their corresponding weights based on provided arrays.
     *
     * @param[in] t Array containing time events.
     * @param[in] n_t Number of time events.
     * @param[in] weight Array containing weights.
     * @param[in] n_weight Number of weights.
     */
    void set_events(
        unsigned long long  *t, int n_t,
        double* weight, int n_weight
    ){
        resize(std::min(n_t, n_weight));
        for(size_t i = 0; i < size();i++){
            times[i] = t[i];
            weights[i] = weight[i];
        }
    }

    /**
     * @brief Coarsen the time events.
     *
     * Coarsens the time events by dividing the times by two. If two consecutive time events
     * have the same time, the weights of the two events are added to the following weight element,
     * and the value of the previous weight is set to zero.
     */
    void coarsen();

    /**
     * @brief Compute the time difference in macro time units between the first and last event.
     *
     * @return The time difference between the first and last event.
     */
    unsigned long long dt();

    /**
     * @brief Compute the sum of weights in the correlation stream.
     *
     * @return The sum of weights in the correlation stream.
     */
    double sum_of_weights();

    /**
     * @brief Compute the mean count rate in the correlation stream.
     *
     * @return The mean count rate in counts per macro time clock.
     */
    double mean_count_rate();

    /**
     * @brief Set the time axis calibration.
     *
     * The time axis calibration represents the duration between two sync signals (macro time clock).
     *
     * @param v The time axis calibration in seconds.
     */
    void set_time_axis_calibration(double v);

    /**
     * @brief Get the calibration of the time axis.
     *
     * Returns the calibration of the time axis in seconds, which represents the duration
     * of a sync signal (macro time clock).
     *
     * @return The calibration of the time axis in seconds.
     */
    double get_time_axis_calibration() const{
        return time_axis_calibration;
    }

    /**
     * @brief Set the TTTR object for the CorrelatorPhotonStream.
     *
     * Sets the TTTR object for the CorrelatorPhotonStream and optionally makes the time events fine
     * by adding the micro time to the macro time.
     *
     * @param tttr The TTTR object to set.
     * @param make_fine If true, makes the time events fine by adding the micro time to the macro time.
     */
    void set_tttr(std::shared_ptr<TTTR> tttr, bool make_fine = false);

    /**
     * @brief Get the TTTR object associated with the CorrelatorPhotonStream.
     *
     * @return The shared pointer to the TTTR object.
     */
    std::shared_ptr<TTTR> get_tttr() const {
        return tttr;
    }

};

#endif //TTTRLIB_CORRELATORPHOTONSTREAM_H
