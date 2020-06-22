
#include "statistics.h"


double statistics::chi2_counting(
        std::vector<double> &data,
        std::vector<double> &model,
        int x_min,
        int x_max,
        std::string type
){
    double chi2 = 0.0;
    if(type == "neyman"){
        for(int i = x_min; i < x_max; i++){
            double mu = model[i];
            double m = std::max(1., data[i]);
            chi2 += (mu - m) * (mu - m) / m;
        }
    } else if(type == "poisson"){
        for(int i = x_min; i < x_max; i++){
            double mu = model[i];
            double m = data[i];
            if (mu > 0) {
                if (m <= 1e-12)
                    chi2 += 2 * mu;
                else
                    chi2 += 2 * (mu - m - m * log(mu / m));
            }
        }
    } else if(type == "pearson"){
        for(int i = x_min; i < x_max; i++){
            double m = model[i];
            double d = data[i];
            if (m > 0) {
                chi2 += (m-d) / m;
            }
        }
    } else if(type == "gauss"){
        for(int i = x_min; i < x_max; i++){
            double mu = model[i];
            double m = data[i];
            double mu_p = std::sqrt(.25 + m * m) - 0.5;
            if(mu_p <= 1.e-12) continue;
            chi2 += (mu - m) * (mu - m) / mu + std::log(mu/mu_p) - (mu_p - m) * (mu_p - m) / mu_p;
        }
    } else if(type == "cnp"){
        for(int i = x_min; i < x_max; i++){
            double m = data[i];
            if(m <= 1e-12) continue;
            double mu = model[i];
            chi2 += (mu - m) * (mu - m) / (3. / (1./m + 2./mu));
        }
    }
    return chi2;
}
