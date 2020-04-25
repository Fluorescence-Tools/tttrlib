#include "decay.h"


void Decay::convolve_lifetime_spectrum(
        double* output, int n_output,
        double* time_axis, int n_time_axis,
        double *instrument_response_function, int n_instrument_response_function,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop,
        bool use_amplitude_threshold,
        double amplitude_threshold
){
    int number_of_exponentials = n_lifetime_spectrum / 2;
    convolution_stop = convolution_stop > 0 ?
            MIN(n_time_axis, MIN(n_instrument_response_function,
                    MIN(n_output, convolution_stop))) :
            MIN(n_time_axis, MIN(n_instrument_response_function, n_output));
#if VERBOSE
    std::clog << "convolve_lifetime_spectrum... " << std::endl;
    std::clog << "-- number_of_exponentials: " << number_of_exponentials << std::endl;
    std::clog << "-- convolution_start: " << convolution_start << std::endl;
    std::clog << "-- convolution_stop: " << convolution_stop << std::endl;
    std::clog << "-- use_amplitude_threshold: " << use_amplitude_threshold << std::endl;
    std::clog << "-- amplitude_threshold: " << amplitude_threshold << std::endl;
#endif
    for(int i=0; i<n_output; i++) output[i] = 0.0;
    if(use_amplitude_threshold){
        for(int ne = 0; ne<number_of_exponentials; ne++){
            double a = std::abs(lifetime_spectrum[2 * ne]);
            lifetime_spectrum[2 * ne] *= (a < amplitude_threshold);
        }
    }
    for(int ne=0; ne<number_of_exponentials; ne++){
        double a = lifetime_spectrum[2 * ne];
        double current_lifetime = (lifetime_spectrum[2 * ne + 1]);
        if(a == 0.0) continue;
        if(current_lifetime == 0.0) continue;
        double current_model_value = 0.0;
        for(int i=convolution_start; i<convolution_stop; i++){
            double dt = dt = (time_axis[i] - time_axis[i - 1]);
            double dt_2 = dt / 2.0;
            double current_exponential = std::exp(-dt / current_lifetime);
            current_model_value = (current_model_value + dt_2 * instrument_response_function[i - 1]) *
                    current_exponential + dt_2 * instrument_response_function[i];
            output[i] += current_model_value * a;
        }
    }
}

void Decay::convolve_lifetime_spectrum_periodic(
        double* output, int n_output,
        double* time_axis, int n_time_axis,
        double *instrument_response_function, int n_instrument_response_function,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop,
        bool use_amplitude_threshold,
        double amplitude_threshold,
        double period
){
    convolution_stop = convolution_stop > 0 ?
                       MIN(n_time_axis, MIN(n_instrument_response_function,
                    MIN(n_output, convolution_stop))) :
                       MIN(n_time_axis, MIN(n_instrument_response_function, n_output));
    int number_of_exponentials = n_lifetime_spectrum / 2;
    double dt = time_axis[1] - time_axis[0];
    double dt_2 = dt / 2;
    int period_n = std::ceil(period / dt - 0.5);
    int stop1 = MIN(convolution_stop, period_n);
    for(int i=convolution_start; i<convolution_stop; i++) output[i] = 0;
#if VERBOSE
    std::clog << "convolve_lifetime_spectrum_periodic..." << std::endl;
    std::clog << "-- number_of_exponentials: " << number_of_exponentials << std::endl;
    std::clog << "-- convolution_start: " << convolution_start << std::endl;
    std::clog << "-- convolution_stop: " << convolution_stop << std::endl;
    std::clog << "-- use_amplitude_threshold: " << use_amplitude_threshold << std::endl;
    std::clog << "-- amplitude_threshold: " << amplitude_threshold << std::endl;
    std::clog << "-- period: " << period << std::endl;
#endif
    if(use_amplitude_threshold){
        for(int ne = 0; ne<number_of_exponentials; ne++){
            double a = std::abs(lifetime_spectrum[2 * ne]);
            lifetime_spectrum[2 * ne] *= (a < amplitude_threshold);
        }
    }
    for(int ne=0; ne < number_of_exponentials; ne++){
        double x_curr = lifetime_spectrum[2 * ne];
        if(x_curr == 0.0) continue;
        double lt_curr = lifetime_spectrum[2 * ne + 1];
        double tail_a = 1./(1.-exp(-period/lt_curr));
        double fit_curr = 0.;
        double exp_curr = std::exp(-dt/lt_curr);
        output[0] += dt_2 * instrument_response_function[0] * (exp_curr + 1.) * x_curr;
        for(int i=0; i<convolution_stop; i++){
            fit_curr = (fit_curr + dt_2 * instrument_response_function[i - 1]) *
                    exp_curr + dt_2 * instrument_response_function[i];
            output[i] += fit_curr * x_curr;
        }

        for(int i=convolution_stop; i<stop1; i++){
            fit_curr *= exp_curr;
            output[i] += fit_curr * x_curr;
        }

        fit_curr *= exp(-(period_n - stop1) * dt / lt_curr);
        for(int i=0; i < convolution_stop; i++) {
            fit_curr *= exp_curr;
            output[i] += fit_curr * x_curr * tail_a;
        }
    }
}


double Decay::compute_scale(
        double* model_function, int n_model_function,
        double* data, int n_data,
        double* weights, int n_weights,
        double background,
        int start,
        int stop
){
    start = MIN(MAX(start, 0), n_model_function);
    stop = (stop <0)? n_model_function: stop;
#if VERBOSE
    std::clog << "scale_to_data..." << std::endl;
    std::clog << "-- background: " << background << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
    std::clog << "-- n_model_function: " << n_model_function << std::endl;
    std::clog << "-- n_data: " << n_data << std::endl;
    std::clog << "-- n_weights: " << n_weights << std::endl;
#endif
    double scale = 0.0;
    double sum_nom = 0.0;
    double sum_denom = 0.0;
    for(int i=start; i<stop; i++){
        if(data[i] > 0){
            double iwsq = 1.0 / (weights[i] * weights[i] + 1e-12);
            sum_nom += model_function[i] * (data[i] - background) * iwsq;
            sum_denom += model_function[i] * model_function[i] * iwsq;
        }
    }
    if(sum_denom > 0){
        return sum_nom / sum_denom;
    } else{
        return 1.0;
    }
}


void Decay::shift_array(
        double* input, int n_input,
        double** output, int *n_output,
        double shift,
        bool set_outside,
        double outside_value
){
    int ts_i = (int) shift < 0 ? n_input + shift: shift;
    double ts_f = shift - std::floor(shift);
#if VERBOSE
    std::clog << "shift_array..." << std::endl;
    std::clog << "-- n_input: " << n_input << std::endl;
    std::clog << "-- shift: " << shift << std::endl;
    std::clog << "-- shift integer: " << ts_i << std::endl;
    std::clog << "-- shift float: " << ts_f << std::endl;
#endif
    auto tmp = (double*) malloc(n_input * sizeof(double));
    std::vector<double> tmp1(input, input+n_input);
    std::vector<double> tmp2(input, input+n_input);
    std::rotate(tmp1.begin(), tmp1.begin()+ts_i, tmp1.end());
    std::rotate(tmp2.begin(), tmp2.begin()+(ts_i + 1), tmp2.end());
    for(int i=0; i < n_input; i++){
        tmp[i] = tmp1[i] * (1.0 - ts_f) + tmp2[i] * ts_f;
    }
    if(set_outside){
        int b = std::ceil(shift);
        if(shift > 0){
            for(int i=0; i<b; i++)
                tmp[i] = outside_value;
        } else if(shift<0){
            for(int i = n_input - b; i<n_input; i++)
                tmp[i] = outside_value;
        }
    }
    *output = tmp;
    *n_output = n_input;
}


void Decay::add_curve(
        double** output, int *n_output,
        double* curve1, int n_curve1,
        double* curve2, int n_curve2,
        double areal_fraction_curve2,
        int start,
        int stop
){
#if VERBOSE
    std::clog << "add_curve..." << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
    std::clog << "-- areal_fraction_curve2: " << areal_fraction_curve2 << std::endl;
#endif
    *n_output = MIN(n_curve1, n_curve2);
    start = MIN(0, start);
    stop = stop < 0? *n_output: MIN(*n_output, stop);
    auto tmp  = (double*) malloc(*n_output * sizeof(double));
    auto tmp1 = (double*) malloc(n_curve1 * sizeof(double));
    auto tmp2 = (double*) malloc(n_curve2 * sizeof(double));
    double sum_curve_1 = std::accumulate( curve1 + start, curve1 + stop, 0.0);
    double sum_curve_2 = std::accumulate( curve2 + start, curve2 + stop, 0.0);
    for(int i=0; i<n_curve1;i++) tmp1[i]  = curve1[i] / sum_curve_1;
    for(int i=0; i<n_curve2;i++) tmp2[i]  = curve2[i] / sum_curve_2;
    for(int i=start; i<stop;i++)
        tmp[i] = ((1. - areal_fraction_curve2) * tmp1[i] + areal_fraction_curve2 * tmp2[i]) *
                 sum_curve_1;
    *output = tmp;
}

void Decay::add_pile_up(
    double* model, int n_model,
    double* data, int n_data,
    double repetition_rate,
    double dead_time,
    double measurement_time
        ){
#if VERBOSE
    std::clog << "add_pile_up..." << std::endl;
    std::clog << "-- repetition_rate: " << repetition_rate << std::endl;
    std::clog << "-- dead_time: " << dead_time << std::endl;
    std::clog << "-- measurement_time: " << measurement_time << std::endl;
    std::clog << "-- n_data: " << n_data << std::endl;
    std::clog << "-- n_model: " << n_model << std::endl;
#endif
    repetition_rate *= 1e6;
    dead_time *= 1e-9;
    std::vector<double> cum_sum(n_data);
//    std::partial_sum(data, data+n_data, cum_sum);
//    int n_pulse_detected = (int) cum_sum[cum_sum.size() - 1];
//    double total_dead_time = n_pulse_detected * dead_time;
//    double live_time = measurement_time - total_dead_time;
//    int n_excitation_pulses = MAX(live_time * repetition_rate, n_pulse_detected);

}

void Decay::compute_decay(
        double* model_function, int n_model_function,
        double* data, int n_data,
        double* weights, int n_weights,
        double* time_axis, int n_time_axis,
        double* instrument_response_function, int n_instrument_response_function,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int start, int stop,
        double irf_background_counts,
        double irf_shift_channels,
        double irf_areal_fraction,
        double period,
        double constant_background,
        double total_area,
        bool use_amplitude_threshold,
        double amplitude_threshold,
        bool correct_pile_up
){
#if VERBOSE
    std::clog << "compute_decay..." << std::endl;
    std::clog << "-- n_model_function: " << n_model_function << std::endl;
    std::clog << "-- n_data: " << n_data << std::endl;
    std::clog << "-- n_weights: " << n_weights << std::endl;
    std::clog << "-- n_time_axis: " << n_time_axis << std::endl;
    std::clog << "-- n_instrument_response_function: " << n_instrument_response_function << std::endl;
    std::clog << "-- n_lifetime_spectrum: " << n_lifetime_spectrum << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
    std::clog << "-- irf_background_counts: " << irf_background_counts << std::endl;
    std::clog << "-- irf_shift_channels: " << irf_shift_channels << std::endl;
    std::clog << "-- irf_areal_fraction: " << irf_areal_fraction << std::endl;
    std::clog << "-- period: " << period << std::endl;
    std::clog << "-- constant_background: " << constant_background << std::endl;
    std::clog << "-- total_area: " << total_area << std::endl;
    std::clog << "-- use_amplitude_threshold: " << use_amplitude_threshold << std::endl;
    std::clog << "-- amplitude_threshold: " << amplitude_threshold << std::endl;
    std::clog << "-- correct_pile_up: " << correct_pile_up << std::endl;
#endif
    // correct irf for background counts
    std::vector<double> irf_bg_corrected;
    irf_bg_corrected.resize(n_instrument_response_function);
    for(int i=0; i < n_instrument_response_function; i++){
        irf_bg_corrected[i] =
                MAX(0, instrument_response_function[i] - irf_background_counts);
    }

    // shift irf
    double* irf_bg_shift_corrected; int n_irf_bg_shift_corrected;
    shift_array(
        irf_bg_corrected.data(), irf_bg_corrected.size(),
        &irf_bg_shift_corrected, &n_irf_bg_shift_corrected,
        irf_shift_channels
    );

    // convolve lifetime spectrum with irf
    convolve_lifetime_spectrum_periodic(
            model_function, n_model_function,
            time_axis, n_time_axis,
            irf_bg_shift_corrected, n_irf_bg_shift_corrected,
            lifetime_spectrum, n_lifetime_spectrum,
            start, stop,
            use_amplitude_threshold, amplitude_threshold,
            period
    );
    free(irf_bg_shift_corrected);

    // add scatter fraction (irf)
    double* decay_irf; int n_decay_irf;
    add_curve(
            &decay_irf, &n_decay_irf,
            model_function, n_model_function,
            irf_bg_corrected.data(), irf_bg_corrected.size(),
            irf_areal_fraction,
            start, stop
    );

    // scale model function
    double scale = 1.0;
    if(total_area < 0){
        // scale the area to the data in the range start, stop
        scale = compute_scale(
            decay_irf, n_decay_irf,
            data, n_data,
            weights, n_weights,
            constant_background,
            start, stop
        );
    } else{
        // normalize model to total_area
        double sum = std::accumulate( decay_irf + start, decay_irf + stop, 0.0);
        scale = total_area / sum;
    }
    for(int i=0; i<n_decay_irf;i++)
        model_function[i] = decay_irf[i] * scale + constant_background;
}


