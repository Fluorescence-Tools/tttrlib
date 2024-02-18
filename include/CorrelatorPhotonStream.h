#ifndef TTTRLIB_CORRELATORPHOTONSTREAM_H
#define TTTRLIB_CORRELATORPHOTONSTREAM_H

#include <vector>
#include <numeric>  /* std::accumulate */

#include "TTTR.h"



//! CorrelatorPhotonStream gathers event times and weights
class CorrelatorPhotonStream{

    friend class Correlator;

protected:

    /// Time axis calibration is a factor that converts the time axsis
    /// of the time events to milliseconds. By default this value is set
    /// to 1.0 and the returned time axis is in units of the time events.
    double time_axis_calibration = 1.0;

    std::shared_ptr<TTTR> tttr = nullptr;

public:

    CorrelatorPhotonStream() = default;

    CorrelatorPhotonStream(const CorrelatorPhotonStream& a):
    times(a.times), weights(a.weights) {};

    ~CorrelatorPhotonStream() = default;

    /// The array containing the times points of the first correlation channel
    std::vector<unsigned long long> times;

    /// The array containing the weights of the first correlation channel
    std::vector<double> weights;

    bool empty() const{
        return times.empty() && weights.empty();
    }

    size_t size() const{
        return times.size();
    }

    void clear(){
        times.clear();
        weights.clear();
    }

    /*!
     *
     * @param n
     * @param x initial value of the weights
     */
    void resize(size_t n, double x=1.0){
        times.resize(n);
        weights.resize(n, x);
    }

    /*!
    * Changes the time events by adding the micro time to the macro time
    *
    * Changes the time events by adding the micro time to the macro time.
    * The micro times should match the macro time, i.e., the length of
    * the micro time array should be the at least the same length as the
    * macro time array.
    *
    * @param[in,out] t An array containing the time events (macro times)
    * @param[in] n_times The number of macro times.
    * @param[in] tac An array containing the micro times of the corresponding macro times
    !*/
    static void make_fine_times(
            unsigned long long *t, unsigned int n_times,
            unsigned short *tac,
            unsigned int n_tac
    );

    void make_fine(
            unsigned short *tac, int n_tac,
            unsigned int number_of_microtime_channels
    );

    void set_weights(
        const std::map<short, std::vector<double>>& filter,
        std::vector<unsigned int> micro_times = std::vector<unsigned int>(),
        std::vector<signed char> routing_channels = std::vector<signed char>()
    );

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

    /*!
     * Coarsens the time events
     *
     * This method coarsens the time events by dividing the times by two.
     * In case two consecutive time events in the array have the same time, the
     * weights of the two events are added to the following weight element and the
     * value of the previous weight is set to zero.
     *
     */
    void coarsen();

    unsigned long long dt();

    double sum_of_weights();

    double mean_count_rate();

    /*!
     * Set time axis calibration. The time axis calibration if the duration of
     * between two sync signals (macro time clock)
     *
     * @param v time axis calibration (duration between sync clock signals) in
     * seconds
     */
    void set_time_axis_calibration(double v);

    /*!
     * @return The calibration of the time axis in seconds. The time axis
     * calibration is the duration of a sync signal (macro time clock).
     */
    double get_time_axis_calibration() const{
        return time_axis_calibration;
    }

    void set_tttr(
            std::shared_ptr<TTTR> tttr,
            bool make_fine = false
    );

    std::shared_ptr<TTTR> get_tttr() const{
        return tttr;
    }

};

#endif //TTTRLIB_CORRELATORPHOTONSTREAM_H
