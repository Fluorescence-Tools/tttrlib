#include "fsconv.h"

/* rescaling -- old version. sum(fit)->sum(decay) */
void rescale(double *fit, double *decay, double *scale, int start, int stop) {
    /* scaling */
    if (*scale == 0.) {
        double sumfit = 0., sumcurve = 0.;
        for (int i = start; i < stop; i++) {
            sumfit += fit[i];
            sumcurve += decay[i];
        }
        if (sumfit != 0.) *scale = sumcurve / sumfit;
    }
    for (int i = start; i < stop; i++)
        fit[i] *= *scale;
}

/* rescaling -- new version. scale = sum(fit*decay/w^2)/sum(fit^2/w^2) */
void rescale_w(double *fit, double *decay, double *w_sq, double *scale, int start, int stop) {
    /* scaling */
    if (*scale == 0.) {
        double sumnom = 0., sumdenom = 0.;
        for (int i = start; i < stop; i++) {
            if (decay[i] != 0.) {
                sumnom += fit[i] * decay[i] / w_sq[i];
                sumdenom += fit[i] * fit[i] / w_sq[i];
            }
        }
        if (sumdenom != 0.) *scale = sumnom / sumdenom;
    }
    for (int i = start; i < stop; i++)
        fit[i] *= *scale;

}

/* rescaling -- new version + background. scale = sum(fit*decay/w^2)/sum(fit^2/w^2) */
void rescale_w_bg(double *fit, double *decay, double *e_sq, double bg, double *scale, int start, int stop) {
    double sumnom = 0., sumdenom = 0.;
    for (int i = start; i < stop; i++) {
        if(decay[i] > 0){
            double iwsq = (e_sq[i]*e_sq[i]+1e-12);
            sumnom += fit[i] * (decay[i] - bg) * iwsq;
            sumdenom += fit[i] * fit[i] * iwsq;
        }
    }
    if (sumdenom != 0.) *scale = sumnom / sumdenom;
    for (int i = start; i < stop; i++)
        fit[i] *= *scale;
#if VERBOSE_TTTRLIB
    std::clog << "RESCALE_W_BG" << std::endl;
    std::clog << "w_sq [start:stop]: "; for(int i=start; i<stop; i++) std::clog << e_sq[i] << " "; std::clog << std::endl;
    std::clog << "decay [start:stop]: "; for(int i=start; i<stop; i++) std::clog << decay[i] << " "; std::clog << std::endl;
    std::clog << "fit [start:stop]: "; for(int i=start; i<stop; i++) std::clog << fit[i] << " "; std::clog << std::endl;
    std::clog << "-- sumnom: " << sumnom << std::endl;
    std::clog << "-- sumdenom: " << sumdenom << std::endl;
    std::clog << "-- final scale: " << *scale << std::endl;
#endif
}


// fast convolution - OK
void fconv(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {
    std::vector<double> l2(stop);
    start = std::max(1, start);
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];
    /* convolution */
    for (int ne = 0; ne < numexp; ne++) {
        double expcurr = exp(-dt / x[2 * ne + 1]);
        double a = x[2 * ne];
        double fitcurr = 0.0;
        fit[0] += l2[0] * a;
        for (int i = start; i < stop; i++) {
            fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            fit[i] += fitcurr * a;
        }
    }
}


// fast convolution AVX
void fconv_avx(double *fit, double *x, double *lamp, int numexp, int start, int stop, double dt) {

    #ifdef __AVX__
    
    int start1 = std::max(1, start);

    // make sure that there are always multiple of 4 in the lifetimes
    const int chunk_size = 4; // the number of lifetimes per AVX register
    int n_chunks = (int) std::ceil((double) numexp / chunk_size);
    int n_ele = n_chunks * chunk_size;

    // copy the interleaved lifetime spectrum to vectors
    auto *p = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(p, p + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) p[i] = x[2 * i + 0];

    auto *ex = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(ex, ex + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) ex[i] = exp(-dt / x[2 * i + 1]);

    // precompute have lamp steps in units of dt
    auto l2 = (double *) malloc(stop * sizeof(double));
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    std::fill(fit, fit + stop, 0.0);
    __m256d e, a, fitcurr, l2p, l2c, tmp;
    for (int ne = 0; ne < numexp; ne += chunk_size) {
        // expcurr = exp(-dt / x[2 * ne + 1]);
        e = _mm256_load_pd(&ex[ne]);
        // amplitudes
        a = _mm256_load_pd(&p[ne]);
        // take care of first channel
        // fit[0] += l2[0] * a;
        l2c = _mm256_set1_pd(l2[0]);
        tmp = _mm256_mul_pd(l2c, a);
#ifdef _WIN32
        fit[0] += double(tmp.m256d_f64[0]);
        fit[0] += double(tmp.m256d_f64[1]);
        fit[0] += double(tmp.m256d_f64[2]);
        fit[0] += double(tmp.m256d_f64[3]);
#else
        fit[0] += tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        fitcurr = _mm256_set1_pd(0.0);
        // convolution
        for (int i = start1; i < stop; i++) {
            l2p = _mm256_set1_pd(l2[i - 1]);
            l2c = _mm256_set1_pd(l2[i]);
            //fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            fitcurr = _mm256_add_pd(fitcurr, l2p);
#ifdef __FMA__
            fitcurr = _mm256_fmadd_pd(fitcurr, e, l2c);
#else
            fitcurr = _mm256_mul_pd(fitcurr, e);
            fitcurr = _mm256_add_pd(fitcurr, l2c);
#endif
            // fit[i] += fitcurr * a;
            tmp = _mm256_mul_pd(fitcurr, a);
#ifdef _WIN32
            fit[i] = double(tmp.m256d_f64[0]);
            fit[i] += double(tmp.m256d_f64[1]);
            fit[i] += double(tmp.m256d_f64[2]);
            fit[i] += double(tmp.m256d_f64[3]);
#else
            fit[i] = tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        }
    }
    _mm_free(ex); _mm_free(p);

    #endif //__AVX__
}



/* fast convolution, high repetition rate */
void fconv_per(double *fit, double *x, double *lamp, int numexp, int start, int stop,
               int n_points, double period, double dt)
{
    stop = (stop < 0) ? n_points: stop;
    int period_n = (int)ceil(period/dt-0.5);

    int lamp_start = 0;
    while(lamp[lamp_start++]==0);

    int start1 = std::max(1, start);
    int stop1 = std::min(period_n+lamp_start, n_points);

    #if VERBOSE_TTTRLIB
    std::clog << "FCONV_PER" << std::endl;
    std::clog << "-- numexp:" << numexp << std::endl;
    std::clog << "-- start:" << start << std::endl;
    std::clog << "-- stop:" << stop << std::endl;
    std::clog << "-- n_points:" << n_points << std::endl;
    std::clog << "-- period:" << period << std::endl;
    std::clog << "-- dt:" << dt << std::endl;
    #endif

    // Precompute everything needed for the convolution
    // lamp * dt * 0.5
    auto l2 = (double *) malloc(stop * sizeof(double));
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    /* convolution */
    for (int ne=0; ne<numexp; ne++) {
        double expcurr = exp(-dt/x[2*ne+1]);
        double tail_a = 1./(1.-exp(-period/x[2*ne+1]));
        double fitcurr = 0;
        fit[0] += (fitcurr + l2[0]);
        for (int i=start1; i<stop1; i++){
            fitcurr=(fitcurr + l2[i - 1])*expcurr + l2[i];
            fit[i] += fitcurr*x[2*ne];
        }
        fitcurr *= exp(-(period_n - stop1 + start)*dt/x[2*ne+1]);
        for (int i=start; i<stop; i++){
            fitcurr *= expcurr;
            fit[i] += fitcurr*x[2*ne]*tail_a;
        }
    }
    free(l2);
}


// fast convolution, high repetition rate, AVX
void fconv_per_avx(double *fit, double *x, double *lamp, int numexp, int start, int stop,
                   int n_points, double period, double dt) {
#ifdef __AVX__

#if VERBOSE_TTTRLIB
    std::clog << "FCONV_PER_AVX" << std::endl;
    std::clog << "-- numexp: " << numexp << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
    std::clog << "-- n_points: " << n_points << std::endl;
    std::clog << "-- period: " << period << std::endl;
    std::clog << "-- dt: " << dt << std::endl;
#endif
    int start1 = std::max(1, start);
    stop = (stop < 0) ? n_points: stop;
    // make sure that there are always multiple of the AVX register size
    const int chunk_size = 4; // the number of lifetimes per AVX register
    int n_chunks = (int) std::ceil((double) numexp / chunk_size);
    int n_ele = n_chunks * chunk_size;

    // Number of time channels in period
    int period_n = (int)ceil(period/dt-0.5);

    // Check if the window is larger than the decay histogram.
    // If it is larger only convolve till the end of the decay. Otherwise,
    // convolve till end of period. The period starts at the
    // excitation pulse.
    // Find the position where the IRF starts
    int lamp_start = 0;
    while (lamp[lamp_start++] == 0);
    int stop1 = std::min(period_n+lamp_start, n_points);

    // Precompute everything needed for the convolution
    // lamp * dt * 0.5
    auto l2 = (double *) malloc(stop * sizeof(double));
    for (int i = 0; i < stop; i++) l2[i] = dt * 0.5 * lamp[i];

    // exponential
    auto ex = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(ex, ex + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) ex[i] = exp(-dt / x[2 * i + 1]);

    // amplitudes
    auto p = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(p, p + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) p[i] = x[2 * i];

    // scale of decay relative to tail
    auto scale = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(scale, scale + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) scale[i] = exp(-(period_n - stop1 + start) * dt / x[2 * i + 1]);

    // tails wrapping to next period
    auto tails = (double *) _mm_malloc(n_ele * sizeof(double), 32);
    std::fill(tails, tails + n_ele, 0.0);
    for (int i = 0; i < numexp; i++) tails[i] = 1. / (1. - exp(-period / x[2 * i + 1]));

    // CONVOLUTION
    std::fill(fit, fit + n_points, 0.0);
    __m256d fitcurr, l2p, l2c, a, e, s, t, tmp;
    for (int ne = 0; ne < numexp; ne += chunk_size) {
        e = _mm256_load_pd(&ex[ne]);     // expcurr = exp(-dt / x[2 * ne + 1]);
        a = _mm256_load_pd(&p[ne]);      // amplitudes
        s = _mm256_load_pd(&scale[ne]);  // scales
        t = _mm256_load_pd(&tails[ne]);  // tail

        // take care of first channel
        // fit[0] += l2[0] * a;
        l2c = _mm256_set1_pd(l2[0]);
        tmp = _mm256_mul_pd(l2c, a);
#ifdef _WIN32
        fit[0] += double(tmp.m256d_f64[0]);
        fit[0] += double(tmp.m256d_f64[1]);
        fit[0] += double(tmp.m256d_f64[2]);
        fit[0] += double(tmp.m256d_f64[3]);
#else
        fit[0] += tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        fitcurr = _mm256_set1_pd(0.0);
        for (int i = start1; i < stop1; i++) {
            //fitcurr = (fitcurr + l2[i - 1]) * expcurr + l2[i];
            int pre = std::max(0, i - 1);

            l2p = _mm256_set1_pd(l2[pre]);
            l2c = _mm256_set1_pd(l2[i]);
            fitcurr = _mm256_add_pd(fitcurr, l2p);
#ifdef __FMA__
            fitcurr = _mm256_fmadd_pd(fitcurr, e, l2c);
#else
            fitcurr = _mm256_mul_pd(fitcurr, e);
            fitcurr = _mm256_add_pd(fitcurr, l2c);
#endif
            // fit[i] += fitcurr * a;
            tmp = _mm256_mul_pd(fitcurr, a);
#ifdef _WIN32
            fit[i] = double(tmp.m256d_f64[0]);
            fit[i] += double(tmp.m256d_f64[1]);
            fit[i] += double(tmp.m256d_f64[2]);
            fit[i] += double(tmp.m256d_f64[3]);
#else
            fit[i] = tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        }
        // fitcurr *= scale[ne];
        fitcurr = _mm256_mul_pd(fitcurr, s);
        // tail
        for (int i = start; i < stop; i++) {
            //fitcurr *= e[ne];
            fitcurr = _mm256_mul_pd(fitcurr, e);
            //fit[i] += fitcurr * a[ne] * tails[ne];
            tmp = _mm256_mul_pd(fitcurr, a);
            tmp = _mm256_mul_pd(tmp, t);
#ifdef _WIN32
            fit[i] += double(tmp.m256d_f64[0]);
            fit[i] += double(tmp.m256d_f64[1]);
            fit[i] += double(tmp.m256d_f64[2]);
            fit[i] += double(tmp.m256d_f64[3]);
#else
            fit[i] += tmp[0] + tmp[1] + tmp[2] + tmp[3];
#endif
        }
    }
    free(l2); _mm_free(p); _mm_free(ex); _mm_free(scale); _mm_free(tails);
    
    #endif //__AVX__
}


/* fast convolution, high repetition rate, with convolution stop for Paris */
/* fast convolution, high repetition rate, with convolution stop for Paris */
void fconv_per_cs(double *fit, double *x, double *lamp, int numexp, int stop,
                  int n_points, double period, int conv_stop, double dt)
{
    int ne, i,
            stop1, period_n = (int)ceil(period/dt-0.5);
    double fitcurr, expcurr, tail_a, deltathalf = dt*0.5;

    for (i=0; i<=stop; i++) fit[i]=0;
    stop1 = (period_n > n_points-1) ? n_points-1 : period_n;

    /* convolution */
    for (ne=0; ne<numexp; ne++) {
        expcurr = exp(-dt/x[2*ne+1]);
        tail_a = 1./(1.-exp(-period/x[2*ne+1]));
        fitcurr = 0.;
        fit[0] += deltathalf*lamp[0]*(expcurr + 1.)*x[2*ne];
        for (i=1; i<=conv_stop; i++) {
            fitcurr=(fitcurr + deltathalf*lamp[i-1])*expcurr + deltathalf*lamp[i];
            fit[i] += fitcurr*x[2*ne];
        }
        for (; i<=stop1; i++) {
            fitcurr=fitcurr*expcurr;
            fit[i] += fitcurr*x[2*ne];
        }
        fitcurr *= exp(-(period_n - stop1)*dt/x[2*ne+1]);
        for (i=0; i<=stop; i++) {
            fitcurr *= expcurr;
            fit[i] += fitcurr*x[2*ne]*tail_a;
        }
    }
}


/* fast convolution with reference compound decay */
void fconv_ref(double *fit, double *x, double *lamp, int numexp, int start, int stop, double tauref, double dt) {
    double deltathalf = dt * 0.5, sum_a = 0;
    for (int i = 0; i < stop; i++) fit[i] = 0;
    /* convolution */
    for (int ne = 0; ne < numexp; ne++) {
        double expcurr = exp(-dt / x[2 * ne + 1]);
        double correct_a = x[2 * ne] * (1 / tauref - 1 / x[2 * ne + 1]);
        sum_a += x[2 * ne];
        double fitcurr = 0;
        for (int i = 1; i < stop; i++) {
            fitcurr = (fitcurr + deltathalf * lamp[i - 1]) * expcurr + deltathalf * lamp[i];
            fit[i] += fitcurr * correct_a;
        }
    }
    for (int i = 1; i < stop; i++) fit[i] += lamp[i] * sum_a;
}

/* slow convolution */
void sconv(double *fit, double *p, double *lamp, int start, int stop) {
    int i, j;
    /* convolution */
    for (i = start; i < stop; i++) {
        fit[i] = 0.5 * lamp[0] * p[i];
        for (j = 1; j < i; j++) fit[i] += lamp[j] * p[i - j];
        fit[i] += 0.5 * lamp[i] * p[0];
        fit[i] = fit[i];
    }
    fit[0] = 0;
}


/* shifting lamp */
void shift_lamp(double *lampsh, double *lamp, double ts, int n_points, double out_value) {
    int tsint = (int) (floor(ts));
    double tsdbl = ts - (double) tsint;
    int out_left = 0, out_right = 0, j;

    if (tsint < 0) out_left = -tsint;
    if (tsint + 1 > 0) out_right = tsint + 1;

    for (j = 0; j < out_left; j++) lampsh[j] = out_value;
    for (j = out_left; j < (n_points - out_right); j++)
        lampsh[j] = lamp[j + tsint] * (1 - tsdbl) + lamp[j + tsint + 1] * (tsdbl);
    for (j = (n_points - out_right); j < n_points; j++) lampsh[j] = out_value;

}


void add_pile_up_to_model(
        double* model, int n_model,
        double* data, int n_data,
        double repetition_rate,
        double instrument_dead_time,
        double measurement_time,
        std::string pile_up_model,
        int start,
        int stop
){
    stop = stop < 0 ? n_data : std::min(n_data, stop);
    start = start < 0 ? 0 : std::min(n_data, start);
    stop = std::min(n_data, n_model);
#if VERBOSE_TTTRLIB
    std::clog << "ADD PILE-UP" << std::endl;
    std::clog << "-- Repetition_rate [MHz]: " << repetition_rate << std::endl;
    std::clog << "-- Dead_time [ns]: " << instrument_dead_time << std::endl;
    std::clog << "-- Measurement_time [s]: " << measurement_time << std::endl;
    std::clog << "-- n_data: " << n_data << std::endl;
    std::clog << "-- n_model: " << n_model << std::endl;
    std::clog << "-- start: " << start << std::endl;
    std::clog << "-- stop: " << stop << std::endl;
#endif
    if(strcmp(pile_up_model.c_str(), "coates") == 0){
#if VERBOSE_TTTRLIB
        std::clog << "-- pile_up_model: " << pile_up_model << std::endl;
#endif
        repetition_rate *= 1e6;
        instrument_dead_time *= 1e-9;
        std::vector<double> cum_sum(n_data);
        std::partial_sum(data, data + n_data, cum_sum.begin(), std::plus<double>());
        long n_pulse_detected = (long) cum_sum[cum_sum.size() - 1];
        double total_dead_time = n_pulse_detected * instrument_dead_time;
        double live_time = measurement_time - total_dead_time;
        double n_excitation_pulses = std::max(live_time * repetition_rate, (double) n_pulse_detected);
#if VERBOSE_TTTRLIB
        std::clog << "-- live_time [s]: " << live_time << std::endl;
        std::clog << "-- total_dead_time [s]: " << total_dead_time << std::endl;
        std::clog << "-- n_pulse_detected [#]: " << n_pulse_detected << std::endl;
        std::clog << "-- n_excitation_pulses [#]: " << n_excitation_pulses << std::endl;
#endif
        // Coates, 1968, eq. 2 & 4
        std::vector<double> rescaled_data(n_data);

        for(int i = start; i < stop; i++)
            rescaled_data[i] = -std::log(1.0 - data[i] / (n_excitation_pulses - cum_sum[i]));
        for(int i = start; i < stop; i++)
            rescaled_data[i] = (rescaled_data[i] == 0) ? 1.0 : rescaled_data[i];
        // rescale model function to preserve data counting statistics
        std::vector<double> sf(n_data);
        for(int i = start; i < stop; i++)
            sf[i] = data[i] / rescaled_data[i];
        double s = std::accumulate(sf.begin(),sf.end(),0.0);
        for(int i = start; i < stop; i++)
            model[i] = model[i] * (sf[i] / s * n_data);
    }
}


void discriminate_small_amplitudes(
        double* lifetime_spectrum, int n_lifetime_spectrum,
        double amplitude_threshold
){
    int number_of_exponentials = n_lifetime_spectrum / 2;
#if VERBOSE_TTTRLIB
    std::clog << "APPLY_AMPLITUDE_THRESHOLD" << std::endl;
    std::clog << "-- amplitude_threshold spectrum: " << amplitude_threshold << std::endl;
    std::clog << "-- lifetime spectrum before: ";
    for (int i=0; i < number_of_exponentials * 2; i++){
        std::clog << lifetime_spectrum[i] << ' ';
    }
    std::clog << std::endl;
#endif
    for(int ne = 0; ne<number_of_exponentials; ne++){
        double amplitude = lifetime_spectrum[2 * ne];
        if(std::abs(amplitude) < amplitude_threshold){
            lifetime_spectrum[2 * ne] = 0.0;
        }
    }
#if VERBOSE_TTTRLIB
    std::clog << "-- lifetime spectrum after: ";
    for (int i=0; i < number_of_exponentials * 2; i++){
        std::clog << lifetime_spectrum[i] << ' ';
    }
    std::clog << std::endl;
#endif
}


/* fast convolution, high repetition rate, with time axis */
void fconv_per_cs_time_axis(
        double* model, int n_model,
        double* time_axis, int n_time_axis,
        double *irf, int n_irf,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop,
        double period
){
    double dt = time_axis[1] - time_axis[0];
#ifdef __AVX__
    fconv_per_avx(
            model, lifetime_spectrum, irf, (int) n_lifetime_spectrum / 2,
            convolution_start, convolution_stop, n_model, period, dt
    );
#endif
#ifndef __AVX__
    fconv_per(
            model, lifetime_spectrum, irf, (int) n_lifetime_spectrum / 2,
            convolution_start, convolution_stop, n_model, period, dt
    );
#endif
}


/* fast convolution, high repetition rate, with time axis */
void fconv_cs_time_axis(
        double* output, int n_output,
        double* time_axis, int n_time_axis,
        double *irf, int n_irf,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop
){
    double dt = time_axis[1] - time_axis[0];
#ifdef __AVX__
    fconv_avx(
            output,
            lifetime_spectrum,
            irf,
            (int) n_lifetime_spectrum / 2,
            convolution_start, convolution_stop, dt
    );
#endif
#ifndef __AVX__
    fconv(
            output,
            lifetime_spectrum,
            irf,
            (int) n_lifetime_spectrum / 2,
            convolution_start, convolution_stop, dt
    );
#endif
}


void fconv_cs_time_axis_old(
        double* output, int n_output,
        double* time_axis, int n_time_axis,
        double *irf, int n_irf,
        double* lifetime_spectrum, int n_lifetime_spectrum,
        int convolution_start,
        int convolution_stop
){
    int number_of_exponentials = n_lifetime_spectrum / 2;
#if VERBOSE_TTTRLIB
    std::clog << "convolve_lifetime_spectrum... " << std::endl;
    std::clog << "-- number_of_exponentials: " << number_of_exponentials << std::endl;
    std::clog << "-- convolution_start: " << convolution_start << std::endl;
    std::clog << "-- convolution_stop: " << convolution_stop << std::endl;
#endif
    for(int ne=0; ne<number_of_exponentials; ne++){
        double a = lifetime_spectrum[2 * ne];
        double current_lifetime = (lifetime_spectrum[2 * ne + 1]);
        if((a == 0.0) || (current_lifetime == 0.0)) continue;
        double current_model_value = 0.0;
        for(int i = convolution_start; i < convolution_stop; i++){
            double dt;
            int pre = std::max(0, i - 1);
            if(i < convolution_stop - 1){
                dt = (time_axis[i + 1] - time_axis[i]);
            } else{
                dt = (time_axis[i] - time_axis[i - 1]);
            }
            double dt_2 = dt / 2.0;
            double current_exponential = std::exp(-dt / current_lifetime);
            current_model_value = (current_model_value + dt_2 * irf[pre]) *
                                  current_exponential + dt_2 * irf[i];
            output[i] += current_model_value * a;
        }
    }
}
