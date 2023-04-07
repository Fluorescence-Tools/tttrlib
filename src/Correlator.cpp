#include <include/Correlator.h>


Correlator::Correlator(
        std::shared_ptr<TTTR> tttr,
        std::string method,
        int n_bins,
        int n_casc,
        bool make_fine
) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "CORRELATOR" << std::endl;
#endif
    curve.set_n_bins(n_bins);
    curve.set_n_casc(n_casc);
    if (tttr != nullptr) {
#ifdef VERBOSE_TTTRLIB
        std::clog << "-- Passed a TTTR object" << std::endl;
#endif
        set_tttr(tttr, tttr, make_fine);
    }
    set_correlation_method(method);
}

void Correlator::set_macrotimes(
        unsigned long long *t1v, int n_t1v,
        unsigned long long *t2v, int n_t2v
){
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Setting macro times..." << std::endl;
    std::clog << "-- n_t1v, n_t2v: " << n_t1v << "," << n_t2v << std::endl;
#endif
    is_valid = false;
    p1.times.resize(n_t1v);
    p2.times.resize(n_t1v);
    for(int i=0; i<n_t1v; i++) p1.times[i] = t1v[i];
    for(int i=0; i<n_t2v; i++) p2.times[i] = t2v[i];
}

void Correlator::set_weights(
        double* weight_ch1, int n_weights_ch1,
        double* weight_ch2, int n_weights_ch2
){
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Setting weights..." << std::endl;
    std::clog << "-- n_weights_ch1, n_weights_ch2: " <<
    n_weights_ch1 << "," << n_weights_ch2 << std::endl;
#endif
    is_valid = false;
    p1.resize(n_weights_ch1);
    p2.resize(n_weights_ch2);
    for(int i=0; i<n_weights_ch1; i++) p1.weights[i] = weight_ch1[i];
    for(int i=0; i<n_weights_ch2; i++) p2.weights[i] = weight_ch2[i];
}

void Correlator::set_events(
        unsigned long long  *t1, int n_t1,
        double* weight_ch1, int n_weights_ch1,
        unsigned long long  *t2, int n_t2,
        double* weight_ch2, int n_weights_ch2
){
    is_valid = false;
    p1.set_events(t1, n_t1, weight_ch1, n_weights_ch1);
    p2.set_events(t2, n_t2, weight_ch2, n_weights_ch2);
}

void Correlator::run(){
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Running correlator..." << std::endl;
    std::clog << "-- Correlation mode: " << correlation_method << std::endl;
    std::clog << "-- Filling correlation vectors with zero." << std::endl;
#endif
    if(is_valid){
#ifdef VERBOSE_TTTRLIB
        std::clog << "CORRELATOR::RUN" << std::endl;
        std::clog << "-- Results are already valid." << std::endl;
#endif
        return;
    } else {
        if(!p1.empty() && !p2.empty()){
            curve.clear();
            if (correlation_method == "wahl"){
                ccf_wahl(
                        get_n_casc(), get_n_bins(),
                        curve.x_axis, 
                        curve.correlation,
                        p1, p2
                );
            } else if (correlation_method == "felekyan") {
                ccf_felekyan(
                        (const unsigned long long *) p1.times.data(),
                        (const unsigned long long *) p2.times.data(),
                        p1.weights.data(), p2.weights.data(),
                        (unsigned int) curve.settings.n_bins,
                        (unsigned int) curve.settings.n_casc,
                        (unsigned int) p1.size(),
                        (unsigned int) p2.size(),
                        curve.x_axis.data(),
                        curve.correlation.data()
                );
            } else if (correlation_method == "laurence") {
                ccf_laurence(curve.x_axis, curve.correlation, p1, p2);
            } else{
                std::cerr << "WARNING: Correlation mode not recognized!" << std::endl;
            }
            normalize(this, this->curve);
        } else{
            std::cerr << "WARNING: No data to correlate!" << std::endl;
        }
        is_valid = true;
    }
}

void Correlator::set_microtimes(
        unsigned short* tac_1, int n_tac_1,
        unsigned short* tac_2, int n_tac_2,
        unsigned int number_of_microtime_channels
        ){
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Setting micro times..." << std::endl;
#endif
    is_valid = false;
    p1.make_fine(tac_1, n_tac_1, number_of_microtime_channels);
    p2.make_fine(tac_2, n_tac_2, number_of_microtime_channels);
    dt();
}

uint64_t Correlator::dt(){
    uint64_t dt1 = p1.dt();
    uint64_t dt2 = p2.dt();
    /// The maximum time of an event in the first and second correlation channel, max(t1, t2)
    uint64_t maximum_macro_time = std::max(dt1, dt2);
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Maximum time (Ch1): " << dt1 << std::endl;
    std::clog << "-- Maximum time (Ch2): " << dt2 << std::endl;
    std::clog << "-- Maximum time: " << maximum_macro_time << std::endl;
#endif
    return maximum_macro_time;
}

void Correlator::set_tttr(
        std::shared_ptr<TTTR> tttr_1,
        std::shared_ptr<TTTR> tttr_2,
        bool make_fine
){
    is_valid = false;
    p1.set_tttr(tttr_1, make_fine);
    if(tttr_2 == nullptr){
        p2.set_tttr(tttr_1, make_fine);
    } else{
        p2.set_tttr(tttr_2, make_fine);
    }
    if(p1.get_time_axis_calibration() != p2.get_time_axis_calibration()){
        std::cerr << "ERROR: Time axis calibration of photon streams do not match." << std::endl;
    } else{
        curve.settings.macro_time_duration = p1.time_axis_calibration;
    }
    dt();
}

void Correlator::set_filter(
        const std::map<short, std::vector<double>>& filter,
        const std::vector<unsigned int>& micro_times_1,
        const std::vector<signed char>& routing_channels_1,
        const std::vector<unsigned int>& micro_times_2,
        const std::vector<signed char>& routing_channels_2
){
    is_valid = false;
    p1.set_weights(filter, micro_times_1, routing_channels_1);
    p2.set_weights(filter, micro_times_2, routing_channels_2);
}

void Correlator::ccf_felekyan(
        const unsigned long long *t1,
        const unsigned long long *t2,
        const double *weights1,
        const double *weights2,
        unsigned int nc,
        unsigned int nb,
        unsigned int np1,
        unsigned int np2,
        const unsigned long long *xdat, double *corrl
) {
    // t1, t2:              macrotime vectors
    // xdat:                correlation time bins (timeaxis)
    // np1, np2:            number of photons in each channel
    // photons1, photons2:  photon weights
    // nc:                  number of evenly spaced elements per block
    // nb:                  number of blocks of increasing spacing
    // corrl:               pointer to correlation output
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Copying data to new arrays..." << std::endl;
#endif
    // the arrays can be modified inplace during the correlation. Thus, copied to a new array.
    auto t1c = (unsigned long long *) malloc(sizeof(unsigned long long) * np1);
    std::memcpy(t1c, t1, sizeof(unsigned long long) * np1);
    auto t2c = (unsigned long long *) malloc(sizeof(unsigned long long) * np2);
    std::memcpy(t2c, t2, sizeof(unsigned long long) * np2);
    auto w1 = (double *) malloc(sizeof(double) * np1);
    std::memcpy(w1, weights1, sizeof(double) * np1);
    auto w2 = (double *) malloc(sizeof(double) * np2);
    std::memcpy(w2, weights2, sizeof(double) * np2);

    //Initializes some variables for for loops
    unsigned int i=0;
    unsigned int j=0;
    unsigned int k=0;
    unsigned int p=0;
    unsigned int im;

    //Initializes some parameters
    unsigned long long index;
    unsigned long long pw;
    unsigned long long limit_l;
    unsigned long long limit_r;

    // Goes through every block
    for (k=0;k<nb;k++)
    {
        // Determines spacing; used 2time spacing of one
        if (k==0) {pw=1;}
        else {pw=pow(2, k-1);};

        // p is the starting photon in second array
        p=0;

        // Goes through every photon in first array
        for (i=0;i<np1;i++) {
            //if (photons1[i]!=0)
            //{
            // Calculates minimal and maximal time for photons in second array
            limit_l= (unsigned long long)(xdat[k*nc]/pw + t1c[i]);
            limit_r= limit_l+nc;

            j=p;
            while ((j<np2) && (t2c[j] <= limit_r))
            {
                //if (photons2[j]!=0)
                //{
                if (k == 0) // Special Case for first round to include the zero time delay bin
                {
                    // If correlation time is positiv OR equal
                    if (t2c[j] >= limit_l)
                    {
                        // Calculates time between two photons
                        index= t2c[j] - limit_l + (unsigned long long)(k * nc);
                        // Adds one to correlation at the appropriate timelag
                        corrl[index]+=(double) (w1[i] * w2[j]);
                    }
                        // Increases starting photon in second array, to save time
                    else {p++;}
                }
                else
                {
                    // If correlation time is positiv
                    if (t2c[j] > limit_l)
                    {
                        // Calculates time between two photons
                        index= t2c[j] - limit_l + (unsigned long long)(k * nc);
                        // Adds one to correlation at the appropriate timelag
                        corrl[index]+=(double) (w1[i] * w2[j]);
                    }
                        // Increases starting photon in second array, to save time
                    else {p++;}
                }
                //}
                j++;
            };
            //};
        };
        //After second iteration;
        if (k>0)
        {
            // Bitwise shift right => Corresponds to dividing by 2 and rounding down
            // If two photons are in the same time bin, sums intensities and sets one to 0 to save calculation time
            for(im=0;im<np1;im++) { t1c[im] /= 2;};
            for(im=1;im<np1;im++)
            {
                if (t1c[im] == t1c[im - 1])
                { w1[im]+=w1[im - 1]; w1[im - 1]=0;};
            };
            for(im=0;im<np2;im++) { t2c[im] /= 2;};
            for(im=1;im<np2;im++)
            {
                if (t2c[im] == t2c[im - 1])
                { w2[im]+=w2[im - 1]; w2[im - 1]=0;};
            };
        };
        //
        j=0;
        for (i=0; i<np1; i++)
        {
            if (w1[i] != 0)
            {
                w1[j] = w1[i];
                t1c[j] = t1c[i];
                j++;
            }
        }
        np1=j;
        j=0;
        for (i=0; i<np2; i++)
        {
            if (w2[i] != 0)
            {
                w2[j] = w2[i];
                t2c[j] = t2c[i];
                j++;
            }
        }
        np2=j;
    }
}

inline void ccf_wahl_correlate(
        size_t start_1, size_t end_1,
        size_t start_2, size_t end_2,
        size_t i_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        const unsigned long long *t1, const double *w1, size_t nt1,
        const unsigned long long *t2, const double *w2, size_t nt2
) {
    size_t i1, i2, p, index;
    start_1 = std::max(start_1, (size_t) 0);
    start_2 = std::max(start_2, (size_t) 0);
    end_1 = std::min(nt1, end_1);
    end_2 = std::min(nt2, end_2);
    auto scale = (unsigned long long) pow(2.0, i_casc);
    size_t tau_offset = taus[i_casc * n_bins];
    size_t offset = tau_offset / scale;

    p = start_2;
    for (i1 = start_1; i1 < end_1; i1++) {
        if (w1[i1] == 0) continue;
        size_t edge_l = t1[i1] + offset;
        size_t edge_r = edge_l + n_bins;
        for (i2 = p; i2 < end_2; i2++) {
            if (t2[i2] > edge_r) break;
            if (t2[i2] > edge_l) {
                index = t2[i2] - edge_l + i_casc * n_bins;
                corr[index] += (w1[i1] * w2[i2]);
            } else {
                p++;
            }
        }
    }
}

void Correlator::ccf_wahl(
        size_t n_casc, size_t n_bins,
        std::vector<unsigned long long> &taus, std::vector<double> &corr,
        CorrelatorPhotonStream &p1,
        CorrelatorPhotonStream &p2
) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "CORRELATOR::CCF" << std::endl;
    std::clog << "-- Copying data to new arrays..." << std::endl;
#endif
    // the photon streams are modified inplace. Thus, copies are creates
    CorrelatorPhotonStream s1(p1);
    CorrelatorPhotonStream s2(p2);
#ifdef VERBOSE_TTTRLIB
    std::clog << "taus:" << std::endl;
    for(auto v: taus){
        std::clog << v << ",";
    }
#endif
    for (size_t i_casc = 0; i_casc < n_casc; i_casc++) {
        ccf_wahl_correlate(
                0, s1.size(),
                0, s2.size(),
                i_casc, n_bins,
                taus, corr,
                s1.times.data(), s1.weights.data(), s1.size(),
                s2.times.data(), s2.weights.data(), s2.size()
        );
        s1.coarsen();
        s2.coarsen();
    }
}

void Correlator::ccf_laurence(
            std::vector<unsigned long long> &taus, 
            std::vector<double> &corr,
            CorrelatorPhotonStream &p1,
            CorrelatorPhotonStream &p2
){
    int nbins = taus.size();
    long i, j, k, l;
    
    std::vector<long> jmin(taus.size(), 0);
    std::vector<long> jmax(taus.size(), 0);

    for(i = 0; i < p1.size(); i++){
        auto ti = p1.times[i];
        double w1 = p1.weights[i];
        
        for(int k = 0; k < nbins - 1; k++){
            double tau_min = taus[k + 0]; // lower edge of tau bin
            double tau_max = taus[k + 1]; // upper edge of tau bin

            if(k == 0){
                j = jmin[k];
                for(; (j < p2.size()) && ((p2.times[j] - ti) < tau_min); j++);
            }
            jmin[k] = j;
            
            j = std::max(jmax[k], j);
            for(; (j < p2.size()) && ((p2.times[j] - ti) < tau_max); j++);
            jmax[k] = j;

            // add weight
            double w2 = 0.0; 
            for(l = jmin[k]; l < jmax[k]; l++) w2 += p2.weights[l];
            corr[k] += w1 * w2; 

        }

    }
}

void Correlator::normalize(Correlator* correlator, CorrelatorCurve &curve){
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Normalizing correlation curve..." << std::endl;
#endif
    for(size_t i=0; i < curve.corr_normalized.size(); i++) curve.corr_normalized[i] = curve.correlation[i];
    uint64_t maximum_macro_time = correlator->dt();
    if(correlator->correlation_method == "wahl"){
        normalize_ccf_wahl(
                correlator->p1.sum_of_weights(), correlator->p1.dt(),
                correlator->p2.sum_of_weights(), correlator->p2.dt(),
                curve.x_axis,
                curve.corr_normalized,
                curve.settings.n_bins
        );
    } else if (correlator->correlation_method == "felekyan") {
        normalize_ccf_felekyan(
                curve.x_axis, curve.correlation,
                curve.x_axis,
                curve.corr_normalized,
                correlator->p1.mean_count_rate(), correlator->p2.mean_count_rate(),
                curve.settings.n_bins,
                curve.settings.n_casc,
                maximum_macro_time
        );
    } else if (correlator->correlation_method == "laurence") {
        normalize_ccf_laurence(
            correlator->p1,
            correlator->p2,
            curve.x_axis, 
            curve.correlation,
            curve.corr_normalized         
        );
    }
}

void Correlator::normalize_ccf_wahl(
        double np1, uint64_t dt1,
        double np2, uint64_t dt2,
        std::vector<unsigned long long> &x_axis, 
        std::vector<double> &corr,
        size_t n_bins
) {
    double cr1 = (double) np1 / std::max(1.0, (double) dt1);
    double cr2 = (double) np2 / std::max(1.0, (double) dt2);
    double maximum_macro_time = (double) std::max(dt1, dt2);
    for (unsigned int j = 0; j < x_axis.size(); j++) {
        double pw = (uint64_t) pow(2.0, (int) (double(j - 1) / n_bins));
        double delta_t = (double) (maximum_macro_time - x_axis[j]);
        corr[j] /= pw; 
        corr[j] /= (cr1 * cr2 * delta_t);
    }
}

void Correlator::normalize_ccf_laurence(
        CorrelatorPhotonStream &p1,
        CorrelatorPhotonStream &p2,
        std::vector<unsigned long long> &axis, 
        std::vector<double> &corr,
        std::vector<double> &corr_normalized
) {
    double dt1 = p1.dt();
    double dt2 = p2.dt();
    double duration = std::max(dt1, dt2);
    double n1 = p1.sum_of_weights();
    double n2 = p2.sum_of_weights();

    for (int i = 0; i < axis.size() - 1; i++) {
        double dtau = axis[i + 1] - axis[i];
        double scale = (duration / dtau - 1.0);

        // double w1 = 0.0;
        // for(int i = 0; i < p1.size(); i++)
        //     w1 += p1.weights[i] * (p1.times[i] >= dtau);

        // double w2 = 0.0;
        // double dt_dtau = dt2 - dtau;
        // for(int i = 0; i < p2.size(); i++)
        //     w2 += p2.weights[i] * (p2.times[i] <= dt_dtau);

        // corr_normalized[i + 1] = corr[i] * scale / (w1 * w2);

        corr_normalized[i + 1] = corr[i] * scale / (n1 * n2);

    }

}


void Correlator::normalize_ccf_felekyan(
        std::vector<unsigned long long> &x_axis,
        std::vector<double> &corr,
        std::vector<unsigned long long> &x_axis_normalized,
        std::vector<double> &corr_normalized,
        double cr1, double cr2,
        unsigned int n_bins,
        unsigned int n_casc,
        unsigned long long maximum_macro_time
){
    std::vector<double> divisor;
    // Compute the coarsening factor
    divisor.resize(x_axis.size());
    std::fill(divisor.begin(), divisor.end(), 1.0);
    unsigned int k = n_bins + 1;
    for(unsigned int j=0; j < n_casc; j++) {
        for (unsigned int i = 0; (i < n_bins) && ((unsigned int) k < divisor.size()); i++) {
            divisor[k] = std::pow(2.0, (double) j);
            k++;
        }
    }
    for(unsigned int i=0; i < corr.size(); i++){
        auto delta_t = (double) (maximum_macro_time - x_axis[i]);
        corr_normalized[i] = corr[i] / (divisor[i] * delta_t * cr1 * cr2);
    }
}

std::pair<std::shared_ptr<TTTR>, std::shared_ptr<TTTR>> Correlator::get_tttr() {
    return {this->p1.tttr, this->p2.tttr};
}

void Correlator::get_x_axis(double** output, int* n_output){
    curve.get_x_axis(output, n_output);
}

void Correlator::get_corr(double** output, int* n_output){
    if(!is_valid) run();
    curve.get_corr(output, n_output);
}

void Correlator::get_corr_normalized(double** output, int* n_output){
    if(!is_valid) run();
    curve.get_corr_normalized(output, n_output);
}