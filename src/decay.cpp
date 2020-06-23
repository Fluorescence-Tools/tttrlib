#include "decay.h"

//// moved to fit2x
//void Decay::convolve_lifetime_spectrum(
//        double* output, int n_output,
//        double* time_axis, int n_time_axis,
//        double *instrument_response_function, int n_instrument_response_function,
//        double* lifetime_spectrum, int n_lifetime_spectrum,
//        int convolution_start,
//        int convolution_stop,
//        bool use_amplitude_threshold,
//        double amplitude_threshold
//){
//    int number_of_exponentials = n_lifetime_spectrum / 2;
//    convolution_stop = convolution_stop > 0 ?
//            std::min(n_time_axis, std::min(n_instrument_response_function,
//                                      std::min(n_output, convolution_stop))) :
//                       std::min(n_time_axis, std::min(n_instrument_response_function, n_output));
//    convolution_start = std::max(convolution_start, 1);
//#if VERBOSE
//    std::clog << "convolve_lifetime_spectrum... " << std::endl;
//    std::clog << "-- number_of_exponentials: " << number_of_exponentials << std::endl;
//    std::clog << "-- convolution_start: " << convolution_start << std::endl;
//    std::clog << "-- convolution_stop: " << convolution_stop << std::endl;
//    std::clog << "-- use_amplitude_threshold: " << use_amplitude_threshold << std::endl;
//    std::clog << "-- amplitude_threshold: " << amplitude_threshold << std::endl;
//#endif
//    for(int i=0; i<n_output; i++) output[i] = 0.0;
//    if(use_amplitude_threshold){
//        for(int ne = 0; ne<number_of_exponentials; ne++){
//            double a = std::abs(lifetime_spectrum[2 * ne]);
//            lifetime_spectrum[2 * ne] *= (a < amplitude_threshold);
//        }
//    }
//    for(int ne=0; ne<number_of_exponentials; ne++){
//        double a = lifetime_spectrum[2 * ne];
//        double current_lifetime = (lifetime_spectrum[2 * ne + 1]);
//        if((a == 0.0) || (current_lifetime == 0.0)) continue;
//        double current_model_value = 0.0;
//        for(int i=convolution_start; i<convolution_stop; i++){
//            double dt = dt = (time_axis[i] - time_axis[i - 1]);
//            double dt_2 = dt / 2.0;
//            double current_exponential = std::exp(-dt / current_lifetime);
//            current_model_value = (current_model_value + dt_2 * instrument_response_function[i - 1]) *
//                    current_exponential + dt_2 * instrument_response_function[i];
//            output[i] += current_model_value * a;
//        }
//    }
//}


/// moved to fit2x
//void Decay::convolve_lifetime_spectrum_periodic(
//        double* output, int n_output,
//        double* time_axis, int n_time_axis,
//        double *instrument_response_function, int n_instrument_response_function,
//        double* lifetime_spectrum, int n_lifetime_spectrum,
//        int convolution_start,
//        int convolution_stop,
//        bool use_amplitude_threshold,
//        double amplitude_threshold,
//        double period
//){
//    convolution_stop = convolution_stop > 0 ?
//                       std::min(n_time_axis, std::min(n_instrument_response_function, std::min(n_output, convolution_stop))) :
//                       std::min(n_time_axis, std::min(n_instrument_response_function, n_output));
//    int number_of_exponentials = n_lifetime_spectrum / 2;
//    double dt = time_axis[1] - time_axis[0];
//    double dt_2 = dt / 2;
//    int period_n = std::ceil(period / dt - 0.5);
//    int stop1 = std::min(convolution_stop, period_n);
//    for(int i=0; i<n_output; i++) output[i] = 0;
//    convolution_start = std::max(convolution_start, 1);
//#if VERBOSE
//    std::clog << "convolve_lifetime_spectrum_periodic..." << std::endl;
//    std::clog << "-- number_of_exponentials: " << number_of_exponentials << std::endl;
//    std::clog << "-- convolution_start: " << convolution_start << std::endl;
//    std::clog << "-- convolution_stop: " << convolution_stop << std::endl;
//    std::clog << "-- use_amplitude_threshold: " << use_amplitude_threshold << std::endl;
//    std::clog << "-- amplitude_threshold: " << amplitude_threshold << std::endl;
//    std::clog << "-- period: " << period << std::endl;
//#endif
//    if(use_amplitude_threshold){
//        for(int ne = 0; ne<number_of_exponentials; ne++){
//            lifetime_spectrum[2 * ne] *= (std::abs(lifetime_spectrum[2 * ne]) < amplitude_threshold);
//        }
//    }
//    for(int ne=0; ne < number_of_exponentials; ne++){
//        double x_curr = lifetime_spectrum[2 * ne];
//        if(x_curr == 0.0) continue;
//        double lt_curr = lifetime_spectrum[2 * ne + 1];
//        double tail_a = 1./(1.-exp(-period/lt_curr));
//        double fit_curr = 0.;
//        double exp_curr = std::exp(-dt/lt_curr);
//        output[0] += dt_2 * instrument_response_function[0] * (exp_curr + 1.) * x_curr;
//
//        for(int i=convolution_start; i<convolution_stop; i++){
//            fit_curr = (fit_curr + dt_2 * instrument_response_function[i - 1]) *
//                    exp_curr + dt_2 * instrument_response_function[i];
//            output[i] += fit_curr * x_curr;
//        }
//
//        for(int i=convolution_stop; i<stop1; i++){
//            fit_curr *= exp_curr;
//            output[i] += fit_curr * x_curr;
//        }
//
//        fit_curr *= exp(-(period_n - stop1) * dt / lt_curr);
//        for(int i=0; i < convolution_stop; i++) {
//            fit_curr *= exp_curr;
//            output[i] += fit_curr * x_curr * tail_a;
//        }
//    }
//}
//

//// moved to fit2x
//double Decay::compute_scale(
//        double* model_function, int n_model_function,
//        double* data, int n_data,
//        double* weights, int n_weights,
//        double background,
//        int start,
//        int stop
//){
//    start = std::min(std::max(start, 0), n_model_function);
//    stop = (stop <0)? n_model_function: stop;
//#if VERBOSE
//    std::clog << "scale_to_data..." << std::endl;
//    std::clog << "-- background: " << background << std::endl;
//    std::clog << "-- start: " << start << std::endl;
//    std::clog << "-- stop: " << stop << std::endl;
//    std::clog << "-- n_model_function: " << n_model_function << std::endl;
//    std::clog << "-- n_data: " << n_data << std::endl;
//    std::clog << "-- n_weights: " << n_weights << std::endl;
//#endif
//    double sum_nom = 0.0;
//    double sum_denom = 0.0;
//    for(int i=start; i<stop; i++){
//        if(data[i] > 0){
//            double iwsq = (weights[i] == 0)? 0.0: 1.0 / (weights[i] * weights[i]);
//            sum_nom += model_function[i] * (data[i] - background) * iwsq;
//            sum_denom += model_function[i] * model_function[i] * iwsq;
//        }
//    }
//    if(sum_denom > 0){
//        return sum_nom / sum_denom;
//    } else{
//        return 1.0;
//    }
//}
//

void Decay::shift_array(
        double* input, int n_input,
        double** output, int *n_output,
        double shift,
        bool set_outside,
        double outside_value
){
    // mod
    int r = (int) shift % n_input;
    int ts_i = r < 0 ? r + n_input : r;
    double ts_f = shift - std::floor(shift);
#if VERBOSE
    std::clog << "shift_array..." << std::endl;
    std::clog << "-- n_input: " << n_input << std::endl;
    std::clog << "-- shift: " << shift << std::endl;
    std::clog << "-- shift integer: " << ts_i << std::endl;
    std::clog << "-- shift float: " << ts_f << std::endl;
#endif
    auto tmp = (double*) calloc(n_input, sizeof(double));
    std::vector<double> tmp1(input, input+n_input);
    std::vector<double> tmp2(input, input+n_input);
    std::rotate(tmp1.begin(), tmp1.begin()+ts_i, tmp1.end());
    std::rotate(tmp2.begin(), tmp2.begin()+(ts_i + 1), tmp2.end());
    for(int i=0; i < n_input; i++){
        tmp[i] = tmp1[i] * (1.0 - ts_f) + tmp2[i] * ts_f;
    }
    if(set_outside){
        if(shift > 0){
            for(int i=0; i<r; i++)
                tmp[i] = outside_value;
        } else if(shift<0){
            for(int i = n_input - r; i<n_input; i++)
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
    *n_output = std::min(n_curve1, n_curve2);
    start = std::min(0, start);
    stop = stop < 0? *n_output: std::min(*n_output, stop);
    auto tmp  = (double*) malloc(*n_output * sizeof(double));
    auto tmp1 = (double*) malloc(n_curve1 * sizeof(double));
    auto tmp2 = (double*) malloc(n_curve2 * sizeof(double));
    double sum_curve_1 = std::accumulate( curve1 + start, curve1 + stop, 0.0);
    double sum_curve_2 = std::accumulate( curve2 + start, curve2 + stop, 0.0);
    for(int i=0; i<n_curve1;i++) tmp1[i]  = curve1[i] / sum_curve_1;
    for(int i=0; i<n_curve2;i++) tmp2[i]  = curve2[i] / sum_curve_2;
    for(int i=start; i<stop;i++)
        tmp[i] = (
                    (1. - areal_fraction_curve2) * tmp1[i] + areal_fraction_curve2 * tmp2[i]
                ) * sum_curve_1;
    *output = tmp;
}


void Decay::compute_decay(
        double* model_function, int n_model_function,
        double* data, int n_data,
        double* squared_weights, int n_weights,
        double* time_axis, int n_time_axis,
        double* instrument_response_function, int n_instrument_response_function,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int start, int stop,
        double irf_background_counts,
        double irf_shift_channels,
        double scatter_areal_fraction,
        double excitation_period,
        double constant_background,
        double total_area,
        bool use_amplitude_threshold,
        double amplitude_threshold,
        bool pile_up,
        double instrument_dead_time,
        double acquisition_time,
        bool add_corrected_irf,
        bool scale_model_to_area
){
    stop = stop > 0 ?
                   std::min(n_time_axis, std::min(n_instrument_response_function, std::min(n_model_function, stop))) :
                   std::min(n_time_axis, std::min(n_instrument_response_function, n_model_function));
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
    std::clog << "-- irf_areal_fraction: " << scatter_areal_fraction << std::endl;
    std::clog << "-- period: " << excitation_period << std::endl;
    std::clog << "-- constant_background: " << constant_background << std::endl;
    std::clog << "-- total_area: " << total_area << std::endl;
    std::clog << "-- use_amplitude_threshold: " << use_amplitude_threshold << std::endl;
    std::clog << "-- amplitude_threshold: " << amplitude_threshold << std::endl;
    std::clog << "-- correct_pile_up: " << pile_up << std::endl;
    std::clog << "-- add_corrected_irf: " << add_corrected_irf << std::endl;
#endif
    // correct irf for background counts
    auto irf_bg_corrected = std::vector<double>(n_instrument_response_function);
    for(int i=0; i < n_instrument_response_function; i++)
        irf_bg_corrected[i] = std::max(
                0.0, instrument_response_function[i] - irf_background_counts
        );

    // shift irf
    double* irf_bg_shift_corrected; int n_irf_bg_shift_corrected;
    shift_array(
        irf_bg_corrected.data(), irf_bg_corrected.size(),
        &irf_bg_shift_corrected, &n_irf_bg_shift_corrected,
        irf_shift_channels
    );

    // convolve lifetime spectrum with irf
    fconv_per_cs_time_axis(
            model_function, n_model_function,
            time_axis, n_time_axis,
            irf_bg_shift_corrected, n_irf_bg_shift_corrected,
            lifetime_spectrum, n_lifetime_spectrum,
            start, stop,
            use_amplitude_threshold, amplitude_threshold,
            excitation_period
    );
    free(irf_bg_shift_corrected);

    // add scatter fraction (irf)
    double* decay_irf; int n_decay_irf;
    if(add_corrected_irf){
        add_curve(
                &decay_irf, &n_decay_irf,
                model_function, n_model_function,
                irf_bg_shift_corrected, n_irf_bg_shift_corrected,
                scatter_areal_fraction,
                start, stop
        );
    } else{
        add_curve(
                &decay_irf, &n_decay_irf,
                model_function, n_model_function,
                instrument_response_function, n_instrument_response_function,
                scatter_areal_fraction,
                start, stop
        );
    }

    if(pile_up){
        add_pile_up(
            decay_irf, n_decay_irf,
            data, n_data,
            excitation_period, instrument_dead_time,
            acquisition_time
        );
    }

    // scale model function
    double scale = 1.0;
    if(scale_model_to_area){
        if(total_area < 0){
            // scale the area to the data in the range start, stop
            rescale_w_bg(decay_irf, data, squared_weights, constant_background, &scale, start, stop);
//            scale = compute_scale(
//                    decay_irf, n_decay_irf,
//                    data, n_data,
//                    weights, n_weights,
//                    constant_background,
//                    start, stop
//            );
        } else{
            // normalize model to total_area
            double sum = std::accumulate( decay_irf + start, decay_irf + stop, 0.0);
            scale = total_area / sum;
        }
    }
#if VERBOSE
    std::clog << "Adding Background" << std::endl;
    std::clog << "-- add constant background: " << constant_background << std::endl;
#endif
    for(int i=0; i<n_model_function;i++)
        model_function[i] = decay_irf[i] * scale + constant_background;
}


void Decay::compute_microtime_histogram(
        TTTR* tttr_data,
        double** histogram, int* n_histogram,
        double** time, int* n_time,
        unsigned short micro_time_coarsening
) {
    // construct histogram
    if (tttr_data != nullptr) {
        auto header = tttr_data->get_header();
        int n_channels =header.number_of_micro_time_channels / micro_time_coarsening;
        double micro_time_resolution = header.micro_time_resolution;
        unsigned short *micro_times; int n_micro_times;
        tttr_data->get_micro_time(&micro_times, &n_micro_times);
#if VERBOSE
        std::cout << "compute_histogram" << std::endl;
        std::cout << "-- micro_time_coarsening: " << micro_time_coarsening << std::endl;
        std::cout << "-- n_channels: " << n_channels << std::endl;
        std::cout << "-- micro_times[0]: " << micro_times[0] << std::endl;
#endif
        for(int i=0; i<n_micro_times;i++)
            micro_times[i] /= micro_time_coarsening;
#if VERBOSE
        std::cout << "-- n_micro_times: " << n_micro_times << std::endl;
        std::cout << "-- micro_times[0]: " << micro_times[0] << std::endl;
#endif
        auto bin_edges = std::vector<unsigned short>(n_channels);
        for (int i = 0; i < bin_edges.size(); i++) bin_edges[i] = i;
        auto hist = (double *) malloc(n_channels * sizeof(double));
        for(int i=0; i<n_channels;i++) hist[i] = 0.0;
        histogram1D<unsigned short>(
                micro_times, n_micro_times,
                nullptr, 0,
                bin_edges.data(), bin_edges.size(),
                hist, n_channels,
                "lin", false
        );
        *histogram = hist;
        *n_histogram = n_channels;

        auto t = (double *) malloc(n_channels * sizeof(double));
        for (int i = 0; i < n_channels; i++) t[i] = micro_time_resolution * i * micro_time_coarsening;
        *time = t;
        *n_time = n_channels;
        free(micro_times);
    }
}

double Decay::compute_mean_lifetime(
        TTTR* tttr_data,
        TTTR* tttr_irf,
        int m0_irf, int m1_irf
){
    if(tttr_irf != nullptr){
        unsigned short *micro_times_irf; int n_micro_times_irf;
        tttr_irf->get_micro_time(&micro_times_irf, &n_micro_times_irf);
        m0_irf = n_micro_times_irf;
        m1_irf = 0;
        for(int i=0; i< n_micro_times_irf; i++) m1_irf += micro_times_irf[i];
    }

    unsigned short *micro_times_data; int n_micro_times_data;
    tttr_data->get_micro_time(&micro_times_data, &n_micro_times_data);
    double mu0 = n_micro_times_data;
    double mu1 = 0.0;
    for(int i=0; i< n_micro_times_data; i++) mu1 += micro_times_data[i];

    double g1 = mu0 / m0_irf;
    double g2 = (mu1 - g1 * m1_irf) / m0_irf;
    double tau1 = g2 / g1;
    return tau1 * tttr_data->get_header().micro_time_resolution;
}


